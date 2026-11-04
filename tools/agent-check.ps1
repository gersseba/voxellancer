[CmdletBinding()]
param(
    [ValidateSet('Test', 'Run')]
    [string]$Mode = 'Test',

    [ValidateSet('None', 'Rendering', 'Combat', 'LargeCombatVsync')]
    [string]$Benchmark = 'None',

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [string]$BuildDirectory = '',

    [string]$Generator = ''
)

$ErrorActionPreference = 'Stop'
if ($Benchmark -ne 'None' -and $Mode -ne 'Run') {
    Write-Error 'Benchmark modes can only be used with -Mode Run.'
    exit 1
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vcpkgRoot = $env:VCPKG_ROOT
if ([string]::IsNullOrWhiteSpace($vcpkgRoot)) {
    Write-Error 'VCPKG_ROOT is not set. Install vcpkg and set VCPKG_ROOT to its root directory.'
    exit 1
}

$vcpkgToolchain = Join-Path $vcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
if (-not (Test-Path $vcpkgToolchain)) {
    Write-Error "The vcpkg toolchain was not found at '$vcpkgToolchain'. Check VCPKG_ROOT."
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

if ([string]::IsNullOrWhiteSpace($Generator) -and $env:OS -eq 'Windows_NT') {
    $vsWhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vsWhere) {
        $visualStudioVersion = & $vsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
        if ($LASTEXITCODE -eq 0 -and $visualStudioVersion -match '^(\d+)\.') {
            $visualStudioMajor = [int]$Matches[1]
            switch ($visualStudioMajor) {
                18 { $Generator = 'Visual Studio 18 2026' }
                17 { $Generator = 'Visual Studio 17 2022' }
                16 { $Generator = 'Visual Studio 16 2019' }
            }
        }
    }
}

$generatorMajor = 0
if ($Generator -match '^Visual Studio (\d+)') {
    $generatorMajor = [int]$Matches[1]
}

if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    if ($generatorMajor -gt 0) {
        $BuildDirectory = "build/agent-vcpkg-vs$generatorMajor-stable"
    } else {
        $BuildDirectory = 'build/agent-vcpkg-stable'
    }
}

if (-not [System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory = Join-Path $repositoryRoot $BuildDirectory
}

Set-Location $repositoryRoot

$configureArguments = @('-S', $repositoryRoot, '-B', $BuildDirectory)
$configureArguments += @('-DCMAKE_TOOLCHAIN_FILE=' + $vcpkgToolchain)
if (-not [string]::IsNullOrWhiteSpace($Generator)) {
    $configureArguments += @('-G', $Generator)
}
if ($generatorMajor -gt 0) {
    $configureArguments += @('-A', 'x64')
}

$cachePath = Join-Path $BuildDirectory 'CMakeCache.txt'
if (Test-Path $cachePath) {
    $cachedGenerator = (Select-String -Path $cachePath -Pattern '^CMAKE_GENERATOR:INTERNAL=' | Select-Object -First 1).Line
    if ($cachedGenerator -and $Generator -and $cachedGenerator -ne "CMAKE_GENERATOR:INTERNAL=$Generator") {
        Write-Error "Build directory uses a different CMake generator. Choose another directory with -BuildDirectory."
        exit 1
    }

    $cachedToolchainEntry = (Select-String -Path $cachePath -Pattern '^CMAKE_TOOLCHAIN_FILE:[^=]+=' | Select-Object -First 1).Line
    if (-not $cachedToolchainEntry) {
        Write-Error "Build directory was configured without vcpkg. Choose a new directory with -BuildDirectory."
        exit 1
    }
    $cachedToolchain = $cachedToolchainEntry.Substring($cachedToolchainEntry.IndexOf('=') + 1).Replace('/', '\')
    $expectedToolchain = [System.IO.Path]::GetFullPath($vcpkgToolchain)
    if (-not [string]::Equals($cachedToolchain, $expectedToolchain, [System.StringComparison]::OrdinalIgnoreCase)) {
        Write-Error "Build directory uses a different CMake toolchain. Choose another directory with -BuildDirectory."
        exit 1
    }
}

& $cmakePath @configureArguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if ($Mode -eq 'Test') {
    & $cmakePath --build $BuildDirectory --target all_tests --config $Configuration
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

& $cmakePath --build $BuildDirectory --target voxellancer --config $Configuration
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$executableBaseName = if ($Mode -eq 'Test') { 'all_tests' } else { 'voxellancer' }
if ($Configuration -eq 'Debug') {
    $executableBaseName += 'd'
}

$testExecutableName = $executableBaseName
if ($env:OS -eq 'Windows_NT') {
    $testExecutableName += '.exe'
}

$testExecutables = @(Get-ChildItem -Path $BuildDirectory -Filter $testExecutableName -File -Recurse)
$configuredExecutables = @($testExecutables | Where-Object { $_.Directory.Name -eq $Configuration })

if ($configuredExecutables.Count -eq 1) {
    $testExecutable = $configuredExecutables[0]
} elseif ($configuredExecutables.Count -eq 0 -and $testExecutables.Count -eq 1) {
    $testExecutable = $testExecutables[0]
} else {
    Write-Error "Could not uniquely locate $testExecutableName under '$BuildDirectory'."
    exit 1
}

$runArguments = @()
if ($Benchmark -eq 'Rendering') {
    $runArguments += '-benchmark-rendering'
} elseif ($Benchmark -eq 'Combat') {
    $runArguments += '-benchmark-combat'
} elseif ($Benchmark -eq 'LargeCombatVsync') {
    $runArguments += '-benchmark-large-combat'
}

& $testExecutable.FullName @runArguments
exit $LASTEXITCODE
