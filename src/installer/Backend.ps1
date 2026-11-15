. "$PSScriptRoot\Journal.ps1"

function Install-Bridge([bool]$useMpc, [bool]$useMpv, [bool]$useThumbnails, [string]$sony, [string]$mpvPath, [bool]$allUsers = $false, [string]$configureMpcRendererFor) {
    if (-not ($useMpc -or $useMpv -or $useThumbnails)) { throw 'Select at least one component.' }
    if (-not $sony -or -not (Test-Path -LiteralPath $sony) -or [Diagnostics.FileVersionInfo]::GetVersionInfo($sony).FileMajorPart -lt 6 -or -not (Is-X64 $sony)) { throw 'SonyRawDev x64 6.0 or newer is required.' }
    if ($useMpv -and -not (Test-Path -LiteralPath $mpvPath)) { throw 'mpv x64 was not found.' }
    if (Get-Process mpc-hc,mpc-hc64,mpv -ErrorAction SilentlyContinue) { throw 'Close MPC-HC and mpv before installing.' }
    if ($configureMpcRendererFor) {
        $renderer = Get-BridgeMpcRenderer $configureMpcRendererFor
        if (-not $useMpc -or $renderer.Portable -or -not $renderer.Available) {
            throw 'Automatic renderer setup requires a registry-based MPC-HC installation and MPC Video Renderer x64.'
        }
    }
    if ($useThumbnails) {
        foreach ($handler in @('HKLM:\Software\Classes\.mxf\shellex\{e357fccd-a995-4576-b01f-234630154e96}','HKCU:\Software\Classes\.mxf\shellex\{e357fccd-a995-4576-b01f-234630154e96}')) {
            if (Test-Path -LiteralPath $handler) {
                $existing = (Get-Item -LiteralPath $handler).GetValue('')
                if ($existing -and $existing -notin @('{c5aec3ec-e812-4677-a9a7-4fee1f9aa000}','{B94A5D91-3BE4-4C81-9934-71C64CD03677}')) { throw "An unsupported MXF thumbnail provider is already installed: $existing" }
            }
        }
    }
    if ($allUsers -and -not [IO.Path]::GetFullPath($sony).StartsWith(
        "$env:ProgramFiles\", [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Select SonyRawDev from an installation under Program Files for All users.'
    }
    if ($allUsers -and $useMpv) { throw 'mpv playback requires Current user scope.' }
    $installDir = if ($allUsers) { Join-Path $env:ProgramFiles 'XOCN-Playback-Bridge' } else { Join-Path $env:LOCALAPPDATA 'Programs\XOCN-Playback-Bridge' }
    $stateDir = if ($allUsers) { Join-Path $installDir 'install-state' } else { Join-Path $env:LOCALAPPDATA 'XOCN-Playback-Bridge' }
    $journalPath = Join-Path $stateDir 'installation.json'
    $previous = $null
    if (Test-Path -LiteralPath $journalPath) {
        $previous = Get-Content -LiteralPath $journalPath -Raw | ConvertFrom-Json
        $journalPath = Join-Path $stateDir 'upgrade.json'
        if (Test-Path -LiteralPath $journalPath) { throw 'An interrupted upgrade needs recovery before retrying.' }
    }
    New-Item -ItemType Directory -Force $stateDir | Out-Null
    $journal = [ordered]@{ Version=(Get-Content -LiteralPath (Join-Path $PSScriptRoot 'manifest.json') -Raw | ConvertFrom-Json).version; Registry=@(); Files=@() }
    Save-BridgeJournal $journal $journalPath
    $backupDir = Join-Path $stateDir ('backup-' + [guid]::NewGuid().ToString('N'))
    $payload = Join-Path $PSScriptRoot 'payload'
    $id='{927EB97C-DA96-4AF7-9D70-F31432674862}'
    $page='{63E54D1A-9352-42BF-A3BB-80A6806A9412}'
    $thumb='{B94A5D91-3BE4-4C81-9934-71C64CD03677}'
    $userRoot = 'HKCU:\Software\Classes'
    $root = if ($allUsers) { 'HKLM:\Software\Classes' } else { $userRoot }
    try {
        Set-BridgeRegistryValue $journal $journalPath 'HKCU:\Software\XOCN-MPC' 'SonyRawDevPath' $sony
        if ($allUsers) {
            Set-BridgeRegistryValue $journal $journalPath 'HKLM:\Software\XOCN-MPC' 'SonyRawDevPath' $sony
        }
        if ($useMpc -or $useMpv) {
            $ax=Join-Path $installDir 'XocnSource.ax'
            Install-BridgeFile $journal $journalPath (Join-Path $payload 'XocnSource.ax') $ax $backupDir
            foreach ($entry in @(@($id,'X-OCN Playback Bridge','Both'),@($page,'X-OCN Settings','Apartment'))) {
                $class="$root\CLSID\$($entry[0])"
                Set-BridgeRegistryValue $journal $journalPath $class '' $entry[1]
                Set-BridgeRegistryValue $journal $journalPath "$class\InprocServer32" '' $ax
                Set-BridgeRegistryValue $journal $journalPath "$class\InprocServer32" 'ThreadingModel' $entry[2]
            }
            if ($allUsers) {
                foreach ($classId in $id,$page) {
                    $server = "$userRoot\CLSID\$classId\InprocServer32"
                    if (Test-Path -LiteralPath $server) {
                        Set-BridgeRegistryValue $journal $journalPath $server '' $ax
                    }
                }
            }
            $instance="$root\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance\$id"
            Set-BridgeRegistryValue $journal $journalPath $instance 'CLSID' $id
            Set-BridgeRegistryValue $journal $journalPath $instance 'FriendlyName' 'X-OCN Playback Bridge'
            Set-BridgeRegistryValue $journal $journalPath $instance 'FilterData' ([byte[]](2,0,0,0,0,0,128,0,0,0,0,0,0,0,0,0)) 'Binary'
            $signature="$root\Media Type\{E436EB83-524F-11CE-9F53-0020AF0BA770}\$id"
            Set-BridgeRegistryValue $journal $journalPath $signature 'Source Filter' $id
            Set-BridgeRegistryValue $journal $journalPath $signature '0' '0,13,,060E2B34020501010D01020101'
        }
        if ($useMpc) {
            if ($configureMpcRendererFor) {
                if (-not $renderer.Selected) {
                    Set-BridgeRegistryValue $journal $journalPath $mpcRendererSettings 'DSVidRen' $mpcVrRendererId 'DWord'
                }
                if ($renderer.Direct3D11Disabled) {
                    Set-BridgeRegistryValue $journal $journalPath $mpcVrSettings 'UseD3D11' 1 'DWord'
                }
            }
            $filterRoot='HKCU:\Software\MPC-HC\MPC-HC\Filters\x64'
            $slots = @(Get-ChildItem $filterRoot -ErrorAction SilentlyContinue | Where-Object { (Get-ItemProperty $_.PSPath -ErrorAction SilentlyContinue).CLSID -eq $id })
            $slot = $slots | Select-Object -First 1
            foreach ($duplicate in ($slots | Select-Object -Skip 1)) {
                Set-BridgeRegistryValue $journal $journalPath $duplicate.PSPath Path $ax
                Set-BridgeRegistryValue $journal $journalPath $duplicate.PSPath Enabled 0 DWord
            }
            if ($slot) { $slotPath=$slot.PSPath } else { $i=0; while (Test-Path "$filterRoot\$('{0:D4}' -f $i)") { $i++ }; $slotPath="$filterRoot\$('{0:D4}' -f $i)" }
            foreach ($pair in @(@('LoadType',0),@('Merit',8388608),@('Enabled',1),@('SourceType',1),@('Path',$ax),@('Name','X-OCN Playback Bridge'),@('CLSID',$id))) {
                $type=if($pair[1] -is [int]){'DWord'}else{'String'}
                Set-BridgeRegistryValue $journal $journalPath $slotPath $pair[0] $pair[1] $type
            }
        }
        if ($useMpv) {
            $scriptDir=Join-Path $env:APPDATA 'mpv\scripts\xocn'
            Install-BridgeFile $journal $journalPath (Join-Path $payload 'xocn.lua') (Join-Path $scriptDir 'main.lua') $backupDir
            foreach ($name in @('DirectShowSource.dll','xocn-mpv-probe.exe')) { Install-BridgeFile $journal $journalPath (Join-Path $payload $name) (Join-Path $scriptDir $name) $backupDir }
            Install-BridgeFile $journal $journalPath (Join-Path $payload 'AviSynth.dll') (Join-Path (Split-Path -Parent $mpvPath) 'AviSynth.dll') $backupDir
        }
        if ($useThumbnails) {
            $dll=Join-Path $installDir 'XocnThumbnail-v1.3.2026.1114.dll'
            Install-BridgeFile $journal $journalPath (Join-Path $payload 'XocnThumbnail.dll') $dll $backupDir
            $class="HKLM:\Software\Classes\CLSID\$thumb"
            Set-BridgeRegistryValue $journal $journalPath $class '' 'Sony X-OCN Thumbnail Provider'
            Set-BridgeRegistryValue $journal $journalPath "$class\InprocServer32" '' $dll
            Set-BridgeRegistryValue $journal $journalPath "$class\InprocServer32" 'ThreadingModel' 'Apartment'
            $handler='HKLM:\Software\Classes\.mxf\shellex\{e357fccd-a995-4576-b01f-234630154e96}'
            Set-BridgeRegistryValue $journal $journalPath $handler '' $thumb
            $userHandler='HKCU:\Software\Classes\.mxf\shellex\{e357fccd-a995-4576-b01f-234630154e96}'
            Set-BridgeRegistryValue $journal $journalPath $userHandler '' $thumb
        }
        if ($previous) {
            $merged = [ordered]@{ Version=$journal.Version; Registry=@(); Files=@() }
            foreach ($category in 'Registry','Files') {
                $records = [ordered]@{}
                foreach ($record in $previous.$category) {
                    if ($record.Restored) { continue }
                    $records["$($record.Path)|$($record.Name)"] = $record
                }
                foreach ($record in $journal[$category]) {
                    $key = "$($record.Path)|$($record.Name)"
                    if ($records.Contains($key)) {
                        if ($category -eq 'Registry') { $records[$key].InstalledValue = $record.InstalledValue }
                        else { $records[$key].InstalledHash = $record.InstalledHash }
                    } else { $records[$key] = $record }
                }
                $merged[$category] = @($records.Values)
            }
            Save-BridgeJournal $merged (Join-Path $stateDir 'installation.json')
            Remove-Item -LiteralPath $journalPath -ErrorAction SilentlyContinue
        }
    } catch {
        $message=$_.Exception.Message
        try { Restore-BridgeJournal $journalPath } catch { $message += "`nRollback requires attention: $($_.Exception.Message)" }
        throw $message
    }
}
