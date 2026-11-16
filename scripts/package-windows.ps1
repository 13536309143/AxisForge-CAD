$ErrorActionPreference='Stop'
if (-not $env:QT_ROOT_DIR) { $env:QT_ROOT_DIR=$env:QT_ROOT }
if (-not $env:QT_ROOT_DIR -or -not (Test-Path "$env:QT_ROOT_DIR/bin/windeployqt.exe")) { throw 'Set QT_ROOT_DIR to a complete Qt MSVC x64 SDK.' }
$match=[regex]::Match((Get-Content CMakeLists.txt -Raw),'project\(AxisForgeCAD VERSION (\d+\.\d+\.\d+)')
if(-not $match.Success){throw 'Missing application version'}
$version=$match.Groups[1].Value
if(-not (Get-ChildItem deps/occt -Filter OpenCASCADEConfig.cmake -Recurse | Select-Object -First 1)){throw 'Run scripts/setup-occt.ps1 first.'}
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
  if($dll -match '^(msvcp[0-9]+|vcruntime[0-9]+|concrt[0-9]+|vccorlib[0-9]+|vcomp[0-9]+).*\.dll$'){continue}
  if($lookup.ContainsKey($dll)){Copy-Item -LiteralPath $lookup[$dll] -Destination "$dest/$dll" -Force;$queue.Enqueue("$dest/$dll")}
  elseif(-not (Test-Path "$env:SystemRoot/System32/$dll") -and $dll -notlike 'api-ms-*'){throw "Missing DLL $dll"}
}}
& "$env:QT_ROOT_DIR/bin/windeployqt.exe" --release --no-compiler-runtime "$dest/AxisForgeCAD.exe"
if($LASTEXITCODE){throw 'Qt deployment failed'}
New-Item -ItemType Directory "$dest/occt-resources","$dest/licenses" -Force | Out-Null
foreach($name in @('XSMessage','SHMessage','XSTEPResource','StdResource','UnitsAPI')) {Copy-Item "$sdk/src/$name" "$dest/occt-resources" -Recurse}
Copy-Item "$sdk/LICENSE_LGPL_21.txt","$sdk/OCCT_LGPL_EXCEPTION.txt" "$dest/licenses"
$n=0
Get-ChildItem deps/thirdparty,$env:QT_ROOT_DIR -Recurse -File | Where-Object {$_.Name -match '^(LICENSE|COPYING|COPYRIGHT)'} | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination "$dest/licenses/$n-$($_.Name)";$n++}
Invoke-WebRequest 'https://aka.ms/vs/17/release/vc_redist.x64.exe' -OutFile release/AxisForge-CAD/VC_redist.x64.exe
$signature=Get-AuthenticodeSignature release/AxisForge-CAD/VC_redist.x64.exe
if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Microsoft Corporation'){throw 'Invalid Microsoft runtime installer signature'}
Copy-Item README.md,LICENSE,start.cmd,test.cmd,RELEASE_NOTES.md release/AxisForge-CAD
$buildCommit = if(Test-Path .git){git rev-parse HEAD}else{'source-archive'}
[IO.File]::WriteAllText((Join-Path (Get-Location) 'release/AxisForge-CAD/BUILD_INFO.txt'),"AxisForge CAD $version`nBuild commit: $buildCommit`nQt SDK: $((Get-Item $env:QT_ROOT_DIR).Name)`n",[Text.UTF8Encoding]::new($false))
Copy-Item docs,examples release/AxisForge-CAD -Recurse
& "$dest/axisforge_tests.exe"
if($LASTEXITCODE){throw 'Packaged core tests failed'}
$windowsArchive="release/AxisForge-CAD-$version-Windows-x64.zip"
$sourceArchive="release/AxisForge-CAD-$version-Source.zip"
Compress-Archive release/AxisForge-CAD $windowsArchive -Force
if(Test-Path .git){
  git archive --format=zip "--prefix=AxisForge-CAD-$version/" "--output=$sourceArchive" HEAD
  if($LASTEXITCODE){throw 'Source archive failed'}
}else{
  $sourceFiles=Get-ChildItem . -Force | Where-Object {$_.Name -notin @('deps','build','run','release','.git','.vs')}
  Compress-Archive -Path $sourceFiles.FullName -DestinationPath $sourceArchive -Force
}
$checksums=foreach($file in @($windowsArchive,$sourceArchive)){(Get-FileHash $file -Algorithm SHA256).Hash.ToLower()+'  '+[IO.Path]::GetFileName($file)}
[IO.File]::WriteAllText((Join-Path (Get-Location) 'release/SHA256SUMS.txt'),($checksums -join "`n")+"`n",[Text.UTF8Encoding]::new($false))


