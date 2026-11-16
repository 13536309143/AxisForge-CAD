@echo off
cd /d "%~dp0"
if exist "run\AxisForgeCAD-0.5.1.exe" (
  start "" "run\AxisForgeCAD-0.5.1.exe"
) else (
  start "" "run\AxisForgeCAD.exe"
)
