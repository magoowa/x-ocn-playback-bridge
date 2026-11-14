function Get-BridgeInstalledState([string]$mpvPath) {
    $sourceId='{927EB97C-DA96-4AF7-9D70-F31432674862}'
    $thumbId='{B94A5D91-3BE4-4C81-9934-71C64CD03677}'
    $thumbnailCategory='{e357fccd-a995-4576-b01f-234630154e96}'
    $source=$null
    foreach($root in @('HKCU:\Software\Classes','HKLM:\Software\Classes')) {
        $key=Get-Item "$root\CLSID\$sourceId\InprocServer32" -ErrorAction SilentlyContinue
        if($key) { $source=$key.GetValue(''); break }
    }
    $sourceOk = [bool]($source -and (Test-Path -LiteralPath $source -PathType Leaf))
    $mpc=$false
    if($sourceOk) {
        $slots=Get-ChildItem 'HKCU:\Software\MPC-HC\MPC-HC\Filters\x64' -ErrorAction SilentlyContinue
        foreach($slot in $slots) {
            $entry=Get-ItemProperty $slot.PSPath -ErrorAction SilentlyContinue
            if($entry.CLSID -eq $sourceId -and $entry.Enabled -eq 1 -and $entry.Path -and (Test-Path -LiteralPath $entry.Path -PathType Leaf)) { $mpc=$true; break }
        }
    }
    $scripts=Join-Path $env:APPDATA 'mpv\scripts\xocn'
    $script=Join-Path $scripts 'main.lua'
    $mpv=$false
    if($sourceOk -and $mpvPath -and (Test-Path -LiteralPath $mpvPath -PathType Leaf) -and (Test-Path -LiteralPath $script -PathType Leaf)) {
        $mpv=(Select-String -LiteralPath $script -SimpleMatch 'Using X-OCN DirectShow bridge' -Quiet) -and
            (Test-Path -LiteralPath (Join-Path $scripts 'DirectShowSource.dll') -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $scripts 'xocn-mpv-probe.exe') -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path (Split-Path -Parent $mpvPath) 'AviSynth.dll') -PathType Leaf)
    }
    $handler=$null
    foreach($root in @('HKCU:\Software\Classes','HKLM:\Software\Classes')) {
        $key=Get-Item "$root\.mxf\shellex\$thumbnailCategory" -ErrorAction SilentlyContinue
        if($key) { $handler=$key.GetValue(''); break }
    }
    $thumbDll=$null
    foreach($root in @('HKCU:\Software\Classes','HKLM:\Software\Classes')) {
        $key=Get-Item "$root\CLSID\$thumbId\InprocServer32" -ErrorAction SilentlyContinue
        if($key) { $thumbDll=$key.GetValue(''); break }
    }
    $thumbnails=[bool]($handler -eq $thumbId -and $thumbDll -and (Test-Path -LiteralPath $thumbDll -PathType Leaf))
    return [pscustomobject]@{Mpc=[bool]$mpc; Mpv=[bool]$mpv; Thumbnails=$thumbnails}
}
