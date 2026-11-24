# Compatibility wrapper for tools/agent-check.cmake, the platform-independent
# harness. Prefer running `cmake -P tools/agent-check.cmake` directly.
[CmdletBinding()]
param(
    [ValidateSet('Test', 'Run')]
    [string]$Mode = 'Test',

    [ValidateSet('None', 'StartServer', 'StartClient', 'LocalLargeCombat', 'MultiplayerLargeCombat')]
    [string]$Benchmark = 'None',

    [ValidateSet('None', 'ShieldShowcase')]
    [string]$Scenario = 'None',

    [ValidateRange(0, 400)]
    [int]$SquadSize = 0,

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [string]$BuildDirectory = '',

    [string]$Generator = ''
)

$ErrorActionPreference = 'Stop'
if ($Benchmark -ne 'None' -and $Scenario -ne 'None') {
    Write-Error '-Scenario cannot be combined with -Benchmark.'
    exit 1
}

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) {
    $cmakePath = $cmakeCommand.Source
} else {
    $cmakePath = Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe'
    if (-not (Test-Path $cmakePath)) {
        Write-Error 'CMake was not found on PATH or in the standard Program Files location.'
        exit 1
    }
}

$arguments = @("-DMODE=$Mode", "-DCONFIG=$Configuration", "-DSQUAD_SIZE=$SquadSize")
if ($Benchmark -ne 'None') {
    $arguments += "-DRUN=$Benchmark"
} elseif ($Scenario -ne 'None') {
    $arguments += "-DRUN=$Scenario"
}
if (-not [string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $arguments += "-DBUILD_DIR=$BuildDirectory"
}
if (-not [string]::IsNullOrWhiteSpace($Generator)) {
    $arguments += "-DGENERATOR=$Generator"
}
$arguments += @('-P', (Join-Path $PSScriptRoot 'agent-check.cmake'))

& $cmakePath @arguments
exit $LASTEXITCODE
