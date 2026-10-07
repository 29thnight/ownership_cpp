#!/usr/bin/env python3
"""All samples retained. Equal-weight process-level inference on paired log ratios."""
import argparse, csv, json, math, statistics, os
from collections import defaultdict
from pathlib import Path
import numpy as np
from scipy.stats import t
p=argparse.ArgumentParser();p.add_argument('directory',type=Path);p.add_argument('--bootstrap',type=int,default=0);args=p.parse_args()
rows=[]
for file in sorted(args.directory.glob('p[0-9]*.csv')):
    with file.open() as f: rows.extend(csv.DictReader(f))
assert rows, 'no rows'
by=defaultdict(dict)
for r in rows:
    key=(r['run'],r['case'],int(r['round']),r['implementation'])
    assert int(r['pass']) not in by[key], 'duplicate pass'
    assert int(r['cpu'])==int(os.environ.get('UNIQUE_PRECISION_MAIN_CPU','2')), 'main thread moved off pinned CPU'
    assert all(math.isfinite(float(r[k])) and float(r[k])>0 for k in ['total_ns','cpu_ns','operations'])
    by[key][int(r['pass'])]=r
assert all(set(v)=={0,1} for v in by.values()),'missing reversed pass'
blocks=defaultdict(dict)
for (run,case,rnd,impl),passes in by.items():
    a,b=passes[0],passes[1]
    assert all(a[k]==b[k] for k in ['operations','repetitions','checksum'])
    blocks[(run,case,rnd)][impl]={metric:statistics.mean(float(x[metric])/int(x['operations']) for x in passes.values()) for metric in ['total_ns','cpu_ns']}
expected={'std8a','std8b_same_code','std8clone','own8','std40a','std40b_same_code','std40clone','own40'}
assert all(set(v)==expected for v in blocks.values()), 'missing implementation'
for key,v in blocks.items():
    rs=[r for r in rows if (r['run'],r['case'],int(r['round']))==key]
    assert len({(r['checksum'],r['operations']) for r in rs})==1,'unequal workload'
comparisons=[('control8_same_code','std8b_same_code','std8a'),('control8_clone','std8clone','std8a'),('own8','own8','std8a'),('control40_same_code','std40b_same_code','std40a'),('control40_clone','std40clone','std40a'),('own40','own40','std40a')]
cases=sorted({key[1] for key in blocks}); runs=sorted({key[0] for key in blocks}); result=[]
rng=np.random.default_rng(202610071)
for case in cases:
    for name,numerator,denominator in comparisons:
        logs=[]; cpulogs=[]; ratios=[]
        for run in runs:
            selected=[(rnd,v) for (r,c,rnd),v in blocks.items() if r==run and c==case]
            selected.sort()
            logs.append(np.array([math.log(v[numerator]['total_ns']/v[denominator]['total_ns']) for rnd,v in selected]))
            cpulogs.append(np.array([math.log(v[numerator]['cpu_ns']/v[denominator]['cpu_ns']) for rnd,v in selected]))
            ratios.extend(np.exp(logs[-1]))
        process_means=np.array([x.mean() for x in logs]); mean=process_means.mean()
        stderr=float(process_means.std(ddof=1)/math.sqrt(len(runs))) if len(runs)>1 else math.nan
        radius=t.ppf(.975,len(runs)-1)*stderr
        upper=t.ppf(1-.05/12,len(runs)-1)*stderr
        output=dict(case=case,comparison=name,processes=len(runs),blocks=sum(map(len,logs)),ratio=math.exp(mean),ci95_low=math.exp(mean-radius),ci95_high=math.exp(mean+radius),simultaneous_12_one_sided95_upper=math.exp(mean+upper),process_ratios=[math.exp(x) for x in process_means],paired_median=statistics.median(ratios),paired_p10=float(np.quantile(ratios,.1)),paired_p90=float(np.quantile(ratios,.9)),cpu_ratio=math.exp(statistics.mean(float(x.mean()) for x in cpulogs)))
        if args.bootstrap:
            # Hierarchical moving-block bootstrap: resample processes, then
            # circular contiguous groups of four paired blocks inside each.
            samples=[]
            for _ in range(args.bootstrap):
                means=[]
                for proc in rng.integers(0,len(logs),size=len(logs)):
                    values=logs[proc]; n=len(values); starts=rng.integers(0,n,size=math.ceil(n/4)); ids=((starts[:,None]+np.arange(4))%n).ravel()[:n]
                    means.append(float(values[ids].mean()))
                samples.append(math.exp(statistics.mean(means)))
            output['hierarchical_block_bootstrap95_low']=float(np.quantile(samples,.025));output['hierarchical_block_bootstrap95_high']=float(np.quantile(samples,.975))
        result.append(output)
(args.directory/'summary.json').write_text(json.dumps(dict(method='Geometric mean of forward/reverse paired wall-time ratios; equal weight per independent process. Primary interval: Student t over process mean log ratios. No outlier removal. Familywise upper bound Bonferroni-adjusts twelve real own/std workload comparisons. A/A controls assess harness precision and layout sensitivity; repeated processes do not vary code layout.',runs=runs,rows=len(rows),summary=result),indent=2)+'\n')
fields=list(result[0]);
with (args.directory/'summary.csv').open('w') as f:
    w=csv.DictWriter(f,fields);w.writeheader();w.writerows(result)
for r in result:
    print(f"{r['case']:27s} {r['comparison']:21s} {r['ratio']:.5f} [{r['ci95_low']:.5f}, {r['ci95_high']:.5f}] processes="+', '.join(f'{x:.4f}' for x in r['process_ratios']))
print(f'Validated {len(rows)} rows in {len(runs)} independent processes; retained all samples')
