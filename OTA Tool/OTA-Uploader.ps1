Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

# ---------- Settings (saved next to this script) ----------
$script:SettingsFile = Join-Path $PSScriptRoot "OTA-Uploader.settings.json"
$settings = [ordered]@{
    EspotaPath = "C:\Users\cardb\AppData\Local\Arduino15\packages\esp32\hardware\esp32\3.3.12\tools\espota.exe"
    Ip         = "192.168.10.1"
    Port       = "3232"
    Password   = ""
    Ssid       = ""
    AutoReconnect = "True"
    RestartAdapter = "True"
    BinPath    = "C:\Users\cardb\OneDrive\Documents\Code\BlueWhale\dual_core_all_features\build\esp32.esp32.esp32s3\dual_core_all_features.ino.bin"
}
if (Test-Path $script:SettingsFile) {
    try {
        $saved = Get-Content $script:SettingsFile -Raw | ConvertFrom-Json
        foreach ($k in @($settings.Keys)) {
            if ($null -ne $saved.$k) { $settings[$k] = [string]$saved.$k }
        }
    } catch {}
}

function Save-Settings {
    $settings.EspotaPath = $txtEspota.Text
    $settings.Ip         = $txtIp.Text
    $settings.Port       = $txtPort.Text
    $settings.Password   = $txtPass.Text
    $settings.BinPath    = $txtBin.Text
    $settings.Ssid       = $txtSsid.Text
    $settings.AutoReconnect  = [string]$chkAuto.Checked
    $settings.RestartAdapter = [string]$chkAdapter.Checked
    try { $settings | ConvertTo-Json | Set-Content $script:SettingsFile } catch {}
}

# ---------- Form ----------
$form = New-Object System.Windows.Forms.Form
$form.Text = "ESP32 OTA Uploader"
$form.Size = New-Object System.Drawing.Size(720, 560)
$form.MinimumSize = New-Object System.Drawing.Size(620, 480)
$form.StartPosition = "CenterScreen"
$form.Font = New-Object System.Drawing.Font("Segoe UI", 9)

function New-Label($text, $x, $y) {
    $l = New-Object System.Windows.Forms.Label
    $l.Text = $text
    $l.Location = New-Object System.Drawing.Point($x, $y)
    $l.AutoSize = $true
    $form.Controls.Add($l)
    return $l
}

# espota.exe path
New-Label "espota.exe location:" 15 15 | Out-Null
$txtEspota = New-Object System.Windows.Forms.TextBox
$txtEspota.Location = New-Object System.Drawing.Point(15, 35)
$txtEspota.Size = New-Object System.Drawing.Size(580, 24)
$txtEspota.Anchor = "Top,Left,Right"
$txtEspota.Text = $settings.EspotaPath
$form.Controls.Add($txtEspota)

$btnEspota = New-Object System.Windows.Forms.Button
$btnEspota.Text = "Browse..."
$btnEspota.Location = New-Object System.Drawing.Point(605, 33)
$btnEspota.Size = New-Object System.Drawing.Size(85, 27)
$btnEspota.Anchor = "Top,Right"
$form.Controls.Add($btnEspota)

# Firmware path
New-Label "Firmware (.bin) file:" 15 70 | Out-Null
$txtBin = New-Object System.Windows.Forms.TextBox
$txtBin.Location = New-Object System.Drawing.Point(15, 90)
$txtBin.Size = New-Object System.Drawing.Size(580, 24)
$txtBin.Anchor = "Top,Left,Right"
$txtBin.Text = $settings.BinPath
$form.Controls.Add($txtBin)

$btnBin = New-Object System.Windows.Forms.Button
$btnBin.Text = "Browse..."
$btnBin.Location = New-Object System.Drawing.Point(605, 88)
$btnBin.Size = New-Object System.Drawing.Size(85, 27)
$btnBin.Anchor = "Top,Right"
$form.Controls.Add($btnBin)

