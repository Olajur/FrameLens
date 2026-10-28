<#
.SYNOPSIS
    One-time setup for FrameLens frame capture: adds (or removes) the current
    user to the built-in "Performance Log Users" group.

.DESCRIPTION
    FrameLens measures FPS with Event Tracing for Windows (ETW). Starting an ETW
    session requires administrator rights OR membership in "Performance Log Users".
    This script grants the group membership so FrameLens itself never runs as admin.

    It changes exactly one thing: membership of the current user in the group with
    the well-known SID S-1-5-32-559. Nothing is installed, no service is created,
    and no other setting is touched.

    The group is addressed by SID, so it works on localized Windows (e.g. Hungarian,
    where the group has a translated name).

.PARAMETER Action
    add     (default) add the current user to the group
    remove  undo: remove the current user from the group
    status  show whether the user is a member and whether it is active yet

.NOTES
    Adding/removing requires one UAC prompt. If you sign in as a standard user and
    type an administrator's credentials, the ORIGINAL user is still the one added.
    The change takes effect after you sign out and back in (or restart).
#>
[CmdletBinding()]
param(
    [ValidateSet('add', 'remove', 'status')]
    [string]$Action = 'add',

    # Internal: passed from the non-elevated instance so the right user is changed.
    [string]$UserSid,
    [switch]$Elevated
)

$ErrorActionPreference = 'Stop'
$GroupSid = 'S-1-5-32-559'   # BUILTIN\Performance Log Users

# Exit codes (used by the .bat launcher)
$EXIT_OK = 0; $EXIT_NOCHANGE = 1; $EXIT_FAILED = 2; $EXIT_CANCELLED = 3

function Resolve-Name([string]$sid) {
    try { (New-Object System.Security.Principal.SecurityIdentifier $sid).Translate([System.Security.Principal.NTAccount]).Value }
    catch { $sid }
}

function Test-IsAdmin {
    $p = New-Object System.Security.Principal.WindowsPrincipal([System.Security.Principal.WindowsIdentity]::GetCurrent())
    $p.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)
}

# Is the group present in the CURRENT logon token (i.e. already effective)?
function Test-ActiveInSession {
    $groups = [System.Security.Principal.WindowsIdentity]::GetCurrent().Groups
    [bool]($groups | Where-Object { $_.Value -eq $GroupSid })
}

