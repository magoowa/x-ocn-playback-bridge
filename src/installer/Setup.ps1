Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class BridgeDpi {
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
}
'@
try { [void][BridgeDpi]::SetProcessDpiAwarenessContext([IntPtr](-4)) } catch { }
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
. "$PSScriptRoot\Detect.ps1"
. "$PSScriptRoot\Backend.ps1"
. "$PSScriptRoot\Status.ps1"
$form = New-Object Windows.Forms.Form
$form.AutoScaleMode=[Windows.Forms.AutoScaleMode]::None
$form.Text='X-OCN Playback Bridge — Setup'
if ($env:XOCN_SETUP_EXECUTABLE -and (Test-Path -LiteralPath $env:XOCN_SETUP_EXECUTABLE)) {
    $form.Icon=[Drawing.Icon]::ExtractAssociatedIcon($env:XOCN_SETUP_EXECUTABLE)
}
$form.ClientSize=New-Object Drawing.Size(780,790)
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
$picture=New-Object Windows.Forms.PictureBox
$picture.Image=[Drawing.Image]::FromFile((Join-Path $PSScriptRoot 'logo.png'))
$picture.SizeMode='Zoom'; $picture.SetBounds(34,20,600,70); $form.Controls.Add($picture)
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
$a=Label 'Best performance currently: MPC-HC. mpv uses 4K preview for 8K.' 36 330 700 30
$a.ForeColor=[Drawing.Color]::FromArgb(164,179,194)
$null=Label 'SELECT COMPONENTS  |  Installed status updates live' 36 369 700 25
function Check([string]$text,[int]$y,[bool]$checked) {
    $c=New-Object Windows.Forms.CheckBox; $c.Text=$text; $c.SetBounds(39,$y,690,29); $c.Checked=$checked; $form.Controls.Add($c); return $c
}
$currentState=Get-BridgeInstalledState $mpv
$mpcBox=Check 'MPC-HC playback' 400 $currentState.Mpc
$mpvBox=Check 'mpv playback' 436 $currentState.Mpv
$thumbBox=Check 'Windows Explorer thumbnails' 472 $currentState.Thumbnails
function Update-BridgeState {
    $state=Get-BridgeInstalledState $mpv
    if ($state.Mpc -ne $script:currentState.Mpc) { $mpcBox.Checked=$state.Mpc }
    if ($state.Mpv -ne $script:currentState.Mpv) { $mpvBox.Checked=$state.Mpv }
    if ($state.Thumbnails -ne $script:currentState.Thumbnails) { $thumbBox.Checked=$state.Thumbnails }
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
$install=Button 'Install selected' 36 531 200
$restore=Button 'Restore previous state' 251 531 210
$uninstall=Button 'Uninstall' 476 531 155
$status=Label 'Restore and uninstall return replaced files and settings to their saved state.' 36 586 700 44
$status.ForeColor=[Drawing.Color]::FromArgb(164,179,194)
$notice=Label 'No SonyRawDev? Install one of these Sony applications:' 36 634 700 24
$notice.ForeColor=[Drawing.Color]::FromArgb(164,179,194)
function DownloadLink([string]$text,[string]$url,[int]$x,[int]$width) {
    $link=New-Object Windows.Forms.LinkLabel
    $link.Text=$text; $link.SetBounds($x,662,$width,25)
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
$null=Label 'or' 155 662 30 25
DownloadLink 'Catalyst Prepare' 'https://support.d-imaging.sony.co.jp/disoft_DL/CatalystPrepareV3_DL/win?fm=en' 187 180
$footer=Label 'v1.13.2026.1114  |  Authored by magoowa  |  fx5-xocn-tools, Microsoft and AviSynth+ contributors' 36 724 710 45
$footer.ForeColor=[Drawing.Color]::FromArgb(164,179,194); $footer.Font=New-Object Drawing.Font('Segoe UI',8)
function Refresh-SoftwareDetection {
    $previous=if($sonyBox.SelectedIndex -ge 0){$script:sonyPaths[$sonyBox.SelectedIndex]}else{$null}
    . "$PSScriptRoot\Detect.ps1"
    $script:player=$player; $script:mpv=$mpv; $script:library=$library
    $mpcVersion=if($player){[Diagnostics.FileVersionInfo]::GetVersionInfo($player).FileVersion}else{'Not found'}
    $mpvVersion=if($mpv){[Diagnostics.FileVersionInfo]::GetVersionInfo($mpv).FileVersion}else{'Not found'}
    $mpcLabel.Text="MPC-HC:  $mpcVersion`n$player"
    $mpvLabel.Text="mpv:  $mpvVersion`n$mpv"
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
        Install-Bridge $mpcBox.Checked $mpvBox.Checked $thumbBox.Checked $chosenSony $mpv
        [Windows.Forms.MessageBox]::Show('Installation complete. Restart MPC-HC or mpv.','X-OCN Playback Bridge') | Out-Null
    } catch { [Windows.Forms.MessageBox]::Show($_.Exception.Message,'Installation failed','OK','Error') | Out-Null }
    finally { $install.Enabled=$true; $form.Cursor=[Windows.Forms.Cursors]::Default }
})
$doRestore={
    try {
        if(Get-Process mpc-hc,mpc-hc64,mpv -ErrorAction SilentlyContinue){throw 'Close MPC-HC and mpv first.'}
        Restore-BridgeJournal $journalPath
        [Windows.Forms.MessageBox]::Show('Previous state restored.','X-OCN Playback Bridge') | Out-Null
    } catch { [Windows.Forms.MessageBox]::Show($_.Exception.Message,'Restore failed','OK','Error') | Out-Null }
}
$restore.Add_Click($doRestore); $uninstall.Add_Click($doRestore)
$graphics=[Drawing.Graphics]::FromHwnd([IntPtr]::Zero)
try { $scale=[Math]::Max(1.0,$graphics.DpiX / 96.0) } finally { $graphics.Dispose() }
if ($scale -ne 1.0) {
    foreach ($control in $form.Controls) {
        $control.SetBounds([int]($control.Left*$scale),[int]($control.Top*$scale),[int]($control.Width*$scale),[int]($control.Height*$scale))
    }
    $form.ClientSize=New-Object Drawing.Size([int](780*$scale),[int](790*$scale))
}
[void]$form.ShowDialog()
$timer.Stop(); $timer.Dispose(); $picture.Image.Dispose(); $form.Dispose()
