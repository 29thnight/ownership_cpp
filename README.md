# ownership_cpp benchmark data

Raw benchmark and validation output referenced by the reports in `docs/` on
the main branch. It lives on this orphan branch so the library checkout stays
small; nothing here is needed to build or test the library.

Each directory holds one run (`console.txt`, `metadata.txt`, `summary.csv`,
`raw.csv` and checksums where recorded), at the same `benchmarks/results/`
path the scripts write to. New local runs are ignored on the main branch;
add a run here when a report cites it.
