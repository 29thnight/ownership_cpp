# CPU 성능 공학 관점의 검토와 개선 계획

[usamahz/cpu-performance-engineering](https://github.com/usamahz/cpu-performance-engineering)의
원칙(측정 방법, 메모리 계층, 동시성, 컴파일러/코드 생성)을 이 라이브러리에
적용한 결과입니다. 기준 시점은 `5761f18`(unique 소유권 문서화) 위의 작업 브랜치이며, 3장에 P1~P7 진행 결과를 정리했습니다.

## 1. 적용한 원칙

| 참고 저장소의 원칙 | 이 저장소에 적용한 방식 |
|---|---|
| 숫자에는 7개 필드(CPU, 사용 코어, 주파수/SMT, 컴파일러·플래그, 워크로드, 기준선, 방법)가 붙어야 함 | `scripts/benchmark_scaling.sh`가 `metadata.txt`에 7개 필드를 기록 |
| 단일 실행은 측정이 아님. 워밍업 폐기, 반복, 지연은 min·처리량은 median, 항상 CV | 워밍업 2회 폐기, 15회 측정, median/min/p90/CV, 독립 프로세스 반복(primary/repeat) |
| 결과를 소비하거나 참조값과 비교해 최적화가 작업을 지우지 못하게 함 | 모든 샘플에서 payload 체크섬과 카운트 복귀(=1)를 검증, `escape()` 사용 |
| 주장을 코드 생성으로 증명 | `scaling_increment_codegen.txt`에 변경 전 `lock cmpxchg` 루프와 변경 후 `lock xadd`를 기록 |
| 대조군으로 메커니즘을 분리 | 경합 없는 private 복사와 CAS가 남은 weak lock을 대조군으로 함께 측정 |
| CI에서 QUICK 스모크 실행 | `QUICK=1` 모드와 GitHub Actions 스모크 실행(타이밍은 근거로 쓰지 않음) |
| 캐시 라인 경합이 동시성 비용의 본질(섹션 9) | 하나의 control block 라인을 1..N 스레드가 공유하는 스케일링 벤치마크 |

## 2. 이번에 반영한 작업

### 2.1 `local_group` ODR/레이아웃 불일치 수정 (정확성)

`OWN_DEBUG_THREAD_CHECK`가 `NDEBUG`에 따라 `thread_id` 필드를 넣고 빼서, release TU와
debug TU를 함께 링크하면 같은 구조체 크기가 40/48바이트로 달라졌습니다. ASan에서
heap-buffer-overflow로 재현했습니다. 이제 필드는 항상 존재하고, 검사 없이 생성된
그룹은 0을 저장해 검사를 건너뜁니다. 두 TU를 섞어 링크하고 정확한 크기를 검사하는
할당자로 확인하는 회귀 테스트를 추가했습니다.

### 2.2 참조 카운트 증가를 단일 locked add로 변경 (성능)

| 4 vCPU Xeon VM, 공유 owner 복사+해제 (ns/쌍, primary/repeat) | 2 스레드 | 4 스레드 |
|---|---:|---:|
| own, CAS 루프 (이전) | 156.5 / 141.9 | 367.7 / 321.1 |
| own, `fetch_add` (현재) | **96.5 / 91.1** | **206.4 / 192.9** |
| `std::shared_ptr` (같은 실행) | 155.7 / 142.4 | 373.4 / 342.5 |

경합이 있을 때 36~44% 줄었고, 이제 std보다 빠릅니다. 대조군(경합 없는 복사,
weak lock)은 노이즈 범위 안에서 변하지 않았습니다. 0을 지나 되돌아가는 것은
`saturation_limit`(카운터 범위의 절반)에서 abort해 막고, 나머지 절반 범위가 이미
진행 중인 증가를 흡수합니다. 자세한 내용은 [scaling 보고서](benchmark_scaling.md)에 있습니다.

### 2.3 Clang 지원과 CI

- 테스트 두 곳 때문에 Clang에서 실패했습니다(라이브러리 버그는 아님). 하나는
  `-Wself-assign-overloaded`이고, 다른 하나는 [expr.new]/14에 따른 `new`/`delete` 쌍
  생략으로 할당 실패 주입이 무력화된 것입니다. 두 곳을 수정해 Clang 18
  debug/release가 통과합니다.
- `.github/workflows/ci.yml`: GCC/Clang × debug/release/ASan/UBSan/TSan,
  모든 벤치마크 하네스의 `-Werror` 컴파일, 스케일링 벤치마크 QUICK 스모크, 예제 실행.

## 3. 개선점 진행 결과

모든 항목은 변경 전후 헤더를 같은 하네스로 두 번(primary/repeat) 측정했고, std를 두 번
돌리는 A/A 대조군으로 노이즈 범위를 함께 확인했습니다. 결과 표와 원자료는 각 보고서에 있습니다.

| 항목 | 결과 | 핵심 수치 | 보고서 |
|---|---|---|---|
| P1 control block 헤더 축소 | **적용** | 헤더 72B→24B, 16k 스캔 1.23→0.94ns, 256k 스캔 8.7→7.7ns | [layout](benchmark_layout.md) |
| P4 `local_group` 축소 | **적용** | 48B→40B (쓰지 않는 allocate 콜백 제거) | [layout](benchmark_layout.md) |
| P7 카운트 한 워드 패킹 (새로 발견) | **적용** | 헤더 24B→16B, `make_shared<8B>` 24B로 std와 동일, 공유되지 않는 객체 생성·소멸 21.8→17.9ns | [counts](benchmark_counts.md) |
| P3 감소 연산 메모리 순서 | **적용** | Arm: `ldaddal`×2 → `ldaddl`+`ldar` (코드 생성으로 확인, Arm 실측은 없음) | [counts](benchmark_counts.md) |
| P5 측정 인프라 | **적용** | `--pin 1` CPU 고정, std A/A 대조, 공통 7필드 메타데이터 스크립트 | 각 보고서 |
| P6 null 비교 | **적용** | `owner == nullptr` 등 5개 타입. 핸들끼리 비교·순서 비교·weak 비교는 계속 금지 | README |
| P2 `allocated_unique_owner` 16B | **측정만** (계약 유지) | vector 이동 −39~45%, 생성 −9%, 단일 move는 개선 없음, 할당 +33% | [allocated unique](allocated_unique_layout.md) |

### 측정으로 드러난 트레이드오프

- **P1 헤더 축소 → 카운터와 payload가 같은 캐시 라인을 씀.** 다른 코어가 owner를 복사하는
  동안 같은 객체를 읽는 스레드의 비용이 0.28ns에서 약 5ns로 늘었습니다. std도 같은 구조라
  1.3~2.1ns를 냅니다. 이런 hot 객체는 payload를 `alignas(64)`로 선언하면 0.27ns로 돌아오며,
  이 방법을 README와 design 문서에 적었습니다.
- **P7은 두 가지 방식을 실측해서 비교.** libstdc++처럼 감소 전에 워드를 먼저 읽는 방식은
  생성·소멸이 12.5ns로 std와 같아지지만, 경합 없는 복사가 24%, 경합 복사가 31~40%
  느려졌습니다. 공유 소유권의 본업은 복사이므로 이 방식은 채택하지 않았습니다. 공유되지
  않는 객체에는 원자 연산이 없는 `unique_owner`가 맞습니다.
- **카운트 한계가 2^31로 바뀜.** 이전에는 2^63이었고, std(`int`)와 같은 수준입니다.

## 4. 재측정 후 최적화 (이후 라운드)

모든 벤치마크를 다시 측정해서 std보다 일관되게 느린 경우를 찾고 최적화했습니다.
자세한 내용은 [optimization round 보고서](optimization_round.md)에 있습니다.

| 경우 | 이전 own/std | 이후 own/std | 변경 |
|---|---:|---:|---|
| `make_local` 생성·소멸 | 1.94× | **1.10×** | 블록 하나에 첫 그룹 내장(할당 1회), 독점 그룹은 원자 연산 없이 해제 |
| `localize` | 1.21× | **0.92×** | 스레드별 그룹 저장소 캐시 |
| 그룹 생성+복사 1개 | 0.76× | **0.42×** | 같은 캐시 |
| `allocated_unique_owner` move | 1.18× | **1.00×** | 임시 객체+swap 없이 직접 이동 대입 |
| `allocate_unique` 생성 | 1.11× | **1.01×** | 기본 할당자를 직접 호출해 `operator new`로 인라인 |
| `make_shared` 생성·소멸 | 1.48× | 1.47× | 유지(복사 성능과의 트레이드오프, 아래 참고) |

중간 커밋에서 회귀 두 건(`share_read_drop` +17%, `scene_4096_attachments` +35%)을
재측정으로 발견해 다음 커밋에서 되돌렸습니다.

## 5. 남은 작업

1. **(해결) 공유되지 않는 `make_shared` 객체의 마지막 해제.** 팩토리가 돌려준 핸들에만
   "유일할 수 있음" 힌트를 두고 그 핸들만 해제 전에 카운트를 확인하도록 바꿔, 생성·소멸이
   std와 같아졌고(1.47×→1.00×) 복사도 14% 빨라졌습니다. 자세한 내용은
   [optimization round](optimization_round.md)의 후속 절에 있습니다.
2. **P2 결정.** 컨테이너로 한꺼번에 옮기는 작업이 실제로 지배적일 때만 헤더 방식(할당자
   계약 변경)을 검토합니다. 단일 move 비용은 이번 라운드에서 std와 같아졌습니다.
3. **다른 플랫폼 실측.** Arm(LSE 유무), MSVC, macOS, 그리고 PMU를 쓸 수 있는 환경에서
   `perf stat`으로 라인 이동을 카운터로 확인하는 일.
