@echo off
rem Simulateur MONOCORE-1 servi en local (clavier MIDI possible dans Firefox)
cd /d "%~dp0"
where python >nul 2>nul && (python simulateur.py) || (py simulateur.py)
pause
