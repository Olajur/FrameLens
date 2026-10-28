<#
.SYNOPSIS
    Installs / removes the FrameLens OSD spike in "Tier 2" form for local testing:
    a uiAccess build that is signed with a throw-away, local-only test certificate.

.DESCRIPTION
    Windows only grants uiAccess to executables that are Authenticode-signed with a
    trusted certificate AND run from a secure location such as Program Files.

    install:
      1. Creates a self-signed code-signing certificate valid for 30 days
         (subject "CN=FrameLens OSD Spike Test Signing (local only)").
      2. Copies framelens-osd-spike-uia.exe to "%ProgramFiles%\FrameLens OSD Spike".
      3. Signs that copy.
      4. Trusts the certificate on this PC (LocalMachine Root + TrustedPublisher).
      5. DELETES THE CERTIFICATE'S PRIVATE KEY. After this step nothing else can ever
         be signed with it - the trust you added only covers the one file signed in step 3.
      6. Verifies the signature.

    uninstall: stops the spike, removes the folder and both trusted certificate entries.
    status:    shows what is installed and whether the signature is valid.

    Requires one UAC prompt for install/uninstall. Nothing else on the system is changed.

.NOTES
    The signature has no timestamp, so it becomes invalid when the 30-day certificate
    expires; the uiAccess build then stops launching. Run uninstall when you're done.
#>
[CmdletBinding()]
param(
    [ValidateSet('install', 'uninstall', 'status')]
    [string]$Action = 'install',
    [string]$SourceDir,
    [string]$ResultFile,
    [switch]$Elevated
)

$ErrorActionPreference = 'Stop'
$Subject    = 'CN=FrameLens OSD Spike Test Signing (local only)'
$InstallDir = Join-Path $env:ProgramFiles 'FrameLens OSD Spike'
$ExeName    = 'framelens-osd-spike-uia.exe'
$Stores     = @('Cert:\LocalMachine\Root', 'Cert:\LocalMachine\TrustedPublisher')
if (-not $SourceDir) {
    # Next to this script (copied spike folder), else the repo's build output (tools\..\build).
    $SourceDir = $PSScriptRoot
    $repoBuild = Join-Path (Split-Path $PSScriptRoot -Parent) 'build'
    if (-not (Test-Path (Join-Path $SourceDir $ExeName)) -and (Test-Path (Join-Path $repoBuild $ExeName))) {
        $SourceDir = (Resolve-Path $repoBuild).Path
    }
}

function Write-Result([string]$text) {
    if ($ResultFile) { try { Set-Content -LiteralPath $ResultFile -Value $text -Encoding UTF8 } catch { } }
}

