[CmdletBinding()]
param(
    [ValidateSet('Test', 'Run')]
    [string]$Mode = 'Test',

    [ValidateSet('None', 'StartServer', 'StartClient', 'LocalLargeCombat', 'MultiplayerLargeCombat')]
    [string]$Benchmark = 'None',

    [ValidateRange(0, 400)]
    [int]$SquadSize = 0,

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [string]$BuildDirectory = '',

    [string]$Generator = ''
)

$ErrorActionPreference = 'Stop'
if ($Benchmark -ne 'None' -and $Mode -ne 'Run') {
    Write-Error 'Run tasks can only be used with -Mode Run.'
    exit 1
}
if ($Mode -eq 'Run' -and $Benchmark -eq 'None') {
    $Benchmark = 'StartClient'
}
if ($SquadSize -gt 0 -and $Benchmark -ne 'MultiplayerLargeCombat') {
    Write-Error '-SquadSize is supported only by MultiplayerLargeCombat.'
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
        Write-Error 'Build directory uses a different CMake generator. Choose another directory with -BuildDirectory.'
        exit 1
    }
    $cachedToolchainEntry = (Select-String -Path $cachePath -Pattern '^CMAKE_TOOLCHAIN_FILE:[^=]+=' | Select-Object -First 1).Line
    if (-not $cachedToolchainEntry) {
        Write-Error 'Build directory was configured without vcpkg. Choose a new directory with -BuildDirectory.'
        exit 1
    }
    $cachedToolchain = $cachedToolchainEntry.Substring($cachedToolchainEntry.IndexOf('=') + 1).Replace('/', '\')
    $expectedToolchain = [System.IO.Path]::GetFullPath($vcpkgToolchain)
    if (-not [string]::Equals($cachedToolchain, $expectedToolchain, [System.StringComparison]::OrdinalIgnoreCase)) {
        Write-Error 'Build directory uses a different CMake toolchain. Choose another build directory.'
        exit 1
    }
}

& $cmakePath @configureArguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

function Get-ExecutablePath([string]$baseName) {
    $executableName = $baseName
    if ($Configuration -eq 'Debug') {
        $executableName += 'd'
    }
    if ($env:OS -eq 'Windows_NT') {
        $executableName += '.exe'
    }
    $matches = @(Get-ChildItem -Path $BuildDirectory -Filter $executableName -File -Recurse)
    $configured = @($matches | Where-Object { $_.Directory.Name -eq $Configuration })
    if ($configured.Count -eq 1) {
        return $configured[0].FullName
    }
    if ($configured.Count -eq 0 -and $matches.Count -eq 1) {
        return $matches[0].FullName
    }
    throw "Could not uniquely locate $executableName under '$BuildDirectory'."
}

if ($Mode -eq 'Test') {
    & $cmakePath --build $BuildDirectory --target all_tests --config $Configuration
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    $testExecutable = Get-ExecutablePath 'all_tests'
    & $testExecutable
    exit $LASTEXITCODE
}

