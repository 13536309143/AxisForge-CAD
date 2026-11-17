@echo off
cd /d "%~dp0"
if exist "run\AxisForgeCAD-1.0.1.exe" (
  start "" "run\AxisForgeCAD-1.0.1.exe"
) else (
  start "" "run\AxisForgeCAD.exe"
)
