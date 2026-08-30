param(
    [ValidateSet('5.8')][string]$EngineVersion = '5.8',
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_$EngineVersion"
)

$repoRoot = Split-Path -Parent $PSScriptRoot
$projectRoot = Join-Path $repoRoot 'Tests\ContentSanitizerTestHost'
$project = Join-Path $projectRoot 'ContentSanitizerTestHost.uproject'
$pluginsDirectory = Join-Path $projectRoot 'Plugins'
$pluginLink = Join-Path $pluginsDirectory 'ContentSanitizer'
$stagingRoot = Join-Path ([IO.Path]::GetTempPath()) ('ContentSanitizer-TestPlugin-' + [Guid]::NewGuid().ToString('N'))
$stagingPlugin = Join-Path $stagingRoot 'ContentSanitizer'
$build = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$editorCmd = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'

if (-not (Test-Path -LiteralPath $build)) { throw "Build.bat was not found: $build" }
if (-not (Test-Path -LiteralPath $editorCmd)) { throw "UnrealEditor-Cmd.exe was not found: $editorCmd" }
if (-not (Test-Path -LiteralPath $project)) { throw "Test host project was not found: $project" }

New-Item -ItemType Directory -Path $pluginsDirectory -Force | Out-Null
if (Test-Path -LiteralPath $pluginLink)
{
    throw "Test plugin link already exists; remove it before running this script: $pluginLink"
}

try
{
    New-Item -ItemType Directory -Path $stagingPlugin -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $repoRoot 'ContentSanitizer.uplugin') -Destination $stagingPlugin -Force
    foreach ($relativePath in @('Source', 'Config', 'Resources', 'Content'))
    {
        $sourcePath = Join-Path $repoRoot $relativePath
        if (Test-Path -LiteralPath $sourcePath)
        {
            Copy-Item -LiteralPath $sourcePath -Destination (Join-Path $stagingPlugin $relativePath) -Recurse -Force
        }
    }

    $createdLink = New-Item -ItemType Junction -Path $pluginLink -Target $stagingPlugin
    if ($createdLink.Target -notcontains $stagingPlugin)
    {
        throw "Test plugin junction did not resolve to the staged plugin directory."
    }

    & $build UnrealEditor Win64 Development "-Project=$project" -WaitMutex -NoHotReload
    if ($LASTEXITCODE -ne 0) { throw "UE $EngineVersion test host build failed with exit code $LASTEXITCODE." }

    & $editorCmd $project -unattended -nop4 -NullRHI '-ExecCmds=Automation RunTests ContentSanitizer; Quit' '-TestExit=Automation Test Queue Empty' -stdout -FullStdOutLogOutput -NoSplash -NoSound
    if ($LASTEXITCODE -ne 0) { throw "ContentSanitizer automation failed with exit code $LASTEXITCODE." }
}
finally
{
    if (Test-Path -LiteralPath $pluginLink)
    {
        $linkItem = Get-Item -LiteralPath $pluginLink -Force
        if (($linkItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0)
        {
            throw "Refusing to remove a non-junction test plugin path: $pluginLink"
        }
        Remove-Item -LiteralPath $pluginLink -Force
    }
    if (Test-Path -LiteralPath $stagingRoot)
    {
        Remove-Item -LiteralPath $stagingRoot -Recurse -Force
    }
}
