param(
    [Parameter(Mandatory = $true)][ValidateSet('5.5', '5.8')][string]$EngineVersion,
    [string]$Configuration = 'Development'
)

$repoRoot = Split-Path -Parent $PSScriptRoot
$engineRoot = "C:\Program Files\Epic Games\UE_$($EngineVersion)"
$uat = Join-Path $engineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
$plugin = Join-Path $repoRoot 'ContentSanitizer.uplugin'
$output = Join-Path $repoRoot "dist\UE$($EngineVersion)"

if (-not (Test-Path -LiteralPath $uat)) { throw "RunUAT was not found for UE ${EngineVersion}: $uat" }
if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output -Recurse -Force }
& $uat BuildPlugin "-Plugin=$plugin" "-Package=$output" '-TargetPlatforms=Win64' "-Configuration=$Configuration" -StrictIncludes
if ($LASTEXITCODE -ne 0) { throw "UE $EngineVersion plugin build failed with exit code $LASTEXITCODE." }
Write-Host "UE $EngineVersion plugin package created: $output"
