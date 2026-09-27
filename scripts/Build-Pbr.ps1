#Requires -Version 7.0
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [ValidateSet('PBR', 'Editor', 'All')][string]$Target = 'All',
    [switch]$SmokeTest,
    [string]$MSBuildPath
)

$ErrorActionPreference = 'Stop'
$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$logs = Join-Path $repository 'bin-int\build-logs'
New-Item -ItemType Directory -Path $logs -Force | Out-Null

if (!$MSBuildPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $MSBuildPath = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    }
}
if (!$MSBuildPath -or !(Test-Path -LiteralPath $MSBuildPath)) {
    throw 'MSBuild was not found. Install Visual Studio C++ tools or pass -MSBuildPath.'
}

function Invoke-ChildProcess([string]$Program, [string[]]$Arguments, [int]$TimeoutSeconds = 300) {
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Program
    $start.WorkingDirectory = $repository
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }

    # A mixed PATH/Path environment can break MSBuild's compiler child processes.
    $start.Environment.Clear()
    foreach ($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
        $start.Environment[$entry.Key.ToUpperInvariant()] = [string]$entry.Value
    }
    $child = [System.Diagnostics.Process]::new()
    $child.StartInfo = $start
    [void]$child.Start()
    $stdout = $child.StandardOutput.ReadToEndAsync()
    $stderr = $child.StandardError.ReadToEndAsync()
    if (!$child.WaitForExit($TimeoutSeconds * 1000)) {
        $child.Kill($true)
        $child.Dispose()
        throw "Process timed out after $TimeoutSeconds seconds: $Program"
    }
    $result = @{ ExitCode = $child.ExitCode; Output = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult() }
    $child.Dispose()
    return $result
}

$targets = if ($Target -eq 'All') { @('PBR', 'Editor') } else { @($Target) }
foreach ($item in $targets) {
    $project = if ($item -eq 'PBR') { 'Samples\PBR\PBR.vcxproj' } else { 'Samples\Editor\Editor.vcxproj' }
    $arguments = @((Join-Path $repository $project), '/m:1', "/p:Configuration=$Configuration", '/p:Platform=x64', "/p:SolutionDir=$repository\", '/v:minimal')
    $build = Invoke-ChildProcess $MSBuildPath $arguments
    $buildLog = Join-Path $logs "$item-$Configuration-build.log"
    Set-Content -LiteralPath $buildLog -Value $build.Output -Encoding utf8
    if ($build.ExitCode -ne 0) { Write-Output $build.Output; throw "Build failed: $item (log: $buildLog)" }
    Write-Output "$item $Configuration build passed (log: $buildLog)"

    if ($SmokeTest) {
        $folder = if ($item -eq 'PBR') { 'PBR' } else { 'AtomEditor' }
        $executable = if ($item -eq 'PBR') { 'PBR.exe' } else { 'AtomEditor.exe' }
        $program = Join-Path $repository "bin\$Configuration-windows-x86_64\$folder\$executable"
        $smoke = Invoke-ChildProcess $program @('--smoke-frames=60') 60
        $smokeLog = Join-Path $logs "$item-$Configuration-smoke.log"
        Set-Content -LiteralPath $smokeLog -Value $smoke.Output -Encoding utf8
        if ($smoke.ExitCode -ne 0 -or $smoke.Output -notmatch 'PBR_SMOKE frames=60 errors=0 deviceRemoved=0') {
            Write-Output $smoke.Output
            throw "Render smoke test failed: $item (log: $smokeLog)"
        }
        if ($Configuration -eq 'Debug' -and $smoke.Output -notmatch 'validation=1') {
            throw "D3D12 validation was unavailable for the Debug smoke test: $item (log: $smokeLog)"
        }
        $validation = if ($smoke.Output -match 'validation=1') { 'D3D12 validation enabled, zero errors' } else { 'D3D12 validation unavailable' }
        if ($smoke.Output -notmatch 'viewErrors=0 resizeChecks=4 windowResizes=1') {
            throw "Render-view resize and isolation checks failed: $item (log: $smokeLog)"
        }
        if ($smoke.Output -notmatch 'assetErrors=0 assetChecks=6') {
            throw "Asset import, sharing, diagnostics or cancellation checks failed: $item (log: $smokeLog)"
        }
        $expectedUiChecks = if ($item -eq 'Editor') { 12 } else { 0 }
        if ($smoke.Output -notmatch "uiErrors=0 uiChecks=$expectedUiChecks uiExpected=$expectedUiChecks") {
            Write-Output $smoke.Output
            throw "Scene document and command checks failed: $item (log: $smokeLog)"
        }
        Write-Output "$item rendered 60 frames and exited normally ($validation; log: $smokeLog)"
    }
}
