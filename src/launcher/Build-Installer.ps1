$ErrorActionPreference = 'Stop'
$source = (Resolve-Path (Join-Path $PSScriptRoot '..\installer')).Path
$workspace = Join-Path $env:TEMP ('XOCN-build-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $workspace | Out-Null
try {
    $zip = Join-Path $workspace 'payload.zip'
    Compress-Archive -Path (Join-Path $source '*') -DestinationPath $zip
    $csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
    $exe = Join-Path $PSScriptRoot '..\..\installer\X-OCN-Playback-Bridge-Setup-1.13.2026.1114.exe'
    & $csc /nologo /target:winexe /platform:x64 /optimize+ /reference:System.Windows.Forms.dll /reference:System.IO.Compression.dll /reference:System.IO.Compression.FileSystem.dll "/resource:$zip,payload.zip" "/win32icon:$PSScriptRoot\icon-xocn.ico" "/win32manifest:$PSScriptRoot\app.manifest" "/out:$exe" "$PSScriptRoot\Launcher.cs"
    if ($LASTEXITCODE -ne 0) { throw 'Compiler failed.' }
} finally { Remove-Item -LiteralPath $workspace -Recurse -Force -ErrorAction SilentlyContinue }
