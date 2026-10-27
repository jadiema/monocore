#!/bin/sh
# Full compile and report extraction (Linux, or Windows with the Quartus shell in the PATH)
set -e
cd "$(dirname "$0")"
quartus_sh --flow compile monocore_trial
quartus_sta -t trial_report.tcl
echo "Termine. A renvoyer : output_files/monocore_trial.fit.summary, output_files/monocore_trial.flow.rpt, trial_fmax_slow.txt, trial_paths.txt, trial_paths_summary.txt"
