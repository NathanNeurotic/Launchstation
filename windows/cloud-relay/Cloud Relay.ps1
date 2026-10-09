[CmdletBinding()]
param(
    [string]$CloudRemote,
    [string]$Ps2 = '192.168.1.111',
    [string]$BackupRoot,
    [string]$Rclone,
    [string]$RcloneConfig,
    [switch]$Configure,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

function Find-Rclone {
    param([string]$Explicit)

    $candidates = @()
    if ($Explicit) { $candidates += $Explicit }
    $candidates += (Join-Path $here 'rclone.exe')
    $candidates += (Join-Path $here '..\PS2 Drive\rclone.exe')
    $candidates += (Join-Path $here '..\..\windows\en\PS2 Drive\rclone.exe')

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $cmd = Get-Command rclone.exe -ErrorAction SilentlyContinue
    if (-not $cmd) { $cmd = Get-Command rclone -ErrorAction SilentlyContinue }
    if ($cmd) { return $cmd.Source }
    return $null
}

$script:RcloneExe = Find-Rclone $Rclone
if (-not $script:RcloneExe) {
    Write-Error 'rclone was not found. Put this folder beside MrRexD''s "PS2 Drive" folder, pass -Rclone, or install rclone in PATH.'
    exit 2
}

function Rc-Capture {
    param([string[]]$Arguments)

    $all = @()
    if ($RcloneConfig) { $all += @('--config', $RcloneConfig) }
    $all += $Arguments
    $output = & $script:RcloneExe @all 2>&1
    return [pscustomobject]@{
        Ok = ($LASTEXITCODE -eq 0)
        Output = @($output | ForEach-Object { "$_" })
    }
}

function Rc-Run {
    param([string[]]$Arguments)

    $all = @()
    if ($RcloneConfig) { $all += @('--config', $RcloneConfig) }
    $all += $Arguments
    & $script:RcloneExe @all
    return $LASTEXITCODE
}

if ($Configure) {
    $configArgs = @('config')
    if ($RcloneConfig) { $configArgs = @('--config', $RcloneConfig, 'config') }
    & $script:RcloneExe @configArgs
    exit $LASTEXITCODE
}

if (-not $CloudRemote) {
    Write-Host ''
    Write-Host 'CloudRemote is required for a relay run.'
    Write-Host 'Example:'
    Write-Host '  .\Cloud Relay.ps1 -CloudRemote "gdrive:PS2/WLE-MMCE-Backups"'
    Write-Host ''
    Write-Host 'Run with -Configure first if rclone does not already have a cloud remote.'
    exit 2
}

$remoteName = ($CloudRemote -split ':', 2)[0] + ':'
$remotes = Rc-Capture @('listremotes')
if (-not $remotes.Ok -or -not ($remotes.Output -contains $remoteName)) {
    Write-Error "The rclone remote '$remoteName' is not configured. Run this script with -Configure first."
    exit 2
}

$obscured = Rc-Capture @('obscure', 'ps2')
if (-not $obscured.Ok -or $obscured.Output.Count -lt 1) {
    Write-Error 'Could not prepare the FTP password for rclone.'
    exit 2
}
$pw = $obscured.Output[-1].Trim()
$ps2Remote = ":ftp,host=$Ps2,user=anonymous,pass=$pw,disable_epsv=true,disable_mlsd=true:"

$probe = Rc-Capture @('lsf', $ps2Remote, '--max-depth', '1')
if (-not $probe.Ok) {
    Write-Error "The PS2 FTP server at $Ps2 did not answer. Start FTP / Remote access in Launchstation and leave the dashboard open."
    exit 3
}

function To-FtpPath {
    param([string]$Path)

    if (-not $Path) { return $null }
    $p = $Path.Trim().Trim('"').Trim() -replace '\\', '/'

    if ($p -match '^(mmce|mc)(\d):/?(.*)$') {
        return "$($matches[1])/$($matches[2])/$($matches[3])".TrimEnd('/')
    }
    if ($p -match '^(ata|usb|mx4sio)(\d):/?(.*)$') {
        return "$($matches[1])/$($matches[2])/$($matches[3])".TrimEnd('/')
    }
    if ($p -match '^mass(\d?):/?(.*)$') {
        $unit = if ($matches[1]) { $matches[1] } else { '0' }
        return "mass/$unit/$($matches[2])".TrimEnd('/')
    }

    if ($p -notmatch ':') {
        return $p.TrimStart('/').TrimEnd('/')
    }
    return $null
}

function Test-Ps2Dir {
    param([string]$Path)

    if (-not $Path) { return $false }
    $r = Rc-Capture @('lsf', "$ps2Remote$Path", '--max-depth', '1')
    return $r.Ok
}

$source = $null
if ($BackupRoot) {
    $candidate = To-FtpPath $BackupRoot
    if (-not $candidate) {
        Write-Error "BackupRoot '$BackupRoot' is not a supported PS2/FTP path."
        exit 4
    }

    if ($candidate -notmatch '(^|/)WLE-MMCE-Backups$') {
        $history = ($candidate.TrimEnd('/') + '/WLE-MMCE-Backups')
        if (Test-Ps2Dir $history) { $candidate = $history }
    }

    if (-not (Test-Ps2Dir $candidate)) {
        Write-Error "No backup history was found at '$BackupRoot'. Pass either the destination chosen in Launchstation or its WLE-MMCE-Backups folder."
        exit 4
    }
    $source = $candidate
}
else {
    Write-Host 'Looking for WLE-MMCE-Backups on the PS2...'
    $scan = Rc-Capture @('lsf', $ps2Remote, '--recursive', '--dirs-only', '--max-depth', '8')
    if ($scan.Ok) {
        $found = @(
            $scan.Output |
                ForEach-Object { "$_".Trim() } |
                Where-Object { $_ -match '(^|/)WLE-MMCE-Backups/$' } |
                ForEach-Object { $_.TrimEnd('/') } |
                Sort-Object -Unique
        )

        if ($found.Count -eq 1) {
            $source = $found[0]
        }
        elseif ($found.Count -gt 1) {
            Write-Host 'More than one WLE-MMCE-Backups folder is visible:'
            $found | ForEach-Object { Write-Host "  $_" }
            Write-Error 'Choose one with -BackupRoot.'
            exit 4
        }
    }

    if (-not $source) {
        Write-Error 'Could not auto-detect WLE-MMCE-Backups. Pass -BackupRoot with the destination selected during OPL IGR backup setup.'
        exit 4
    }
}

Write-Host ''
Write-Host "PS2 backup source: $source"
Write-Host "Cloud destination: $CloudRemote"
Write-Host 'Relay policy: copy new immutable history files only; existing cloud files are never deleted or replaced.'

$copy = @(
    'copy',
    "$ps2Remote$source",
    $CloudRemote,
    '--ignore-existing',
    '--log-level', 'INFO'
)
if ($DryRun) { $copy += '--dry-run' }

$code = Rc-Run $copy
if ($code -ne 0) {
    Write-Error "Cloud relay failed with rclone exit code $code."
    exit $code
}

Write-Host ''
if ($DryRun) {
    Write-Host 'Dry run completed. No files were changed.'
}
else {
    Write-Host 'Cloud relay completed.'
}
exit 0