# IP / Port / Password
New-Label "ESP32 IP address:" 15 125 | Out-Null
$txtIp = New-Object System.Windows.Forms.TextBox
$txtIp.Location = New-Object System.Drawing.Point(15, 145)
$txtIp.Size = New-Object System.Drawing.Size(180, 24)
$txtIp.Text = $settings.Ip
$form.Controls.Add($txtIp)

New-Label "Port:" 215 125 | Out-Null
$txtPort = New-Object System.Windows.Forms.TextBox
$txtPort.Location = New-Object System.Drawing.Point(215, 145)
$txtPort.Size = New-Object System.Drawing.Size(70, 24)
$txtPort.Text = $settings.Port
$form.Controls.Add($txtPort)

New-Label "OTA password (optional):" 305 125 | Out-Null
$txtPass = New-Object System.Windows.Forms.TextBox
$txtPass.Location = New-Object System.Drawing.Point(305, 145)
$txtPass.Size = New-Object System.Drawing.Size(180, 24)
$txtPass.UseSystemPasswordChar = $true
$txtPass.Text = $settings.Password
$form.Controls.Add($txtPass)

New-Label "WiFi network (SSID):" 505 125 | Out-Null
$txtSsid = New-Object System.Windows.Forms.TextBox
$txtSsid.Location = New-Object System.Drawing.Point(505, 145)
$txtSsid.Size = New-Object System.Drawing.Size(185, 24)
$txtSsid.Anchor = "Top,Right"
$txtSsid.Text = $settings.Ssid
$form.Controls.Add($txtSsid)

# Buttons
$btnUpload = New-Object System.Windows.Forms.Button
$btnUpload.Text = "Upload"
$btnUpload.Location = New-Object System.Drawing.Point(15, 185)
$btnUpload.Size = New-Object System.Drawing.Size(120, 34)
$btnUpload.BackColor = [System.Drawing.Color]::FromArgb(0, 120, 215)
$btnUpload.ForeColor = [System.Drawing.Color]::White
$btnUpload.FlatStyle = "Flat"
$form.Controls.Add($btnUpload)

$btnCancel = New-Object System.Windows.Forms.Button
$btnCancel.Text = "Cancel"
$btnCancel.Location = New-Object System.Drawing.Point(145, 185)
$btnCancel.Size = New-Object System.Drawing.Size(90, 34)
$btnCancel.Enabled = $false
$form.Controls.Add($btnCancel)

$btnPing = New-Object System.Windows.Forms.Button
$btnPing.Text = "Ping"
$btnPing.Location = New-Object System.Drawing.Point(245, 185)
$btnPing.Size = New-Object System.Drawing.Size(70, 34)
$form.Controls.Add($btnPing)

$btnReconnect = New-Object System.Windows.Forms.Button
$btnReconnect.Text = "Reconnect WiFi"
$btnReconnect.Location = New-Object System.Drawing.Point(325, 185)
$btnReconnect.Size = New-Object System.Drawing.Size(120, 34)
$form.Controls.Add($btnReconnect)

$chkAuto = New-Object System.Windows.Forms.CheckBox
$chkAuto.Text = "Auto-reconnect after upload"
$chkAuto.Location = New-Object System.Drawing.Point(458, 184)
$chkAuto.Size = New-Object System.Drawing.Size(232, 18)
$chkAuto.Checked = ($settings.AutoReconnect -eq "True")
$form.Controls.Add($chkAuto)

$chkAdapter = New-Object System.Windows.Forms.CheckBox
$chkAdapter.Text = "Restart WiFi adapter (admin only)"
$chkAdapter.Location = New-Object System.Drawing.Point(458, 203)
$chkAdapter.Size = New-Object System.Drawing.Size(232, 18)
$chkAdapter.Checked = ($settings.RestartAdapter -eq "True")
$form.Controls.Add($chkAdapter)

# Progress
$progress = New-Object System.Windows.Forms.ProgressBar
$progress.Location = New-Object System.Drawing.Point(15, 232)
$progress.Size = New-Object System.Drawing.Size(675, 20)
$progress.Anchor = "Top,Left,Right"
$progress.Minimum = 0
$progress.Maximum = 100
$form.Controls.Add($progress)