function Test-IsAdmin {
    $p = New-Object System.Security.Principal.WindowsPrincipal([System.Security.Principal.WindowsIdentity]::GetCurrent())
    $p.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-TestCerts {
    foreach ($s in $Stores) { Get-ChildItem $s | Where-Object { $_.Subject -eq $Subject } }
}

function Show-Status {
    $exe = Join-Path $InstallDir $ExeName
    Write-Host ("  Installed exe : {0}" -f $(if (Test-Path $exe) { $exe } else { 'not installed' }))
    if (Test-Path $exe) {
        $sig = Get-AuthenticodeSignature $exe
        Write-Host ("  Signature     : {0}" -f $sig.Status)
    }
    $certs = @(Get-TestCerts)
    if ($certs.Count -eq 0) { Write-Host '  Trusted certs : none' }
    foreach ($c in $certs) {
        Write-Host ("  Trusted cert  : {0}  expires {1:yyyy-MM-dd}  private key: {2}" -f $c.PSParentPath.Split(':')[-1], $c.NotAfter, $(if ($c.HasPrivateKey) { 'PRESENT' } else { 'none' }))
    }
}

Write-Host ''
Write-Host 'FrameLens OSD spike - Tier 2 test install' -ForegroundColor Cyan
Write-Host ''

if ($Action -eq 'status') { Show-Status; exit 0 }

if (-not (Test-IsAdmin)) {
    if ($Elevated) { Write-Result 'Elevation did not grant administrator rights.'; exit 2 }
    if ($Action -eq 'install' -and -not (Test-Path (Join-Path $SourceDir $ExeName))) {
        Write-Host "$ExeName not found next to this script ($SourceDir)." -ForegroundColor Red; exit 2
    }
    Write-Host 'Requesting administrator approval (UAC)...'
    $resultFile = Join-Path $env:TEMP 'framelens-osd-spike-setup.result.txt'
    Remove-Item -LiteralPath $resultFile -Force -ErrorAction SilentlyContinue
    $psArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"",
                '-Action', $Action, '-SourceDir', "`"$SourceDir`"", '-ResultFile', "`"$resultFile`"", '-Elevated')
    try {
        $proc = Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" `
                              -ArgumentList $psArgs -Verb RunAs -Wait -PassThru -WindowStyle Hidden
    }
    catch { Write-Host 'Cancelled - nothing was changed.' -ForegroundColor Yellow; exit 3 }
    if ($proc.ExitCode -eq 0) {
        Write-Host "Done ($Action)." -ForegroundColor Green
        if ($Action -eq 'install') {
            Write-Host "Start the Tier 2 spike from: $InstallDir\$ExeName" -ForegroundColor Green
        }
    }
    else {
        Write-Host "Failed (exit code $($proc.ExitCode))." -ForegroundColor Red
        if (Test-Path -LiteralPath $resultFile) {
            Get-Content -LiteralPath $resultFile | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
        }
        else {
            Write-Host '  The elevated step left no error details.' -ForegroundColor Red
        }
    }
    Write-Host ''
    Show-Status
    exit $proc.ExitCode
}

# ----------------------------------------------------------------- elevated part
$step = 'starting'
try {
    # Stop a running spike so files can be replaced/removed.
    Get-Process -Name 'framelens-osd-spike-uia', 'framelens-osd-spike' -ErrorAction SilentlyContinue | Stop-Process -Force

    if ($Action -eq 'uninstall') {
        foreach ($c in @(Get-TestCerts)) { Remove-Item -Path $c.PSPath -Force }
        # Also clean up any leftover key-holding copy (only exists if an install was interrupted).
        Get-ChildItem Cert:\LocalMachine\My | Where-Object { $_.Subject -eq $Subject } | ForEach-Object { Remove-Item -Path $_.PSPath -DeleteKey -Force }
        if (Test-Path -LiteralPath $InstallDir) { Remove-Item -LiteralPath $InstallDir -Recurse -Force }
        exit 0
    }

    # install: start from a clean state
    foreach ($c in @(Get-TestCerts)) { Remove-Item -Path $c.PSPath -Force }
    Get-ChildItem Cert:\LocalMachine\My | Where-Object { $_.Subject -eq $Subject } | ForEach-Object { Remove-Item -Path $_.PSPath -DeleteKey -Force }

    $step = 'copy the exe to Program Files'
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    $dest = Join-Path $InstallDir $ExeName
    Copy-Item -LiteralPath (Join-Path $SourceDir $ExeName) -Destination $dest -Force
    Unblock-File -LiteralPath $dest -ErrorAction SilentlyContinue

    $step = 'create the test certificate'
    # 1. short-lived, non-exportable code-signing certificate
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $Subject `
                -CertStoreLocation 'Cert:\LocalMachine\My' -KeyExportPolicy NonExportable `
                -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 -NotAfter (Get-Date).AddDays(30)

    $step = 'sign the exe'
    # 2. sign the installed copy
    $sig = Set-AuthenticodeSignature -LiteralPath $dest -Certificate $cert -HashAlgorithm SHA256
    if (-not $sig.SignerCertificate) { throw "Signing failed: $($sig.StatusMessage)" }

    $step = 'trust the certificate'
    # 3. trust the PUBLIC certificate only
    $cer = Join-Path $env:TEMP 'framelens-osd-spike-test.cer'
    Export-Certificate -Cert $cert -FilePath $cer -Force | Out-Null
    foreach ($s in $Stores) { Import-Certificate -FilePath $cer -CertStoreLocation $s | Out-Null }
    Remove-Item $cer -Force

    $step = 'delete the private key'
    # 4. destroy the private key: nothing else can be signed with this trusted certificate
    Remove-Item -Path $cert.PSPath -DeleteKey -Force

    $step = 'verify the signature'
    # 5. verify
    $check = Get-AuthenticodeSignature -LiteralPath $dest
    if ($check.Status -ne 'Valid') { throw "Signature verification: $($check.Status) - $($check.StatusMessage)" }

    exit 0
}
catch {
    Write-Result ("Step failed: $step`r`n" + $_.Exception.GetType().FullName + ': ' + $_.Exception.Message + "`r`nat " + $_.InvocationInfo.PositionMessage)
    exit 2
}
