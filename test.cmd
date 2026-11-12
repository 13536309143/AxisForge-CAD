@echo off
cd /d "%~dp0"
run\axisforge_tests.exe
if errorlevel 1 goto failed
start /wait "" "run\AxisForgeCAD.exe" --vsync=1 --self-test "%~dp0test-artifacts"
if errorlevel 1 goto failed
start /wait "" "run\AxisForgeCAD.exe" --vsync=0 --self-test "%~dp0test-artifacts-vsync-off"
if errorlevel 1 goto failed
echo All tests passed.
pause
exit /b 0
:failed
echo Test failure. Review test-artifacts and console messages.
pause
exit /b 1