$lblStatus = New-Object System.Windows.Forms.Label
$lblStatus.Text = "Ready"
$lblStatus.Location = New-Object System.Drawing.Point(15, 257)
$lblStatus.Size = New-Object System.Drawing.Size(675, 20)
$lblStatus.Anchor = "Top,Left,Right"
$form.Controls.Add($lblStatus)

# Log
$txtLog = New-Object System.Windows.Forms.TextBox
$txtLog.Location = New-Object System.Drawing.Point(15, 282)
$txtLog.Size = New-Object System.Drawing.Size(675, 228)
$txtLog.Anchor = "Top,Bottom,Left,Right"
$txtLog.Multiline = $true
$txtLog.ReadOnly = $true
$txtLog.ScrollBars = "Vertical"
$txtLog.Font = New-Object System.Drawing.Font("Consolas", 9)
$txtLog.BackColor = [System.Drawing.Color]::White
$form.Controls.Add($txtLog)

function Log($msg) { $txtLog.AppendText("$msg`r`n") }

# ---------- Browse dialogs ----------
$btnEspota.Add_Click({
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Filter = "espota|espota.exe;espota.py|All files|*.*"
    if (Test-Path $txtEspota.Text) { $dlg.InitialDirectory = Split-Path $txtEspota.Text -Parent }
    if ($dlg.ShowDialog() -eq "OK") { $txtEspota.Text = $dlg.FileName }
})

$btnBin.Add_Click({
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Filter = "Firmware (*.bin)|*.bin|All files|*.*"
    if (Test-Path $txtBin.Text) { $dlg.InitialDirectory = Split-Path $txtBin.Text -Parent }
    if ($dlg.ShowDialog() -eq "OK") { $txtBin.Text = $dlg.FileName }
})

# ---------- Ping ----------
$btnPing.Add_Click({
    $ip = $txtIp.Text.Trim()
    if (-not $ip) { return }
    $form.Cursor = "WaitCursor"
    try {
        $ok = Test-Connection -ComputerName $ip -Count 2 -Quiet -ErrorAction SilentlyContinue
        if ($ok) { Log "Ping $ip : reply received" } else { Log "Ping $ip : no reply" }
    } finally { $form.Cursor = "Default" }
})

# ---------- Output polling ----------
$script:proc = $null
$script:outFile = $null
$script:errFile = $null
$script:outPos = 0L
$script:errPos = 0L
$script:cancelled = $false

function Read-New($path, [ref]$pos) {
    if (-not (Test-Path $path)) { return "" }
    try {
        $fs = New-Object System.IO.FileStream($path, "Open", "Read", "ReadWrite")
        try {
            if ($fs.Length -le $pos.Value) { return "" }
            $fs.Seek($pos.Value, "Begin") | Out-Null
            $buf = New-Object byte[] ($fs.Length - $pos.Value)
            $n = $fs.Read($buf, 0, $buf.Length)
            $pos.Value += $n
            return [System.Text.Encoding]::UTF8.GetString($buf, 0, $n)
        } finally { $fs.Close() }
    } catch { return "" }
}

