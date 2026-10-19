param([Parameter(Mandatory=$true)][string]$Executable,[string]$Arguments='', [int]$TimeoutSeconds=60, [string]$Log='build/process')
$ErrorActionPreference='Stop'
$info = New-Object System.Diagnostics.ProcessStartInfo
$info.FileName = $Executable
$info.Arguments = $Arguments
$info.WorkingDirectory = $PWD.Path
$info.UseShellExecute = $false
$info.CreateNoWindow = $true
$info.WindowStyle = 'Hidden'
$info.RedirectStandardOutput = $true
$info.RedirectStandardError = $true
$process = [System.Diagnostics.Process]::Start($info)
$out = $process.StandardOutput.ReadToEndAsync()
$err = $process.StandardError.ReadToEndAsync()
if (-not $process.WaitForExit($TimeoutSeconds*1000)) { $process.Kill(); $process.WaitForExit(); throw "Timeout: $Executable" }
$out.Result | Set-Content -Encoding utf8 "$Log.log"
$err.Result | Set-Content -Encoding utf8 "$Log.err.log"
Write-Output "Exit: $($process.ExitCode); log: $Log.log"
exit $process.ExitCode
