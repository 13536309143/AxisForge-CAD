@echo off
setlocal
cd /d "%~dp0"
if not defined QT_ROOT if defined QT_ROOT_DIR set "QT_ROOT=%QT_ROOT_DIR%"
if not defined QT_ROOT (
  echo Set QT_ROOT to the Qt MSVC x64 installation prefix.
  goto failed
)
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
if exist "%QT_ROOT%\plugins\platforms" set "QT_QPA_PLATFORM_PLUGIN_PATH=%QT_ROOT%\plugins\platforms"
if not exist "%QT_ROOT%\plugins\platforms" if exist "run\platforms" set "QT_QPA_PLATFORM_PLUGIN_PATH=%~dp0run\platforms"
ctest --test-dir build -C Release --output-on-failure
if errorlevel 1 goto failed
if exist "run\" (
  copy /y "build\Release\AxisForgeCAD.exe" "run\AxisForgeCAD-1.0.1.exe" >nul
  if errorlevel 1 goto failed
  copy /y "build\Release\AxisForgeCAD.exe" "run\AxisForgeCAD.exe" >nul 2>&1
)
echo Build and tests completed.
pause
exit /b 0
:failed
echo Build or tests failed. See messages above.
pause
exit /b 1