function Process-Text($text) {
    if (-not $text) { return }
    foreach ($seg in ($text -split "[\r\n]+")) {
        $s = $seg.Trim()
        if (-not $s) { continue }
        if ($s -match '(\d{1,3})\s*%') {
            $pct = [Math]::Min(100, [int]$Matches[1])
            $progress.Value = $pct
            $lblStatus.Text = $s
        } else {
            Log $s
        }
    }
}

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 200
$timer.Add_Tick({
    if ($null -eq $script:proc) { return }
    Process-Text (Read-New $script:outFile ([ref]$script:outPos))
    Process-Text (Read-New $script:errFile ([ref]$script:errPos))

    if ($script:proc.HasExited) {
        $timer.Stop()
        Start-Sleep -Milliseconds 150
        Process-Text (Read-New $script:outFile ([ref]$script:outPos))
        Process-Text (Read-New $script:errFile ([ref]$script:errPos))
        $code = $script:proc.ExitCode
        if ($script:cancelled) {
            $lblStatus.Text = "Cancelled"
            Log "--- Cancelled ---"
        } elseif ($code -eq 0) {
            $progress.Value = 100
            $lblStatus.Text = "Upload complete. The ESP32 should now reboot."
            Log "--- Success (exit code 0) ---"
        } else {
            $lblStatus.Text = "Upload failed (exit code $code)"
            Log "--- Failed (exit code $code) ---"
        }
        Remove-Item $script:outFile, $script:errFile -ErrorAction SilentlyContinue
        $script:proc = $null
        $btnUpload.Enabled = $true
        $btnCancel.Enabled = $false
        if ($code -eq 0 -and -not $script:cancelled -and $chkAuto.Checked) {
            Invoke-Reconnect 8
        }
    }
})

# ---------- Reconnect WiFi ----------
$script:isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if ($script:isAdmin) { $form.Text += " (Administrator)" } else { $form.Text += " (not admin - limited reconnect)" }
$script:reconnecting = $false

function Wait-UI([int]$ms) {
    $end = [DateTime]::Now.AddMilliseconds($ms)
    while ([DateTime]::Now -lt $end) {
        [System.Windows.Forms.Application]::DoEvents()
        Start-Sleep -Milliseconds 50
    }
}

function Get-CurrentSsid {
    $out = netsh wlan show interfaces 2>$null
    foreach ($line in $out) {
        if ($line -match '^\s*SSID\s*:\s*(.+)$') { return $Matches[1].Trim() }
    }
    return ""
}

function Get-WifiAdapter {
    try {
        return Get-NetAdapter -Physical -ErrorAction Stop |
            Where-Object { $_.PhysicalMediaType -match '802.11' -or $_.InterfaceDescription -match 'Wireless|Wi-?Fi|WLAN' } |
            Select-Object -First 1
    } catch { return $null }
}

function Invoke-Reconnect([int]$delaySec = 0) {
    if ($script:reconnecting) { return }
    $script:reconnecting = $true
    $btnReconnect.Enabled = $false
    $btnUpload.Enabled = $false
    $form.Cursor = "WaitCursor"
    try {
        $ip = $txtIp.Text.Trim()
        $ssid = $txtSsid.Text.Trim()
        if (-not $ssid) {
            $ssid = Get-CurrentSsid
            if ($ssid) { $txtSsid.Text = $ssid }
        }

        Log ""
        Log "--- Reconnect WiFi ---"
        if ($delaySec -gt 0) {
            Log "Waiting $delaySec s for the ESP32 to reboot..."
            $lblStatus.Text = "Waiting for ESP32 to reboot..."
            Wait-UI ($delaySec * 1000)
        }
        if (-not $ssid) {
            Log "No WiFi network name known. Enter it in 'WiFi network (SSID)' and try again."
            $lblStatus.Text = "Reconnect skipped - no SSID"
            return
        }

        $adapter = Get-WifiAdapter
        if ($script:isAdmin) {
            Log "Clearing ARP cache..."
            arp -d * | Out-Null
            if ($chkAdapter.Checked -and $adapter) {
                Log "Restarting adapter '$($adapter.Name)'..."
                $lblStatus.Text = "Restarting WiFi adapter..."
                try { Restart-NetAdapter -Name $adapter.Name -Confirm:$false -ErrorAction Stop }
                catch { Log "  Adapter restart failed: $_" }
                Wait-UI 4000
            }
        } else {
            Log "Not running as administrator: skipping ARP clear, adapter restart and IP renew."
            Log "(Right-click the script / PowerShell and choose 'Run as administrator' for the full fix.)"
        }

        Log "Disconnecting..."
        $lblStatus.Text = "Reconnecting to $ssid..."
        netsh wlan disconnect | Out-Null
        Wait-UI 3000

        Log "Connecting to '$ssid'..."
        $r = netsh wlan connect name="$ssid" 2>&1
        Log (($r | Out-String).Trim())
        Wait-UI 4000

        if ($script:isAdmin -and $adapter) {
            Log "Renewing IP lease..."
            ipconfig /renew "$($adapter.Name)" | Out-Null
        }

        $ok = $false
        for ($i = 1; $i -le 10 -and -not $ok; $i++) {
            $lblStatus.Text = "Waiting for $ip to respond ($i/10)..."
            $ok = Test-Connection -ComputerName $ip -Count 1 -Quiet -ErrorAction SilentlyContinue
            if (-not $ok) { Wait-UI 1500 }
        }
        if ($ok) {
            Log "$ip is reachable. You can open the web page now."
            $lblStatus.Text = "Reconnected - $ip is reachable"
        } else {
            Log "$ip did not respond. Check the SSID, or try 'Run as administrator'."
            $lblStatus.Text = "Reconnect finished - $ip not reachable"
        }
    } finally {
        $form.Cursor = "Default"
        $btnReconnect.Enabled = $true
        if ($null -eq $script:proc) { $btnUpload.Enabled = $true }
        $script:reconnecting = $false
    }
}

