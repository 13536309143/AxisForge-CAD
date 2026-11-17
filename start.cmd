@echo off
cd /d "%~dp0"
if exist "run\AxisForgeCAD-1.0.2.exe" (
  start "" "run\AxisForgeCAD-1.0.2.exe"
) else (
  start "" "run\AxisForgeCAD.exe"
)
