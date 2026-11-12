@echo off
cd /d "%~dp0"
set "AXISFORGE_APP=run\AxisForgeCAD.exe"
if exist "run\AxisForgeCAD-0.4.0.exe" set "AXISFORGE_APP=run\AxisForgeCAD-0.4.0.exe"
run\axisforge_tests.exe
if errorlevel 1 goto failed
start /wait "" "%AXISFORGE_APP%" --vsync=1 --self-test "%~dp0test-artifacts"
if errorlevel 1 goto failed
start /wait "" "%AXISFORGE_APP%" --vsync=0 --self-test "%~dp0test-artifacts-vsync-off"
if errorlevel 1 goto failed
echo All tests passed.
pause
exit /b 0
:failed
echo Test failure. Review test-artifacts and console messages.
pause
exit /b 1
