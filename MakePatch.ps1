param(
    [int]$BuildVer = 1,
    [string]$BuildDir = "build\win32\GeneralsMD\Release",
    [string]$PatchDirectoryPath = "Patch"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$buildDir = (Resolve-Path $BuildDir).Path
$patchDirectoryPath = $PatchDirectoryPath

$crcFilesDir = "$patchDirectoryPath/private/genonlineserver/crcfiles"
$installerDir = "$patchDirectoryPath/public_html"
$updaterDir = "$patchDirectoryPath/public_html/updater"
$workingDir = "$patchDirectoryPath/working_dir"

# Check if the patch directory exists  
if (Test-Path $patchDirectoryPath)
{  
   # If it exists, delete all contents  
   Get-ChildItem -Path $patchDirectoryPath -Recurse -Force | Remove-Item -Recurse -Force  
}
else
{  
   # If it does not exist, create the directory  
   New-Item -ItemType Directory -Path $patchDirectoryPath
}

# make our folder structure
New-Item -ItemType Directory -Path "$crcFilesDir" -Force | Out-Null
New-Item -ItemType Directory -Path "$installerDir" -Force | Out-Null
New-Item -ItemType Directory -Path "$updaterDir" -Force | Out-Null
New-Item -ItemType Directory -Path "$workingDir" -Force | Out-Null

function Copy-IfExists {
   param(
      [string]$Path,
      [string]$Destination
   )
   if (Test-Path -LiteralPath $Path) {
      Copy-Item -Path $Path -Destination $Destination -Force
      return $true
   }
   return $false
}

$exePath = Join-Path $buildDir "GeneralsOnlineZH.exe"
if (!(Test-Path -LiteralPath $exePath)) {
   throw "Required build artifact is missing: $exePath"
}

# Copy optional runtime dependencies and required EXE to working dir.
$optionalRuntime = @("libcurl.dll", "zlib1.dll")
foreach ($runtimeName in $optionalRuntime) {
   $runtimePath = Join-Path $buildDir $runtimeName
   if (-not (Copy-IfExists -Path $runtimePath -Destination $workingDir)) {
      Write-Warning "Optional runtime not found, skipping: $runtimePath"
   }
}
Copy-Item -Path $exePath -Destination $workingDir -Force

# package up our patch and installer
$zipPath = Join-Path $workingDir "v$BuildVer.zip"
if (Test-Path -LiteralPath $zipPath) {
   Remove-Item -Path $zipPath -Force
}
Compress-Archive -Path "$workingDir\*" -DestinationPath $zipPath

# Copy the patch to the patch directory  
Copy-Item -Path $zipPath -Destination (Join-Path $installerDir "v$BuildVer.zip") -Force
Copy-Item -Path $zipPath -Destination (Join-Path $updaterDir "v$BuildVer.gopatch") -Force

# copy the exe to crcfiles
Copy-Item -Path $exePath -Destination "$crcFilesDir" -Force

# cleanup working dir
if (Test-Path $workingDir)
{  
   # If it exists, delete all contents  
   Get-ChildItem -Path $workingDir -Recurse -Force | Remove-Item -Recurse -Force  

   # Remove the working directory itself
   Remove-Item -Path $workingDir -Recurse -Force
}
