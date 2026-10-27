@echo off
rem Full compile and report extraction (Windows). Run from a command prompt where quartus_sh is in the PATH,
rem for example C:\intelFPGA_lite\17.0\quartus\bin64
cd /d "%~dp0"
quartus_sh --flow compile monocore_trial
if errorlevel 1 goto fail
quartus_sta -t trial_report.tcl
echo Termine. A renvoyer : output_files\monocore_trial.fit.summary, output_files\monocore_trial.flow.rpt, trial_fmax_slow.txt, trial_paths.txt, trial_paths_summary.txt
goto end
:fail
echo La compilation a echoue : renvoyer output_files\monocore_trial.flow.rpt et les fichiers .rpt du dossier output_files
:end
pause