$targets = switch ($Benchmark) {
    'StartServer' { @('voxellancer-server') }
    'StartClient' { @('voxellancer') }
    'LocalLargeCombat' { @('voxellancer-server', 'voxellancer') }
    'MultiplayerLargeCombat' { @('voxellancer-server', 'voxellancer') }
}
foreach ($target in $targets) {
    & $cmakePath --build $BuildDirectory --target $target --config $Configuration
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

if ($Benchmark -eq 'StartServer') {
    $serverExecutable = Get-ExecutablePath 'voxellancer-server'
    & $serverExecutable --scenario battle --listen-port 27801
    exit $LASTEXITCODE
}
if ($Benchmark -eq 'StartClient') {
    $clientExecutable = Get-ExecutablePath 'voxellancer'
    $clientName = [System.IO.Path]::GetFileName($clientExecutable)
    $existingClients = @(Get-CimInstance Win32_Process | Where-Object { $_.Name -ieq $clientName -and $null -ne $_.CommandLine -and $_.CommandLine.IndexOf($clientExecutable, [System.StringComparison]::OrdinalIgnoreCase) -ge 0 })
    $clientId = if ($existingClients.Count -eq 0) { 'client-1' } else { 'client-2' }
    & $clientExecutable -multiplayer-client -benchmark-client-id $clientId
    exit $LASTEXITCODE
}

$clientExecutable = Get-ExecutablePath 'voxellancer'
$serverExecutable = Get-ExecutablePath 'voxellancer-server'
$runId = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds().ToString()
if ($SquadSize -gt 0) {
    $runId += "-squad-$SquadSize"
}
$reportDirectory = Join-Path $repositoryRoot 'benchmark-results'

if ($Benchmark -eq 'LocalLargeCombat') {
    & $clientExecutable `
        -local-singleplayer `
        -benchmark-large-combat `
        -benchmark-run-id $runId `
        -benchmark-client-id integrated-client
    exit $LASTEXITCODE
}

$serverLog = Join-Path $env:TEMP "voxellancer-$runId-server.log"
$serverErrorLog = Join-Path $env:TEMP "voxellancer-$runId-server-error.log"
$serverReport = Join-Path $reportDirectory "large-combat-$runId-server.json"
$serverProcess = $null
$clientProcesses = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
$benchmarkExitCode = 1
$serverArguments = @(
    '--scenario', 'benchmark',
    '--listen-port', '27801',
    '--target-players', '2',
    '--ships-per-team', '400',
    '--benchmark-ticks', '1800',
    '--benchmark-run-id', $runId
)
if ($SquadSize -gt 0) {
    $serverArguments += @('--squad-size', "$SquadSize")
}

function Test-CompleteServerReport {
    if (-not (Test-Path $serverReport)) {
        return $false
    }
    try {
        $report = Get-Content -Raw $serverReport | ConvertFrom-Json
        $complete = $report.run_id -eq $runId
        $complete = $complete -and $report.role -eq 'server'
        $complete = $complete -and $report.target_players -eq 2
        $complete = $complete -and $report.target_simulation_intervals -eq 1800
        $complete = $complete -and $report.measured_simulation_intervals -eq 1800
        $complete = $complete -and $report.squad_size -eq $SquadSize
        return $complete
    } catch {
        return $false
    }
}

try {
    $loopback = [System.Net.IPAddress]::Loopback
    $portInUse = [System.Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpListeners() |
        Where-Object { $_.Port -eq 27801 }
    if ($portInUse) {
        throw 'Port 27801 is already in use; stop the other server before starting this benchmark.'
    }

    $serverProcess = Start-Process `
        -FilePath $serverExecutable `
        -ArgumentList $serverArguments `
        -WorkingDirectory $repositoryRoot `
        -WindowStyle Hidden `
        -RedirectStandardOutput $serverLog `
        -RedirectStandardError $serverErrorLog `
        -PassThru

    $serverReady = $false
    $serverStartDeadline = [DateTime]::UtcNow.AddSeconds(30)
    while ([DateTime]::UtcNow -lt $serverStartDeadline) {
        $serverProcess.Refresh()
        if ($serverProcess.HasExited) {
            $details = if (Test-Path $serverErrorLog) { Get-Content -Raw $serverErrorLog } else { '' }
            throw "Large-combat server exited with code $($serverProcess.ExitCode). $details"
        }
        $listeners = [System.Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpListeners()
        if ($listeners | Where-Object {
            $_.Port -eq 27801 -and ($_.Address.Equals($loopback) -or $_.Address.Equals([System.Net.IPAddress]::Any))
        }) {
            $serverReady = $true
            break
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $serverReady) {
        throw 'Timed out waiting for the large-combat server to listen on port 27801.'
    }

    foreach ($clientId in @('client-1', 'client-2')) {
        $arguments = @(
            '-multiplayer-client',
            '-benchmark-large-combat',
            '-benchmark-run-id', $runId,
            '-benchmark-client-id', $clientId
        )
        $clientProcess = Start-Process `
            -FilePath $clientExecutable `
            -ArgumentList $arguments `
            -WorkingDirectory $repositoryRoot `
            -PassThru
        $clientProcesses.Add($clientProcess)

        $windowReady = $false
        $windowDeadline = [DateTime]::UtcNow.AddSeconds(45)
        while ([DateTime]::UtcNow -lt $windowDeadline) {
            $clientProcess.Refresh()
            if ($clientProcess.HasExited) {
                throw "$clientId exited before its window was ready (exit code $($clientProcess.ExitCode))."
            }
            if ($clientProcess.MainWindowHandle -ne [IntPtr]::Zero) {
                $windowReady = $true
                break
            }
            Start-Sleep -Milliseconds 100
        }
        if (-not $windowReady) {
            throw "Timed out waiting for the $clientId window."
        }
    }

    $clientDeadline = [DateTime]::UtcNow.AddMinutes(5)
    while ($true) {
        $allClientsExited = $true
        foreach ($clientProcess in $clientProcesses) {
            $clientProcess.Refresh()
            if (-not $clientProcess.HasExited) {
                $allClientsExited = $false
            } elseif ($clientProcess.ExitCode -ne 0) {
                throw "A large-combat client exited with code $($clientProcess.ExitCode)."
            }
        }
        $serverProcess.Refresh()
        if ($serverProcess.HasExited -and -not (Test-CompleteServerReport)) {
            $details = if (Test-Path $serverErrorLog) { Get-Content -Raw $serverErrorLog } else { '' }
            throw "Large-combat server exited before producing a complete report (exit code $($serverProcess.ExitCode)). $details"
        }
        if ($allClientsExited) {
            break
        }
        if ([DateTime]::UtcNow -ge $clientDeadline) {
            throw 'Timed out waiting for the two large-combat clients to finish.'
        }
        Start-Sleep -Milliseconds 250
    }

    if (-not $serverProcess.WaitForExit(30000)) {
        throw 'Both clients finished, but the large-combat server did not finish writing its report.'
    }
    if (-not (Test-CompleteServerReport)) {
        $details = if (Test-Path $serverErrorLog) { Get-Content -Raw $serverErrorLog } else { '' }
        throw "Large-combat server did not produce a complete report (exit code $($serverProcess.ExitCode)). $details"
    }
    if ([int]$serverProcess.ExitCode -ne 0) {
        $details = if (Test-Path $serverErrorLog) { Get-Content -Raw $serverErrorLog } else { '' }
        throw "Large-combat server exited with code $($serverProcess.ExitCode). $details"
    }

    foreach ($clientId in @('client-1', 'client-2')) {
        $reportPath = Join-Path $reportDirectory "large-combat-$runId-$clientId.json"
        if (-not (Test-Path $reportPath)) {
            throw "Expected $clientId benchmark report was not written: $reportPath"
        }
    }
    if (-not (Test-CompleteServerReport)) {
        throw "Expected server benchmark report was not written: $serverReport"
    }

    Write-Host "Large-combat benchmark complete (run $runId)."
    Write-Host "Server report: $serverReport"
    Write-Host "Client reports: $(Join-Path $reportDirectory "large-combat-$runId-client-1.json"), $(Join-Path $reportDirectory "large-combat-$runId-client-2.json")"
    $benchmarkExitCode = 0
} catch {
    Write-Error $_
    if (Test-Path $serverLog) {
        Get-Content -Tail 30 $serverLog
    }
    if (Test-Path $serverErrorLog) {
        Get-Content -Tail 30 $serverErrorLog
    }
    $benchmarkExitCode = 1
} finally {
    foreach ($clientProcess in $clientProcesses) {
        $clientProcess.Refresh()
        if (-not $clientProcess.HasExited) {
            Stop-Process -Id $clientProcess.Id -ErrorAction SilentlyContinue
            $clientProcess.WaitForExit(5000) | Out-Null
        }
    }
    if ($null -ne $serverProcess) {
        $serverProcess.Refresh()
        if (-not $serverProcess.HasExited) {
            Stop-Process -Id $serverProcess.Id -ErrorAction SilentlyContinue
            $serverProcess.WaitForExit(5000) | Out-Null
        }
    }
    Remove-Item -LiteralPath $serverLog, $serverErrorLog -ErrorAction SilentlyContinue
}

exit $benchmarkExitCode
