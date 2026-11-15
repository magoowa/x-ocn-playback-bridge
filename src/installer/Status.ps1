$mpcRendererSettings = 'HKCU:\Software\MPC-HC\MPC-HC\Settings'
$mpcVrSettings = 'HKCU:\Software\MPC-BE Filters\MPC Video Renderer'
$mpcVrRendererId = 14

function Get-BridgeMpcRenderer([string]$playerPath) {
    if (-not $playerPath) { return $null }
    $ini = [IO.Path]::ChangeExtension($playerPath, '.ini')
    $portable = Test-Path -LiteralPath $ini -PathType Leaf
    if ($portable) {
        $content = Get-Content -LiteralPath $ini -Raw -ErrorAction SilentlyContinue
        $section = [regex]::Match([string]$content, '(?im)^\[Settings\][^\S\r\n]*\r?\n(?:^(?![ \t]*\[)[^\r\n]*(?:\r?\n|$))*').Value
        $selection = [regex]::Match($section, '(?im)^DSVidRen[ \t]*=[ \t]*(\d+)').Groups[1].Value
    } else {
        $selection = Get-ItemPropertyValue -LiteralPath $mpcRendererSettings -Name DSVidRen -ErrorAction SilentlyContinue
    }

    $bundled = Join-Path (Split-Path -Parent $playerPath) 'MPCVR\MpcVideoRenderer64.ax'
    $registered = Get-Item 'Registry::HKEY_CLASSES_ROOT\CLSID\{71F080AA-8661-4093-B15E-4F6903E77D0A}' -ErrorAction SilentlyContinue
    $registeredPath = if ($registered) {
        $server = Get-Item -LiteralPath ($registered.PSPath + '\InprocServer32') -ErrorAction SilentlyContinue
        if ($server) { [Environment]::ExpandEnvironmentVariables([string]$server.GetValue('')) }
    }
    $available = ((Test-Path -LiteralPath $bundled -PathType Leaf) -and (Is-X64 $bundled)) -or
        ($registeredPath -and (Test-Path -LiteralPath $registeredPath -PathType Leaf) -and (Is-X64 $registeredPath))
    $d3d11 = Get-ItemPropertyValue -LiteralPath $mpcVrSettings -Name UseD3D11 -ErrorAction SilentlyContinue
    return [pscustomobject]@{
        Portable = $portable
        Available = $available
        Selected = $selection -eq $mpcVrRendererId
        Direct3D11Disabled = $null -ne $d3d11 -and $d3d11 -eq 0
    }
}

function Get-BridgeInstalledState([string]$mpvPath) {
    $sourceId='{927EB97C-DA96-4AF7-9D70-F31432674862}'
    $thumbId='{B94A5D91-3BE4-4C81-9934-71C64CD03677}'
    $thumbnailCategory='{e357fccd-a995-4576-b01f-234630154e96}'
    $source=$null
    $sourceScope=$null
    foreach($root in @('HKCU:\Software\Classes','HKLM:\Software\Classes')) {
        $key=Get-Item "$root\CLSID\$sourceId\InprocServer32" -ErrorAction SilentlyContinue
        if($key) { $source=$key.GetValue(''); $sourceScope=if($root.StartsWith('HKCU')){'Current user'}else{'All users'}; break }
    }
    $sourceOk = [bool]($source -and (Test-Path -LiteralPath $source -PathType Leaf))
    $mpc=$sourceOk -and $sourceScope -eq "All users"
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
    $version = if($sourceOk){$info=[Diagnostics.FileVersionInfo]::GetVersionInfo($source); if($info.ProductVersion){$info.ProductVersion}else{$info.FileVersion}}
    return [pscustomobject]@{SourcePath=$source; SourceVersion=$version; SourceScope=$sourceScope; Mpc=[bool]$mpc; Mpv=[bool]$mpv; Thumbnails=$thumbnails}
}
