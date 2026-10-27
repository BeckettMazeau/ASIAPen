@echo off
rem Double-click to start NeuroSense. The server runs without a console window and the exam
rem opens in its own Edge/Chrome app window. Status output goes to backups\neurosense.log.
cd /d "%~dp0"
where pythonw >nul 2>nul && (start "" pythonw "%~dp0start_exam.py" %* & exit /b 0)
where pyw >nul 2>nul && (start "" pyw -3 "%~dp0start_exam.py" %* & exit /b 0)
echo Python 3 was not found.
echo Install it from https://www.python.org/downloads/ and tick "Add python.exe to PATH".
pause
