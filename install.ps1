<#
.SYNOPSIS
  Installs gazekvm and starts the tray app (gazekvmw.exe) at login.

.DESCRIPTION
  Copies gazekvm.exe, gazekvmw.exe, the gazekvm-tobii.exe helper and the Tobii DLLs from
  build\Release to the install folder. An existing gazekvm.ini there is kept, so your
  calibration survives updates. Rerun after every rebuild to update.

  Registers a scheduled task "gazekvm" that starts the tray app 30 s after you log in,
  with highest privileges, so switching also works while an admin window is focused.
  Registering that task needs administrator rights; the script asks for them.

  Adds a "gazekvm" Start menu shortcut (type "gaze" in Start). It starts the tray app
  through the same task, so it gets the same privileges without a UAC prompt; if the
  tray app is already running it does nothing.

.EXAMPLE
  .\install.ps1                 # install or update, start at login, start now
.EXAMPLE
  .\install.ps1 -NoAutostart    # install without the login task
.EXAMPLE
  .\install.ps1 -Uninstall      # remove the task, shortcut and program files (keeps gazekvm.ini)
#>
param(
  [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'Programs\gazekvm'),
  [string]$BuildDir = (Join-Path $PSScriptRoot 'build\Release'),
  [switch]$NoAutostart,
  [switch]$NoStart,
  [switch]$Uninstall,
  # Start menu folder for the shortcut (this user's).
  [string]$StartMenuDir = [Environment]::GetFolderPath('Programs'),
  # Internal: set when the script relaunches itself elevated.
  [string]$TaskUser = "$env:USERDOMAIN\$env:USERNAME",
  [switch]$Elevated
)

$ErrorActionPreference = 'Stop'
$TaskName = 'gazekvm'
$Shortcut = Join-Path $StartMenuDir 'gazekvm.lnk'

function Test-Admin {
  $id = [Security.Principal.WindowsIdentity]::GetCurrent()
  ([Security.Principal.WindowsPrincipal]$id).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Wait-IfElevated {
  if ($Elevated) { Read-Host "`nPress Enter to close" | Out-Null }
}

function Stop-Gazekvm {
  if (Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue) {
    Stop-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
  }
  $running = Get-Process gazekvm, gazekvmw, gazekvm-tobii -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and $_.Path.StartsWith($InstallDir, [StringComparison]::OrdinalIgnoreCase) }
  foreach ($p in $running) {
    Write-Host "Stopping $($p.ProcessName) (pid $($p.Id))"
    # Stopping the task may already have ended it; that's fine.
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
  }
  if ($running) { Start-Sleep -Milliseconds 500 }
}

# The login task runs with highest privileges, which needs an elevated shell to register.
$needsAdmin = $Uninstall -or -not $NoAutostart
if ($needsAdmin -and -not (Test-Admin)) {
  Write-Host 'Administrator rights are needed for the login task; asking...'
  # Resolve paths and the user now, so the elevated copy uses this user's, not the admin's.
  $forward = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"",
    '-InstallDir', "`"$InstallDir`"", '-BuildDir', "`"$BuildDir`"",
    '-TaskUser', "`"$TaskUser`"", '-StartMenuDir', "`"$StartMenuDir`"", '-Elevated')
  if ($NoAutostart) { $forward += '-NoAutostart' }
  if ($NoStart) { $forward += '-NoStart' }
  if ($Uninstall) { $forward += '-Uninstall' }
  $shell = (Get-Process -Id $PID).Path
  $p = Start-Process -FilePath $shell -Verb RunAs -ArgumentList $forward -Wait -PassThru
  exit $p.ExitCode
}

