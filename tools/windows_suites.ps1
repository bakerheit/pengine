# Runs every headless suite in .\bin on Windows and prints one line per suite
# and a summary; the exit code is the number of suites that did not pass.
#
# tools/test_windows.sh copies this next to the cross-built suites and runs it
# over SSH. By hand, from a PowerShell prompt in the same folder:
#
#   powershell -ExecutionPolicy Bypass -File windows_suites.ps1 -Assets <assets dir>
#
# PowerShell 5.1 has no ForEach-Object -Parallel, so a small loop keeps $Jobs
# suites running at once. Each suite's output goes to logs\<name>.txt.
param(
    [Parameter(Mandatory = $true)][string]$Assets,
    [int]$Jobs = [Environment]::ProcessorCount,
    [int]$TimeoutSeconds = 900
)
$ErrorActionPreference = 'Stop'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$bin = Join-Path $here 'bin'
$logs = Join-Path $here 'logs'
Remove-Item -Recurse -Force $logs -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $logs | Out-Null

# asset_root() takes APRICOT_ASSETS before anything else, and children inherit it.
$env:APRICOT_ASSETS = (Resolve-Path $Assets).Path

$pending = New-Object System.Collections.Queue
Get-ChildItem $bin -Filter *.exe | Sort-Object Name | ForEach-Object { $pending.Enqueue($_) }
$total = $pending.Count
$running = @()
$failed = New-Object System.Collections.ArrayList
$passed = 0

function Finish($run, [string]$verdict) {
    $seconds = '{0,6:N1}s' -f $run.Clock.Elapsed.TotalSeconds
    if ($verdict -eq 'PASS') {
        $script:passed++
        Write-Output ("PASS  {0}  {1}" -f $seconds, $run.Name)
        return
    }
    [void]$script:failed.Add($run.Name)
    Write-Output ("{0}  {1}  {2}" -f $verdict, $seconds, $run.Name)
    # The tail is usually the failing REQUIRE; the whole log stays in logs\.
    Get-Content $run.Out, $run.Err -ErrorAction SilentlyContinue |
        Select-Object -Last 12 | ForEach-Object { Write-Output ("      " + $_) }
}

while ($pending.Count -gt 0 -or $running.Count -gt 0) {
    while ($pending.Count -gt 0 -and $running.Count -lt $Jobs) {
        $exe = $pending.Dequeue()
        $name = [IO.Path]::GetFileNameWithoutExtension($exe.Name)
        $out = Join-Path $logs "$name.txt"
        $err = Join-Path $logs "$name.err.txt"
        $p = Start-Process -FilePath $exe.FullName -WorkingDirectory $here -NoNewWindow -PassThru `
            -RedirectStandardOutput $out -RedirectStandardError $err
        # Without touching Handle now, PowerShell 5.1 can report a null ExitCode later.
        $null = $p.Handle
        $running += [pscustomobject]@{
            Name = $name; Process = $p; Out = $out; Err = $err
            Clock = [Diagnostics.Stopwatch]::StartNew()
        }
    }
    Start-Sleep -Milliseconds 200
    $still = @()
    foreach ($run in $running) {
        if ($run.Process.HasExited) {
            $run.Process.WaitForExit()
            if ($run.Process.ExitCode -eq 0) { Finish $run 'PASS' }
            else { Finish $run ("FAIL(exit {0})" -f $run.Process.ExitCode) }
        } elseif ($run.Clock.Elapsed.TotalSeconds -gt $TimeoutSeconds) {
            Stop-Process -Id $run.Process.Id -Force -ErrorAction SilentlyContinue
            Finish $run 'TIMEOUT'
        } else {
            $still += $run
        }
    }
    $running = $still
}

Write-Output ""
Write-Output ("{0} of {1} suites passed on {2}" -f $passed, $total, $env:COMPUTERNAME)
if ($failed.Count -gt 0) { Write-Output ("not passing: " + ($failed -join ', ')) }
exit $failed.Count