# Is the user configured as a member (may not be effective until next sign-in)?
# Returns $true / $false, or $null if it cannot be determined.
function Test-ConfiguredMember([string]$sid) {
    try {
        [bool](Get-LocalGroupMember -SID $GroupSid | Where-Object { $_.SID.Value -eq $sid })
    }
    catch {
        # Get-LocalGroupMember fails on groups containing orphaned/Azure AD SIDs; fall back to net.exe.
        try {
            $groupName = (Resolve-Name $GroupSid).Split('\')[-1]
            $userShort = (Resolve-Name $sid).Split('\')[-1]
            $out = & net.exe localgroup "$groupName" 2>$null
            [bool]($out | Where-Object { $_.Trim().Split('\')[-1] -ieq $userShort })
        }
        catch { $null }
    }
}

if (-not $UserSid) { $UserSid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value }
$userName  = Resolve-Name $UserSid
$groupName = Resolve-Name $GroupSid

Write-Host ''
Write-Host 'FrameLens - ETW access setup' -ForegroundColor Cyan
Write-Host "  User : $userName"
Write-Host "  Group: $groupName ($GroupSid)"
Write-Host ''

if ($Action -eq 'status') {
    $configured = Test-ConfiguredMember $UserSid
    $active = Test-ActiveInSession
    Write-Host ("  Member of group     : {0}" -f $(if ($null -eq $configured) { 'unknown' } elseif ($configured) { 'yes' } else { 'no' }))
    Write-Host ("  Active this session : {0}" -f $(if ($active) { 'yes' } else { 'no (sign out and back in after adding)' }))
    exit $EXIT_OK
}

# Nothing to change: answer without a UAC prompt (the installer runs this on every install).
if (-not $Elevated) {
    $configured = Test-ConfiguredMember $UserSid
    if ($Action -eq 'add' -and $configured -eq $true) {
        Write-Host 'Already a member - nothing to do.' -ForegroundColor Yellow
        if (-not (Test-ActiveInSession)) { Write-Host 'Not active yet in this session: sign out and back in.' }
        exit $EXIT_NOCHANGE
    }
    if ($Action -eq 'remove' -and $configured -eq $false) {
        Write-Host 'Not a member - nothing to do.' -ForegroundColor Yellow
        exit $EXIT_NOCHANGE
    }
}

# --- add / remove need elevation: relaunch once with UAC, passing the original user's SID ---
if (-not (Test-IsAdmin)) {
    if ($Elevated) { Write-Host 'Elevation did not grant administrator rights.' -ForegroundColor Red; exit $EXIT_FAILED }
    Write-Host 'Requesting administrator approval (UAC) for this one change...'
    $psArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"",
              '-Action', $Action, '-UserSid', $UserSid, '-Elevated')
    try {
        $proc = Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" `
                              -ArgumentList $psArgs -Verb RunAs -Wait -PassThru -WindowStyle Hidden
    }
    catch {
        Write-Host 'Cancelled - nothing was changed.' -ForegroundColor Yellow
        exit $EXIT_CANCELLED
    }
    $code = $proc.ExitCode
    switch ($code) {
        0 {
            if ($Action -eq 'add') {
                Write-Host "Done: $userName was added to $groupName." -ForegroundColor Green
                Write-Host 'Sign out and back in (or restart) before starting FrameLens.' -ForegroundColor Green
            } else {
                Write-Host "Done: $userName was removed from $groupName." -ForegroundColor Green
                Write-Host 'The change applies after you sign out and back in.' -ForegroundColor Green
            }
        }
        1 { Write-Host $(if ($Action -eq 'add') { 'Already a member - nothing to do.' } else { 'Not a member - nothing to do.' }) -ForegroundColor Yellow
            if ($Action -eq 'add' -and -not (Test-ActiveInSession)) { Write-Host 'Not active yet in this session: sign out and back in.' } }
        default { Write-Host "Failed (exit code $code). Nothing was changed." -ForegroundColor Red }
    }
    exit $code
}

# --- elevated part: perform exactly one change ---
$haveModule = $false
try { Import-Module Microsoft.PowerShell.LocalAccounts -ErrorAction Stop; $haveModule = $true } catch { }

$shortGroup = $groupName.Split('\')[-1]
try {
    if ($haveModule) {
        if ($Action -eq 'add') {
            try { Add-LocalGroupMember -SID $GroupSid -Member $UserSid }
            catch [Microsoft.PowerShell.Commands.MemberExistsException] { exit $EXIT_NOCHANGE }
        }
        else {
            try { Remove-LocalGroupMember -SID $GroupSid -Member $UserSid }
            catch [Microsoft.PowerShell.Commands.MemberNotFoundException] { exit $EXIT_NOCHANGE }
        }
    }
    else {
        # Fallback for systems without the LocalAccounts module: net.exe with the localized names.
        $before = Test-ConfiguredMember $UserSid
        if ($Action -eq 'add' -and $before) { exit $EXIT_NOCHANGE }
        if ($Action -eq 'remove' -and $before -eq $false) { exit $EXIT_NOCHANGE }
        $verb = if ($Action -eq 'add') { '/add' } else { '/delete' }
        & net.exe localgroup "$shortGroup" "$userName" $verb | Out-Null
        if ($LASTEXITCODE -ne 0) { exit $EXIT_FAILED }
    }
    exit $EXIT_OK
}
catch {
    exit $EXIT_FAILED
}
