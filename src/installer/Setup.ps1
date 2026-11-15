Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
. "$PSScriptRoot\Detect.ps1"
. "$PSScriptRoot\Backend.ps1"
. "$PSScriptRoot\Status.ps1"
$form = New-Object Windows.Forms.Form
$form.SuspendLayout()
$form.AutoScaleMode = [Windows.Forms.AutoScaleMode]::None
$form.Text='X-OCN Playback Bridge — Setup'
if ($env:XOCN_SETUP_EXECUTABLE -and (Test-Path -LiteralPath $env:XOCN_SETUP_EXECUTABLE)) {
    $form.Icon=[Drawing.Icon]::ExtractAssociatedIcon($env:XOCN_SETUP_EXECUTABLE)
}
$form.ClientSize=New-Object Drawing.Size(780,930)
$form.StartPosition='CenterScreen'
$form.BackColor=[Drawing.Color]::FromArgb(12,18,29)
$form.ForeColor=[Drawing.Color]::White
$form.FormBorderStyle='FixedDialog'; $form.MaximizeBox=$false
$font=New-Object Drawing.Font('Segoe UI',10)
$form.Font=$font
function Label([string]$text,[int]$x,[int]$y,[int]$w,[int]$h) {
    $l=New-Object Windows.Forms.Label; $l.Text=$text; $l.SetBounds($x,$y,$w,$h); $l.ForeColor=[Drawing.Color]::White; $form.Controls.Add($l); return $l
}
function Button([string]$text,[int]$x,[int]$y,[int]$w) {
    $b=New-Object Windows.Forms.Button; $b.Text=$text; $b.SetBounds($x,$y,$w,42); $b.FlatStyle='Flat'; $b.BackColor=[Drawing.Color]::FromArgb(38,59,80); $b.ForeColor=[Drawing.Color]::White; $form.Controls.Add($b); return $b
}
$title=Label 'X-OCN Playback Bridge' 36 21 700 68
$title.Font=New-Object Drawing.Font('Segoe UI',28)
$t=Label 'DETECTED SOFTWARE' 36 105 700 25; $t.ForeColor=[Drawing.Color]::FromArgb(115,199,236)
$mpcVersion=if($player){[Diagnostics.FileVersionInfo]::GetVersionInfo($player).FileVersion}else{'Not found'}
$mpvVersion=if($mpv){[Diagnostics.FileVersionInfo]::GetVersionInfo($mpv).FileVersion}else{'Not found'}
$sonyVersion=if($library){[Diagnostics.FileVersionInfo]::GetVersionInfo($library).FileVersion}else{'Not found (6.0+ required)'}
$mpcLabel=Label "MPC-HC:  $mpcVersion`n$player" 36 135 700 50
$mpvLabel=Label "mpv:  $mpvVersion`n$mpv" 36 188 700 50
$null=Label "Sony RAW Development Library (SonyRawDev 6.0+)" 36 241 700 22
$detail=Label 'Decodes Sony RAW / X-OCN frames. Required for video playback and thumbnails.' 36 267 700 25
$detail.ForeColor=[Drawing.Color]::FromArgb(164,179,194)
$sonyBox=New-Object Windows.Forms.ComboBox
$sonyBox.DropDownStyle='DropDownList'; $sonyBox.SetBounds(36,296,700,28)
$sonyPaths=@($libraries | Select-Object -Unique | Where-Object {Test-Path -LiteralPath $_ -PathType Leaf})
$selected=0
for($i=0;$i -lt $sonyPaths.Count;$i++) {
    $p=$sonyPaths[$i]; $v=[Diagnostics.FileVersionInfo]::GetVersionInfo($p)
    $suffix=if($v.FileMajorPart -lt 6 -or -not (Is-X64 $p)){' (not supported)'}else{''}
    [void]$sonyBox.Items.Add("$($v.FileVersion) — $p$suffix")
    if($p -eq $library){$selected=$i}
}
if($sonyBox.Items.Count){$sonyBox.SelectedIndex=$selected}
$form.Controls.Add($sonyBox)
$a=Label "System OpenCL (x64): $(Get-BridgeOpenClStatus)" 36 330 700 30
$a.ForeColor=[Drawing.Color]::FromArgb(164,179,194)
function Check([string]$text,[int]$y,[bool]$checked) {
    $c=New-Object Windows.Forms.CheckBox; $c.Text=$text; $c.SetBounds(39,$y,690,29); $c.Checked=$checked; $form.Controls.Add($c); return $c
}
$rendererLabel=Label '' 36 361 700 44
$filterLabel=Label '' 36 441 700 49
$scope=New-Object Windows.Forms.ComboBox
$scope.DropDownStyle='DropDownList'
$scope.SetBounds(36,496,260,28)
[void]$scope.Items.Add('Current user')
[void]$scope.Items.Add('All users (MPC-HC and thumbnails)')
$scope.SelectedIndex=0
$form.Controls.Add($scope)
$currentState=Get-BridgeInstalledState $mpv
$mpcBox=Check 'MPC-HC playback' 540 $currentState.Mpc
$mpvBox=Check 'mpv playback' 576 $currentState.Mpv
$scope.Add_SelectedIndexChanged({ $mpvBox.Enabled=$scope.SelectedIndex -eq 0 })
$thumbBox=Check 'Windows Explorer thumbnails' 612 $currentState.Thumbnails
$rendererBox=Check 'Set MPC Video Renderer with Direct3D 11 for the current user' 408 $false
function Update-RendererState {
    $renderer = Get-BridgeMpcRenderer $player
    $rendererBox.Enabled = [bool]($mpcBox.Checked -and $renderer.Available -and -not $renderer.Portable)
    if (-not $renderer) {
        $rendererLabel.Text = 'For GPU playback, install MPC-HC x64 with MPC Video Renderer.'
    } elseif (-not $renderer.Available) {
        $rendererLabel.Text = 'MPC Video Renderer x64 not found. Install it, then select it under MPC-HC Options > Playback > Output.'
    } elseif ($renderer.Selected -and -not $renderer.Direct3D11Disabled) {
        $rendererLabel.Text = 'Video output: MPC Video Renderer selected. GPU playback uses Direct3D 11.'
    } else {
        $rendererLabel.Text = 'For GPU playback select MPC Video Renderer in MPC-HC Options > Playback > Output and enable Use Direct3D 11 in its settings.'
    }
}
$mpcBox.Add_CheckedChanged({ Update-RendererState })
Update-RendererState
function Update-BridgeState {
    $state=Get-BridgeInstalledState $mpv
    if ($state.Mpc -ne $script:currentState.Mpc) { $mpcBox.Checked=$state.Mpc }
    if ($state.Mpv -ne $script:currentState.Mpv) { $mpvBox.Checked=$state.Mpv }
    if ($state.Thumbnails -ne $script:currentState.Thumbnails) { $thumbBox.Checked=$state.Thumbnails }
    $filterLabel.Text=if($state.SourcePath) {
        $version=if($state.SourceVersion){$state.SourceVersion}else{'File missing or version unavailable'}
        "X-OCN filter: $version ($($state.SourceScope))`n$($state.SourcePath)"
    } else { 'X-OCN filter: Not registered' }
    $mpcBox.Text='MPC-HC playback' + $(if($state.Mpc){'  •  Installed'}else{'  •  Not installed'})
    $mpvBox.Text='mpv playback' + $(if($state.Mpv){'  •  Installed'}else{'  •  Not installed'})
    $thumbBox.Text='Windows Explorer thumbnails' + $(if($state.Thumbnails){'  •  Installed'}else{'  •  Not installed'})
    $script:currentState=$state
}
Update-BridgeState
$timer=New-Object Windows.Forms.Timer
$timer.Interval=3000
$timer.Add_Tick({ Update-BridgeState })
$timer.Start()
$install=Button 'Install selected' 36 671 200
$restore=Button 'Restore previous state' 251 671 210
$uninstall=Button 'Uninstall' 476 671 155
$status=Label 'Restore and uninstall return replaced files and settings to their saved state.' 36 726 700 44
$status.ForeColor=[Drawing.Color]::FromArgb(164,179,194)
$notice=Label 'No SonyRawDev? Install one of these Sony applications:' 36 774 700 24
$notice.ForeColor=[Drawing.Color]::FromArgb(164,179,194)
function DownloadLink([string]$text,[string]$url,[int]$x,[int]$width) {
    $link=New-Object Windows.Forms.LinkLabel
    $link.Text=$text; $link.SetBounds($x,802,$width,25)
    $link.LinkColor=[Drawing.Color]::FromArgb(115,199,236)
    $link.ActiveLinkColor=[Drawing.Color]::White
    $link.Tag=$url
    $link.Add_LinkClicked({ param($sender,$eventArgs)
        try { $open=New-Object Diagnostics.ProcessStartInfo; $open.FileName=$sender.Tag; $open.UseShellExecute=$true; [void][Diagnostics.Process]::Start($open) }
        catch { [Windows.Forms.MessageBox]::Show($_.Exception.Message,'Could not open link','OK','Error') | Out-Null }
    })
    $form.Controls.Add($link)
}
DownloadLink 'RAW Viewer' 'https://www.sony.com/electronics/support/software/00408235' 36 115
$null=Label 'or' 155 802 30 25
DownloadLink 'Catalyst Prepare' 'https://support.d-imaging.sony.co.jp/disoft_DL/CatalystPrepareV3_DL/win?fm=en' 187 180
$setupVersion = (Get-Content -LiteralPath "$PSScriptRoot\manifest.json" -Raw | ConvertFrom-Json).version
$footer=Label "$setupVersion  |  Authored by magoowa  |  fx5-xocn-tools, Microsoft and AviSynth+ contributors" 36 864 710 45
$footer.ForeColor=[Drawing.Color]::FromArgb(164,179,194); $footer.Font=New-Object Drawing.Font('Segoe UI',8)
function Refresh-SoftwareDetection {
    $previous=if($sonyBox.SelectedIndex -ge 0){$script:sonyPaths[$sonyBox.SelectedIndex]}else{$null}
    . "$PSScriptRoot\Detect.ps1"
    $script:player=$player; $script:mpv=$mpv; $script:library=$library
    $mpcVersion=if($player){[Diagnostics.FileVersionInfo]::GetVersionInfo($player).FileVersion}else{'Not found'}
    $mpvVersion=if($mpv){[Diagnostics.FileVersionInfo]::GetVersionInfo($mpv).FileVersion}else{'Not found'}
    $mpcLabel.Text="MPC-HC:  $mpcVersion`n$player"
    $mpvLabel.Text="mpv:  $mpvVersion`n$mpv"
    Update-RendererState
    $paths=@($libraries | Select-Object -Unique | Where-Object {Test-Path -LiteralPath $_ -PathType Leaf})
    $script:sonyPaths=$paths
    $sonyBox.BeginUpdate(); $sonyBox.Items.Clear(); $selection=-1
    for($i=0;$i -lt $paths.Count;$i++) {
        $p=$paths[$i]; $v=[Diagnostics.FileVersionInfo]::GetVersionInfo($p)
        $suffix=if($v.FileMajorPart -lt 6 -or -not (Is-X64 $p)){' (not supported)'}else{''}
        [void]$sonyBox.Items.Add("$($v.FileVersion) — $p$suffix")
        if($p -eq $previous){$selection=$i}
    }
    if($selection -lt 0 -and $library){$selection=[array]::IndexOf($paths,$library)}
    if($selection -ge 0){$sonyBox.SelectedIndex=$selection}
    $sonyBox.EndUpdate()
    Update-BridgeState
}
$form.Add_Activated({ Refresh-SoftwareDetection })
$journalPath=Join-Path $env:LOCALAPPDATA 'XOCN-Playback-Bridge\installation.json'
$install.Add_Click({
    try {
        $install.Enabled=$false; $form.Cursor=[Windows.Forms.Cursors]::WaitCursor
        if ($mpcBox.Checked -and -not $player) { throw 'MPC-HC x64 was not found.' }
        $chosenSony=if($sonyBox.SelectedIndex -ge 0){$sonyPaths[$sonyBox.SelectedIndex]}else{$null}
        Install-Bridge $mpcBox.Checked ($mpvBox.Enabled -and $mpvBox.Checked) $thumbBox.Checked $chosenSony $mpv ($scope.SelectedIndex -eq 1) $(if($rendererBox.Enabled -and $rendererBox.Checked){$player})
        Update-RendererState
        [Windows.Forms.MessageBox]::Show('Installation complete. Restart MPC-HC or mpv.','X-OCN Playback Bridge') | Out-Null
    } catch { [Windows.Forms.MessageBox]::Show($_.Exception.Message,'Installation failed','OK','Error') | Out-Null }
    finally { $install.Enabled=$true; $form.Cursor=[Windows.Forms.Cursors]::Default }
})
$doRestore={
    try {
        if(Get-Process mpc-hc,mpc-hc64,mpv -ErrorAction SilentlyContinue){throw 'Close MPC-HC and mpv first.'}
        $restorePath=if($scope.SelectedIndex -eq 1){Join-Path $env:ProgramFiles 'XOCN-Playback-Bridge\install-state\installation.json'}else{$journalPath}
        Restore-BridgeJournal $restorePath
        [Windows.Forms.MessageBox]::Show('Previous state restored.','X-OCN Playback Bridge') | Out-Null
    } catch { [Windows.Forms.MessageBox]::Show($_.Exception.Message,'Restore failed','OK','Error') | Out-Null }
}
$restore.Add_Click($doRestore); $uninstall.Add_Click($doRestore)
# Apply the same scale once to every control after the form has a monitor.
$form.Add_Load({
    $dpi = [SetupDisplay]::GetDpiForWindow($form.Handle)
    $scale = if ($dpi) { $dpi / 96.0 } else { 1.0 }
    foreach ($control in $form.Controls) {
        $control.SetBounds([int]($control.Left*$scale), [int]($control.Top*$scale),
            [int]($control.Width*$scale), [int]($control.Height*$scale))
    }
    $area = [Windows.Forms.Screen]::FromHandle($form.Handle).WorkingArea
    $form.ClientSize = New-Object Drawing.Size(
        [Math]::Min([int](780*$scale),$area.Width-32),
        [Math]::Min([int](930*$scale),$area.Height-72))
    $form.AutoScroll = $form.ClientSize.Width -lt [int](780*$scale) -or
                       $form.ClientSize.Height -lt [int](930*$scale)
})
$form.ResumeLayout($true)
$form.Add_FormClosing({ $timer.Stop() })
try { [void]$form.ShowDialog() }
finally { $timer.Dispose(); $form.Dispose() }
