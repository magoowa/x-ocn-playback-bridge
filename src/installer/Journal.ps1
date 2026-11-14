$ErrorActionPreference = 'Stop'

function Save-BridgeJournal($journal, [string]$path) {
    $tmp = "$path.tmp"
    $journal | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $tmp -Encoding UTF8
    Move-Item -LiteralPath $tmp -Destination $path -Force
}

function Test-BridgeValueEqual($left, $right) {
    if ($left -is [array] -or $right -is [array]) {
        return (($left | ConvertTo-Json -Compress) -eq ($right | ConvertTo-Json -Compress))
    }
    return $left -eq $right
}

function Set-BridgeRegistryValue($journal, [string]$journalPath, [string]$path, [string]$name, $value, [string]$kind = 'String') {
    $existing = $null
    $exists = $false
    $oldKind = $null
    if (Test-Path -LiteralPath $path) {
        $key = Get-Item -LiteralPath $path
        if ($key.GetValueNames() -contains $name) {
            $exists = $true
            $existing = $key.GetValue($name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
            $oldKind = $key.GetValueKind($name).ToString()
        }
    }
    $journal.Registry += [ordered]@{ Path=$path; Name=$name; Existed=$exists; OldValue=$existing; OldKind=$oldKind; InstalledValue=$value }
    Save-BridgeJournal $journal $journalPath
    if (-not (Test-Path -LiteralPath $path)) { New-Item -Path $path -Force | Out-Null }
    $key = Get-Item -LiteralPath $path
    $writable = $key.OpenSubKey('', $true)
    try { $writable.SetValue($name, $value, [Microsoft.Win32.RegistryValueKind]::$kind) } finally { $writable.Dispose() }
}

function Install-BridgeFile($journal, [string]$journalPath, [string]$source, [string]$destination, [string]$backupDir) {
    $existed = Test-Path -LiteralPath $destination -PathType Leaf
    $backup = $null
    if ($existed) {
        New-Item -ItemType Directory -Force $backupDir | Out-Null
        $backup = Join-Path $backupDir ("{0:D4}.bak" -f $journal.Files.Count)
        Copy-Item -LiteralPath $destination -Destination $backup -ErrorAction Stop
    }
    $installedHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    $journal.Files += [ordered]@{ Path=$destination; Existed=$existed; Backup=$backup; InstalledHash=$installedHash }
    Save-BridgeJournal $journal $journalPath
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Force
}

function Restore-BridgeJournal([string]$journalPath) {
    if (-not (Test-Path -LiteralPath $journalPath)) { throw 'No installation backup was found.' }
    $journal = Get-Content -LiteralPath $journalPath -Raw | ConvertFrom-Json
    $conflicts = @()
    for ($i = $journal.Registry.Count - 1; $i -ge 0; $i--) {
        $record = $journal.Registry[$i]
        if ($record.Restored) { continue }
        if (-not (Test-Path -LiteralPath $record.Path)) { $conflicts += $record.Path; continue }
        $key = Get-Item -LiteralPath $record.Path
        if (($key.GetValueNames() -notcontains $record.Name) -or -not (Test-BridgeValueEqual ($key.GetValue($record.Name)) $record.InstalledValue)) {
            $conflicts += "$($record.Path) [$($record.Name)]"; continue
        }
        $writable = $key.OpenSubKey('', $true)
        if ($record.Existed) {
            $old = $record.OldValue
            if ($record.OldKind -eq 'Binary') { $old = [byte[]]$old }
            if ($record.OldKind -eq 'MultiString') { $old = [string[]]$old }
            $writable.SetValue($record.Name, $old, [Microsoft.Win32.RegistryValueKind]::$($record.OldKind))
        } else { $writable.DeleteValue($record.Name, $false) }
        $writable.Dispose()
        $record | Add-Member -NotePropertyName Restored -NotePropertyValue $true -Force
        Save-BridgeJournal $journal $journalPath
    }
    for ($i = $journal.Files.Count - 1; $i -ge 0; $i--) {
        $record = $journal.Files[$i]
        if ($record.Restored) { continue }
        if (-not (Test-Path -LiteralPath $record.Path -PathType Leaf) -or (Get-FileHash -LiteralPath $record.Path).Hash -ne $record.InstalledHash) {
            $conflicts += $record.Path; continue
        }
        if ($record.Existed) { Copy-Item -LiteralPath $record.Backup -Destination $record.Path -Force }
        else { Remove-Item -LiteralPath $record.Path }
        $record | Add-Member -NotePropertyName Restored -NotePropertyValue $true -Force
        Save-BridgeJournal $journal $journalPath
    }
    if ($conflicts.Count) { throw "Some items changed since installation and were left untouched:`n$($conflicts -join "`n")" }
    Remove-Item -LiteralPath $journalPath
}
