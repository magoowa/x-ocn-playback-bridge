
$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitOperatingSystem -or -not [Environment]::Is64BitProcess) { throw '64-bit Windows and PowerShell are required.' }

function Is-X64([string]$Path) {
    try {
        $stream = [IO.File]::OpenRead($Path)
        try {
            $reader = [IO.BinaryReader]::new($stream)
            if ($reader.ReadUInt16() -ne 0x5a4d) { return $false }
            $stream.Position = 0x3c; $offset = $reader.ReadUInt32()
            if ($offset -gt ($stream.Length - 6)) { return $false }
            $stream.Position = $offset
            return ($reader.ReadUInt32() -eq 0x4550 -and $reader.ReadUInt16() -eq 0x8664)
        } finally { $stream.Dispose() }
    } catch { return $false }
}

$uninstall = @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
    'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
    'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*')
$apps = @(Get-ItemProperty $uninstall -ErrorAction SilentlyContinue)
$roots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}, "$env:LOCALAPPDATA\Programs") | Where-Object { $_ }
$players = @()
foreach ($root in $roots) {
    $players += "$root\MPC-HC\mpc-hc64.exe", "$root\K-Lite Codec Pack\MPC-HC64\mpc-hc64.exe", "$root\MPC-HC\mpc-hc.exe"
}
foreach ($app in $apps | Where-Object { $_.DisplayName -match 'MPC-HC|Media Player Classic|K-Lite Codec Pack' }) {
    if ($app.InstallLocation) { $players += (Join-Path $app.InstallLocation 'mpc-hc64.exe'), (Join-Path $app.InstallLocation 'MPC-HC64\mpc-hc64.exe'), (Join-Path $app.InstallLocation 'mpc-hc.exe') }
    if ($app.DisplayIcon) { $players += ($app.DisplayIcon -replace ',\d+$','').Trim('"') }
}
foreach ($key in @('HKCU:\Software\Microsoft\Windows\CurrentVersion\App Paths\mpc-hc64.exe', 'HKLM:\Software\Microsoft\Windows\CurrentVersion\App Paths\mpc-hc64.exe')) {
    $item = Get-Item $key -ErrorAction SilentlyContinue
    if ($item) { $p = $item.GetValue(''); if ($p) { $players += $p } }
}
$player = $players | Select-Object -Unique | Where-Object { (Test-Path -LiteralPath $_ -PathType Leaf) -and (Is-X64 $_) } | Select-Object -First 1

$libraries = @()
foreach ($root in $roots) { $libraries += "$root\Blackmagic Design\DaVinci Resolve\SonyRawDev.dll" }
foreach ($app in $apps | Where-Object { $_.DisplayName -match '^DaVinci Resolve' -and $_.InstallLocation }) { $libraries += (Join-Path $app.InstallLocation 'SonyRawDev.dll') }
foreach ($root in $roots) { $libraries += "$root\Sony\RAW Viewer\SonyRawDev.dll", "$root\Sony\Catalyst Prepare V3\SonyRawDev.dll", "$root\Sony\Catalyst Prepare\SonyRawDev.dll", "$root\Sony\Catalyst Browse\SonyRawDev.dll" }
foreach ($app in $apps | Where-Object { $_.DisplayName -match 'RAW Viewer|Catalyst' -and $_.InstallLocation }) { $libraries += (Join-Path $app.InstallLocation 'SonyRawDev.dll') }
foreach ($root in $roots) {
    Get-ChildItem (Join-Path $root 'Adobe') -Directory -Filter 'Adobe Premiere Pro*' -ErrorAction SilentlyContinue | ForEach-Object { $libraries += (Join-Path $_.FullName 'SonyRawDev.dll') }
}
foreach ($app in $apps | Where-Object { $_.DisplayName -match 'Adobe Premiere Pro' -and $_.InstallLocation }) { $libraries += (Join-Path $app.InstallLocation 'SonyRawDev.dll') }
$mpvCandidates = @()
foreach ($root in $roots) { $mpvCandidates += "$root\mpv\mpv.exe", "$root\MPV\mpv.exe" }
$command = Get-Command mpv.exe -ErrorAction SilentlyContinue
if ($command) { $mpvCandidates += $command.Source }
$mpv = $mpvCandidates | Select-Object -Unique | Where-Object { (Test-Path -LiteralPath $_ -PathType Leaf) -and (Is-X64 $_) } | Select-Object -First 1
$library = $libraries | Select-Object -Unique | Where-Object {
    if (-not (Test-Path -LiteralPath $_ -PathType Leaf) -or -not (Is-X64 $_)) { return $false }
    $v = [Diagnostics.FileVersionInfo]::GetVersionInfo($_)
    return $v.FileMajorPart -ge 6
} | Select-Object -First 1


