param(
    [ValidateSet('5.5', '5.8')][string]$EngineVersion = '5.8',
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_$EngineVersion",
    [string]$ResultsDirectory,
    [string]$CompilerVersion,
    [ValidateRange(1, 2)][int]$MaxParallelActions = 2
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$runId = [Guid]::NewGuid().ToString('N')
$stagingRoot = Join-Path ([IO.Path]::GetTempPath()) "ContentSanitizer-Validation-$runId"
if (-not $ResultsDirectory) { $ResultsDirectory = Join-Path $repoRoot "Saved\Validation\UE$EngineVersion-$runId" }
$resultsRoot = [IO.Path]::GetFullPath($ResultsDirectory)
$projectRoot = Join-Path $stagingRoot 'Host'
$pluginRoot = Join-Path $projectRoot 'Plugins\ContentSanitizer'
$project = Join-Path $projectRoot 'ContentSanitizerTestHost.uproject'
$build = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$editorCmd = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$buildVersion = Join-Path $EngineRoot 'Engine\Build\Build.version'
foreach ($required in @($build, $editorCmd, $buildVersion)) { if (-not (Test-Path -LiteralPath $required)) { throw "Required engine file missing: $required" } }
$version = Get-Content -LiteralPath $buildVersion -Raw | ConvertFrom-Json
if ("$($version.MajorVersion).$($version.MinorVersion)" -ne $EngineVersion) { throw 'Engine version does not match requested lane.' }
# No junctions or writes to the repository plugin, existing test host, or another editor project.
New-Item -ItemType Directory -Path $pluginRoot, $resultsRoot, (Join-Path $projectRoot 'Content') -Force | Out-Null
$buildProcess = $null
$testProcess = $null
$cleanupOkay = $false
try
{
    Copy-Item -LiteralPath (Join-Path $repoRoot 'ContentSanitizer.uplugin') -Destination $pluginRoot
    foreach ($relative in @('Source', 'Config', 'Resources', 'Content'))
    {
        $source = Join-Path $repoRoot $relative
        if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination (Join-Path $pluginRoot $relative) -Recurse }
    }
    Copy-Item -LiteralPath (Join-Path $repoRoot 'Tests\ContentSanitizerTestHost\Config') -Destination $projectRoot -Recurse
    Copy-Item -LiteralPath (Join-Path $repoRoot 'Tests\ContentSanitizerTestHost\Source') -Destination $projectRoot -Recurse
    $hostDescriptor = Get-Content -LiteralPath (Join-Path $repoRoot 'Tests\ContentSanitizerTestHost\ContentSanitizerTestHost.uproject') -Raw | ConvertFrom-Json
    $hostDescriptor.EngineAssociation = $EngineVersion
    $hostDescriptor | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $project -Encoding UTF8
    $buildArguments = "`"$build`" ContentSanitizerTestHostEditor Win64 Development `"-Project=$project`" -NoHotReloadFromIDE -NoEngineChanges -NoEnginePluginChanges -MaxParallelActions=$MaxParallelActions -NoUBA -NoXGE -NoSNDBS -NoFASTBuild"
    if ($CompilerVersion) {
        if ($CompilerVersion -notmatch '^[0-9.]+$') { throw 'CompilerVersion must be a numeric MSVC version.' }
        $buildArguments += " -CompilerVersion=$CompilerVersion"
    }
    # cmd invokes Build.bat only. All filesystem operations remain native PowerShell.
    if ($build -match '[&|<>^\r\n]' -or $project -match '[&|<>^\r\n]') { throw 'Unsupported shell metacharacter in build path.' }
    "UE $EngineVersion ($($version.Changelist))`n$buildArguments" | Set-Content -LiteralPath (Join-Path $resultsRoot 'commands.txt')
    $buildProcess = Start-Process -FilePath $env:ComSpec -ArgumentList "/d /s /c `"$buildArguments`"" -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $resultsRoot 'build.log') -RedirectStandardError (Join-Path $resultsRoot 'build-stderr.log')
    try { $buildProcess.PriorityClass = 'BelowNormal' } catch { Write-Warning 'Could not lower build priority.' }
    $buildProcess.WaitForExit()
    if ($buildProcess.ExitCode -ne 0) { throw "UE $EngineVersion build failed ($($buildProcess.ExitCode)); see build.log. Foreground editors were not touched." }
    $testArguments = "`"$project`" -unattended -nop4 -NullRHI -NoSplash -NoLiveCoding -ExecCmds=`"Automation RunTests ContentSanitizer`" -TestExit=`"Automation Test Queue Empty`" -ReportExportPath=`"$resultsRoot\Automation`" -abslog=`"$resultsRoot\editor.log`""
    "$editorCmd $testArguments" | Add-Content -LiteralPath (Join-Path $resultsRoot 'commands.txt')
    $testProcess = Start-Process -FilePath $editorCmd -ArgumentList $testArguments -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $resultsRoot 'automation-stdout.log') -RedirectStandardError (Join-Path $resultsRoot 'automation-stderr.log')
    try { $testProcess.PriorityClass = 'BelowNormal' } catch { Write-Warning 'Could not lower test priority.' }
    $testProcess.WaitForExit()
    if ($testProcess.ExitCode -ne 0) { throw "UE $EngineVersion automation failed ($($testProcess.ExitCode)); see editor.log." }
    $reportPath = Join-Path $resultsRoot 'Automation\index.json'
    if (-not (Test-Path -LiteralPath $reportPath)) { throw 'Automation report was not produced; cannot claim success.' }
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    $passedCount = [int]$report.succeeded + [int]$report.succeededWithWarnings
    if ($report.failed -gt 0 -or $passedCount -lt 1 -or $report.notRun -gt 0 -or $report.inProcess -gt 0) { throw 'Automation report contains failures or zero passed tests.' }
    $leftovers = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Content') -Recurse -File -ErrorAction SilentlyContinue)
    $leftovers.FullName | Set-Content -LiteralPath (Join-Path $resultsRoot 'fixture-leftovers.txt')
    if ($leftovers.Count) { throw 'Generated fixture files remain after the automation suite.' }
    "Passed: $passedCount (with warnings: $($report.succeededWithWarnings)); failed: $($report.failed); fixture files: $($leftovers.Count)" | Set-Content -LiteralPath (Join-Path $resultsRoot 'result.txt')
}
finally
{
    $fixtureRoot = Join-Path $projectRoot 'Content'
    $leftoverFiles = @()
    if (Test-Path -LiteralPath $fixtureRoot) { $leftoverFiles = @(Get-ChildItem -LiteralPath $fixtureRoot -Recurse -File) }
    $leftoverFiles.FullName | Set-Content -LiteralPath (Join-Path $resultsRoot 'fixture-leftovers.txt')
    # Only remove the unique directory created here after its owned children exit.
    $resolved = [IO.Path]::GetFullPath($stagingRoot)
    $expected = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) "ContentSanitizer-Validation-$runId"))
    if ($resolved -ne $expected -or (Split-Path -Leaf $resolved) -ne "ContentSanitizer-Validation-$runId") { throw 'Staging cleanup boundary mismatch.' }
    $running = ($buildProcess -and -not $buildProcess.HasExited) -or ($testProcess -and -not $testProcess.HasExited)
    if (-not $running -and (Test-Path -LiteralPath $resolved)) { Remove-Item -LiteralPath $resolved -Recurse -Force; $cleanupOkay = $true }
    "Temporary host removed: $cleanupOkay" | Set-Content -LiteralPath (Join-Path $resultsRoot 'cleanup.txt')
    Write-Host "Validation evidence: $resultsRoot"
}