try {
  if ($Uninstall) {
    Stop-Gazekvm
    if (Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue) {
      Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
      Write-Host "Removed scheduled task '$TaskName'"
    }
    if (Test-Path $Shortcut) {
      Remove-Item $Shortcut -Force
      Write-Host "Removed Start menu shortcut"
    }
    if (Test-Path $InstallDir) {
      Get-ChildItem $InstallDir -File | Where-Object Name -ne 'gazekvm.ini' | Remove-Item -Force
      Write-Host "Removed program files from $InstallDir"
      if (Test-Path (Join-Path $InstallDir 'gazekvm.ini')) {
        Write-Host "Kept $InstallDir\gazekvm.ini (your calibration). Delete the folder to remove it."
      }
    }
    Wait-IfElevated
    exit 0
  }

  # --- Install / update --------------------------------------------------------
  foreach ($f in 'gazekvm.exe', 'gazekvmw.exe') {
    if (-not (Test-Path (Join-Path $BuildDir $f))) {
      throw "$f not found in $BuildDir. Build first: cmake --build build --config Release"
    }
  }

  Stop-Gazekvm
  New-Item -ItemType Directory -Force $InstallDir | Out-Null

  # The Tobii.Interaction helper is optional (it needs the dotnet SDK to build).
  $helper = @('gazekvm-tobii.exe', 'gazekvm-tobii.exe.config') | Where-Object { Test-Path (Join-Path $BuildDir $_) }
  $files = @('gazekvm.exe', 'gazekvmw.exe') + $helper + @(Get-ChildItem $BuildDir -Filter '*.dll' | ForEach-Object Name)
  foreach ($f in $files) {
    Copy-Item (Join-Path $BuildDir $f) $InstallDir -Force
    Write-Host "Copied $f"
  }

  # Keep an existing config; otherwise take the one you've been using in the build
  # folder (it may hold your calibration), else the repo template.
  $ini = Join-Path $InstallDir 'gazekvm.ini'
  if (Test-Path $ini) {
    Write-Host "Kept existing $ini"
  } else {
    $src = Join-Path $BuildDir 'gazekvm.ini'
    if (-not (Test-Path $src)) { $src = Join-Path $PSScriptRoot 'config\gazekvm.ini' }
    Copy-Item $src $ini
    Write-Host "Config: copied $src"
  }

  $exe = Join-Path $InstallDir 'gazekvmw.exe'
  if ($NoAutostart) {
    if (Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue) {
      Write-Host "Note: the '$TaskName' login task from an earlier install still exists (-Uninstall removes it)."
    }
  } else {
    $action = New-ScheduledTaskAction -Execute $exe -WorkingDirectory $InstallDir
    $trigger = New-ScheduledTaskTrigger -AtLogOn -User $TaskUser
    $trigger.Delay = 'PT30S'  # let Deskflow and Tobii start first
    $principal = New-ScheduledTaskPrincipal -UserId $TaskUser -LogonType Interactive -RunLevel Highest
    $settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) `
      -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -MultipleInstances IgnoreNew
    Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger `
      -Principal $principal -Settings $settings -Force | Out-Null
    Write-Host "Scheduled task '$TaskName': starts gazekvmw.exe 30 s after $TaskUser logs in (highest privileges)"
  }

  # Start menu shortcut. With the login task, go through it (same privileges, no UAC
  # prompt, and the task ignores a second start while the tray app is running).
  $wsh = New-Object -ComObject WScript.Shell
  $lnk = $wsh.CreateShortcut($Shortcut)
  if ($NoAutostart) {
    $lnk.TargetPath = $exe
    $lnk.Arguments = ''
    $lnk.WindowStyle = 1
  } else {
    $lnk.TargetPath = Join-Path $env:SystemRoot 'System32\schtasks.exe'
    $lnk.Arguments = "/run /tn $TaskName"
    $lnk.WindowStyle = 7  # minimized: hides schtasks' brief console window
  }
  $lnk.WorkingDirectory = $InstallDir
  $lnk.IconLocation = "$exe,0"
  $lnk.Description = 'Switch Deskflow screens by where you look (tray app)'
  $lnk.Save()
  Write-Host "Start menu shortcut: $Shortcut"

  if (-not $NoStart) {
    if ($NoAutostart) { Start-Process $exe -WorkingDirectory $InstallDir }
    else { Start-ScheduledTask -TaskName $TaskName }
    Write-Host 'Started gazekvm in the tray.'
  }

  Write-Host "`nInstalled to $InstallDir"
  Write-Host 'Calibrate from the tray menu, or run: gazekvm.exe --calibrate (from that folder, with the tray app quit)'
  Wait-IfElevated
} catch {
  Write-Host "`nFAILED: $_" -ForegroundColor Red
  Wait-IfElevated
  exit 1
}
