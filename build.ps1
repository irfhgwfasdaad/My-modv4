param(
    [switch]$Debug
)

# Builds SyncsToolkit.dll with CMake/MSVC and packs it into SyncsToolkit.script
# (a zip containing info.json and the dll) for Overstrike.
# SYNC'S TOOLKIT - made by Sync.

$config = if ($Debug) { "Debug" } else { "Release" }

$jsonContent = Get-Content -Raw -Path "info.json" | ConvertFrom-Json
$name = $jsonContent.name

$zipFile = "$name.zip"
$scriptFile = "$name.script"

if (Test-Path $zipFile) { Remove-Item $zipFile -Force }
if (Test-Path $scriptFile) { Remove-Item $scriptFile -Force }

cmake -S . -B build
if ($LASTEXITCODE -ne 0) { exit 1 }
cmake --build ./build --config $config
if ($LASTEXITCODE -ne 0) { exit 1 }

Compress-Archive -Path "info.json" -DestinationPath $zipFile

$dllFiles = Get-ChildItem -Path "build/$config" -Filter "*.dll"
foreach ($dll in $dllFiles) {
    Compress-Archive -Update -Path $dll.FullName -DestinationPath $zipFile
}

Rename-Item -Path $zipFile -NewName $scriptFile -Force
Write-Output "Created $scriptFile using $config."
