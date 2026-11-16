$ErrorActionPreference = 'Stop'
$version = '7.9.3'
$sdkArchive = 'deps/occt-sdk.zip'
$thirdArchive = 'deps/occt-thirdparty.zip'
New-Item -ItemType Directory deps/occt,deps/thirdparty -Force | Out-Null
if (-not (Get-ChildItem deps/occt -Filter OpenCASCADEConfig.cmake -Recurse | Select-Object -First 1)) {
    Invoke-WebRequest "https://github.com/Open-Cascade-SAS/OCCT/releases/download/V7_9_3/opencascade-$version-vc14-64.zip" -OutFile $sdkArchive
    if ((Get-FileHash $sdkArchive -Algorithm SHA256).Hash.ToLower() -ne '2a4f5e187992427674869e89477882d1ace4d67695d4b27173830d93f8cf11be') { throw 'OCCT SDK integrity mismatch' }
    Expand-Archive $sdkArchive deps/occt -Force
}
if (-not (Get-ChildItem deps/thirdparty -Filter *.dll -Recurse | Select-Object -First 1)) {
    Invoke-WebRequest 'https://github.com/Open-Cascade-SAS/OCCT/releases/download/V7_9_3/3rdparty-vc14-64.zip' -OutFile $thirdArchive
    Expand-Archive $thirdArchive deps/thirdparty -Force
}
Write-Output 'OCCT SDK and third-party libraries are ready under deps/.'
