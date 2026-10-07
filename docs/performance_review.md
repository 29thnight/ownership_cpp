# CPU 성능 공학 관점의 검토와 개선 계획

[usamahz/cpu-performance-engineering](https://github.com/usamahz/cpu-performance-engineering)의
원칙(측정 방법, 메모리 계층, 동시성, 컴파일러/코드 생성)을 이 라이브러리에
적용한 결과입니다. 기준 시점은 `5761f18`(unique 소유권 문서화) 위의 작업 브랜치입니다.

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

## 3. 남은 개선점 (우선순위 순)

각 항목에 근거, 예상 효과, 그리고 효과를 증명할 측정 방법을 적었습니다. 측정 전에는
어떤 것도 개선이라고 주장하지 않는 것이 참고 저장소의 원칙입니다.

### P1. 공유 control block 헤더 축소 (메모리 계층)

- **근거:** `detail::control_block`이 72바이트입니다(strong, weak, `allocator_ref` 3워드,
  `retirement_hook` 2워드, `dispose`, `destroy`). 8바이트 payload의
  `in_place_control`은 80바이트이고, libstdc++ `make_shared` 블록은 24바이트입니다.
  glibc 청크 기준으로는 96바이트 대 32바이트라서, 작은 객체가 많은 경우 캐시와
  메모리 사용량이 약 3배입니다.
- **방안:** `dispose`/`destroy`는 타입별 정적 테이블 포인터 하나로 합칩니다(−8B).
  기본 할당자와 hook이 없는 경우는 별도 블록 타입으로 분리해 해당 필드를 빼서,
  목표 32~40바이트로 줄입니다. 사용자 지정 할당자나 retirement hook이 있을 때만
  확장 블록을 씁니다.
- **주의:** 지금은 헤더가 72바이트라서 payload가 항상 카운터와 다른 캐시 라인에서
  시작합니다(우연한 이점). 헤더를 줄이면 카운터와 payload가 같은 라인을 공유해서,
  다른 코어가 owner를 복사할 때 payload 읽기가 라인 이동 비용을 함께 냅니다
  (참고 저장소 섹션 9의 false sharing). 메모리 이득과 경합 비용을 둘 다 측정해야 합니다.
- **측정:** `create_read_destroy`, 4,096개 asset 스캔(캐시 상주 vs 비상주),
  "한 스레드는 payload 읽기, 나머지는 owner 복사" 케이스를 헤더 크기별로 비교합니다.

### P2. `allocated_unique_owner`의 move 비용

- **근거:** 상위 커밋의 정밀 측정에서 move 1.548×, borrow/read 1.242×, vector 이동
  1.140×(erased std40 대비)가 남았습니다. 핸들이 5워드(40B)라서 move할 때마다 5워드를
  복사하고 원본을 비워야 합니다.
- **방안:** 할당 앞부분에 정리 정보(context, deallocate, dispose, 저장소 오프셋)를 담는
  헤더를 두고, 핸들은 `T*`와 헤더 포인터 2워드(16B)만 들고 있게 합니다. 할당 크기는
  헤더만큼 늘어나지만, move·vector·큐 전달 비용은 기본 `unique_owner`에 가까워질
  것으로 예상합니다.
- **측정:** `benchmarks/precision`의 기존 A/A 대조를 포함한 설계로 move, vector, queue를 비교합니다.

### P3. strong 감소의 메모리 순서 (Arm)

- **근거:** 모든 감소가 `acq_rel`입니다. x86에서는 `lock xadd` 하나로 같지만, Arm에서는
  `release` 감소 후 0에 도달했을 때만 acquire하는 형태가 더 쌉니다(참고 저장소
  섹션 9의 C/C++11 매핑 표).
- **방안:** `fetch_sub(1, release)`를 쓰고, 0에 도달한 경우에만 `load(acquire)`를 합니다.
  펜스 대신 load를 쓰면 TSan과 호환됩니다.
- **측정:** 이 VM(x86)에서는 효과를 볼 수 없습니다. Arm(Graviton 등) 측정 전까지는
  변경을 보류합니다.

### P4. `local_group` 별도 할당

- **근거:** `make_local`은 할당이 2번(블록 + 48B 그룹)이고, `localize()`마다 1번씩 더
  필요합니다. 이번 레이아웃 수정으로 release 빌드 그룹도 40B에서 48B가 됐습니다
  (glibc 청크 48B에서 64B).
- **방안:** 첫 그룹을 블록에 함께 배치하는 옵션을 두거나, `references`와 `thread_id`를
  32비트로 줄여 40B로 복원하는 것을 검토합니다(한계에 도달하면 fail-fast).
- **측정:** 기존 `4096_independent_localizations`, `create_local` 케이스.

### P5. 측정 인프라 보강

- 스케일링 벤치마크에 선택적 CPU 고정(`taskset`/`pthread_setaffinity_np`)과 A/A
  대조(같은 구현 두 벌)를 추가합니다. 이번 데이터에서는 identical-code std의 4스레드
  중앙값이 327~373ns로 움직였으므로, 경합 행에서 약 15% 미만의 차이는 입증되지 않습니다.
- 지원되는 환경에서는 `perf stat`으로 cycles, `machine_clears.memory_ordering`,
  HITM 계열 이벤트를 수집해 라인 이동이 원인이라는 것을 카운터로 확인합니다(이
  VM에서는 PMU를 쓸 수 없음).
- Arm(LSE 유무), MSVC, macOS에서의 재현.

### P6. API 소소한 개선

- `owner == nullptr` 비교 연산자가 없습니다(비교와 해시는 의도적으로 제외했다고
  문서화됨). 최소한 null 비교 정도는 사용성 측면에서 검토할 만합니다.
- `local_owner`의 스레드 오용은 release에서 검출되지 않는다는 점은 문서화되어 있습니다.
  release에서도 켤 수 있는 저비용 검사 모드(생성 시 ID 저장은 이제 레이아웃상 공짜)를
  고려할 수 있습니다.
