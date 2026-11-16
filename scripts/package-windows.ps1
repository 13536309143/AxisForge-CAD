$ErrorActionPreference='Stop'
$sdk=(Get-ChildItem deps/occt -Filter OpenCASCADEConfig.cmake -Recurse | Select-Object -First 1).Directory.Parent.FullName
cmake -S . -B build -A x64 "-DCMAKE_PREFIX_PATH=$env:QT_ROOT_DIR;$sdk"
if ($LASTEXITCODE) { throw 'Configure failed' }
cmake --build build --config Release --parallel 2
if ($LASTEXITCODE) { throw 'Build failed' }
New-Item -ItemType Directory release/AxisForge-CAD/run -Force | Out-Null
$dest=(Resolve-Path release/AxisForge-CAD/run).Path
Copy-Item build/Release/AxisForgeCAD.exe,build/Release/axisforge_tests.exe $dest
$lookup=@{}
foreach($root in @("$sdk/win64/vc14/bin",'deps/thirdparty',"$env:QT_ROOT_DIR/bin")) {
  Get-ChildItem $root -Filter *.dll -Recurse | ForEach-Object { if(-not $lookup.ContainsKey($_.Name)){$lookup[$_.Name]=$_.FullName} }
}
$dump=(Get-Command dumpbin.exe).Source
$queue=[Collections.Generic.Queue[string]]::new();$queue.Enqueue("$dest/AxisForgeCAD.exe");$queue.Enqueue("$dest/axisforge_tests.exe");$seen=@{}
while($queue.Count){$file=$queue.Dequeue();foreach($line in (& $dump /dependents $file)) {
  if($line -notmatch '^\s+([\w.-]+\.dll)\s*$'){continue};$dll=$Matches[1]
  if($seen.ContainsKey($dll)){continue};$seen[$dll]=$true
  if($lookup.ContainsKey($dll)){Copy-Item -LiteralPath $lookup[$dll] -Destination "$dest/$dll" -Force;$queue.Enqueue("$dest/$dll")}
  elseif(-not (Test-Path "$env:SystemRoot/System32/$dll") -and $dll -notlike 'api-ms-*'){throw "Missing DLL $dll"}
}}
& "$env:QT_ROOT_DIR/bin/windeployqt.exe" --release --compiler-runtime "$dest/AxisForgeCAD.exe"
if($LASTEXITCODE){throw 'Qt deployment failed'}
New-Item -ItemType Directory "$dest/occt-resources","$dest/licenses" -Force | Out-Null
foreach($name in @('XSMessage','SHMessage','XSTEPResource','StdResource','UnitsAPI')) {Copy-Item "$sdk/src/$name" "$dest/occt-resources" -Recurse}
Copy-Item "$sdk/LICENSE_LGPL_21.txt","$sdk/OCCT_LGPL_EXCEPTION.txt" "$dest/licenses"
$n=0
Get-ChildItem deps/thirdparty,$env:QT_ROOT_DIR -Recurse -File | Where-Object {$_.Name -match '^(LICENSE|COPYING|COPYRIGHT)'} | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination "$dest/licenses/$n-$($_.Name)";$n++}
Copy-Item README.md,LICENSE,start.cmd,test.cmd,RELEASE_NOTES.md release/AxisForge-CAD
Copy-Item docs,examples release/AxisForge-CAD -Recurse
& "$dest/axisforge_tests.exe"
if($LASTEXITCODE){throw 'Packaged core tests failed'}
Compress-Archive release/AxisForge-CAD release/AxisForge-CAD-0.5.0-Windows-x64.zip