$btnReconnect.Add_Click({ Invoke-Reconnect 0 })

# ---------- Upload ----------
$btnUpload.Add_Click({
    $esp = $txtEspota.Text.Trim().Trim('"')
    $bin = $txtBin.Text.Trim().Trim('"')
    $ip  = $txtIp.Text.Trim()
    $port = $txtPort.Text.Trim()

    if (-not (Test-Path $esp -PathType Leaf)) {
        [System.Windows.Forms.MessageBox]::Show("espota.exe not found:`n$esp", "Error", "OK", "Error") | Out-Null; return
    }
    if (-not (Test-Path $bin -PathType Leaf)) {
        [System.Windows.Forms.MessageBox]::Show("Firmware file not found:`n$bin", "Error", "OK", "Error") | Out-Null; return
    }
    if (-not $ip) {
        [System.Windows.Forms.MessageBox]::Show("Enter the ESP32 IP address.", "Error", "OK", "Error") | Out-Null; return
    }
    if ($port -notmatch '^\d+$') {
        [System.Windows.Forms.MessageBox]::Show("Port must be a number (default 3232).", "Error", "OK", "Error") | Out-Null; return
    }

    Save-Settings

    $argList = "-i $ip -p $port -r -d -f `"$bin`""
    if ($txtPass.Text) { $argList += " -a `"$($txtPass.Text)`"" }

    $script:outFile = Join-Path $env:TEMP "ota_out_$PID.txt"
    $script:errFile = Join-Path $env:TEMP "ota_err_$PID.txt"
    "" | Set-Content $script:outFile
    "" | Set-Content $script:errFile
    $script:outPos = 0L
    $script:errPos = 0L
    $script:cancelled = $false

    $progress.Value = 0
    $lblStatus.Text = "Uploading..."
    $txtLog.Clear()
    Log "> espota.exe -i $ip -p $port -f `"$bin`""
    Log ""

    $env:PYTHONUNBUFFERED = "1"
    try {
        $script:proc = Start-Process -FilePath $esp -ArgumentList $argList `
            -WorkingDirectory (Split-Path $esp -Parent) `
            -RedirectStandardOutput $script:outFile -RedirectStandardError $script:errFile `
            -WindowStyle Hidden -PassThru
    } catch {
        Log "Failed to start: $_"
        return
    }
    $btnUpload.Enabled = $false
    $btnCancel.Enabled = $true
    $timer.Start()
})

$btnCancel.Add_Click({
    if ($script:proc -and -not $script:proc.HasExited) {
        $script:cancelled = $true
        try { $script:proc.Kill() } catch {}
    }
})

$form.Add_FormClosing({
    Save-Settings
    if ($script:proc -and -not $script:proc.HasExited) { try { $script:proc.Kill() } catch {} }
})

[void]$form.ShowDialog()
