@echo off
cd /d "%~dp0"
if exist "run\AxisForgeCAD-0.4.0.exe" (
  start "" "run\AxisForgeCAD-0.4.0.exe"
) else (
  start "" "run\AxisForgeCAD.exe"
)
