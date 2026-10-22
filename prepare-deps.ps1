param()
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    if (-not $env:VULKAN_SDK -or -not (Test-Path "$env:VULKAN_SDK/Bin/glslc.exe")) { throw 'Vulkan SDK not found: install https://vulkan.lunarg.com/sdk/home and open a new terminal (VULKAN_SDK + glslc on PATH are required).' }
    $destination='tools/nesrecomp-src/runner/external/SDL2'
    if ((Test-Path "$destination/lib/x64/SDL2.lib") -and (Test-Path "$destination/lib/x64/SDL2.dll") -and (Test-Path "$destination/include/SDL.h")) { Write-Host 'SDL2 already available.'; return }
    New-Item -ItemType Directory -Force build/dependencies | Out-Null
    $archive='build/dependencies/SDL2-devel-2.30.0-VC.zip'
    Invoke-WebRequest 'https://github.com/libsdl-org/SDL/releases/download/release-2.30.0/SDL2-devel-2.30.0-VC.zip' -OutFile $archive
    $expected='6413358B67F19B5398E902C576518D5350394A863EB53EB27BB9A81E75A36958'
    if((Get-FileHash $archive -Algorithm SHA256).Hash -ne $expected){throw 'Bad SDL2 checksum.'}
    Expand-Archive $archive -DestinationPath build/dependencies -Force
    New-Item -ItemType Directory -Force $destination | Out-Null
    Copy-Item 'build/dependencies/SDL2-2.30.0/*' $destination -Recurse -Force
    Write-Host 'SDL2 2.30.0 installed and verified.'
} finally { Pop-Location }
