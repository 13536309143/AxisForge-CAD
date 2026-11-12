@echo off
setlocal
cd /d "%~dp0"
if not defined QT_ROOT set "QT_ROOT=E:\ProgramData\anaconda3\Library"
if not defined OCCT_ROOT set "OCCT_ROOT=%~dp0deps\occt"
if not exist "%OCCT_ROOT%\cmake\OpenCASCADEConfig.cmake" (
    echo Set OCCT_ROOT to your OCCT SDK installation prefix.
    goto failed
)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DCMAKE_PREFIX_PATH=%QT_ROOT%;%OCCT_ROOT%"
if errorlevel 1 goto failed
cmake --build build --config Release --parallel 1
if errorlevel 1 goto failed
set "PATH=%~dp0run;%OCCT_ROOT%\win64\vc14\bin;%QT_ROOT%\bin;%PATH%"
ctest --test-dir build -C Release --output-on-failure
if errorlevel 1 goto failed
if exist "run\" copy /y "build\Release\AxisForgeCAD.exe" "run\AxisForgeCAD.exe" >nul
echo Build and tests completed.
pause
exit /b 0
:failed
echo Build or tests failed. See messages above.
pause
exit /b 1
