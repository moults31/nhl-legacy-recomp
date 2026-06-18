@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set "VULKAN_SDK=C:\VulkanSDK\1.4.350.0"
set "PATH=%VULKAN_SDK%\Bin;%PATH%"
set "FFXAPI=E:\Tools\rexglue-sdk\src\out\build\win-amd64-ffx\_deps\fidelityfx-src\ffx-api"
cmake -S "%FFXAPI%" -B "%FFXAPI%\build_vk" -G "Visual Studio 17 2022" -A x64 ^
  -DFFX_API_BACKEND=VK_X64 -DFFX_API_AUTO_COMPILE_SHADERS=ON
if errorlevel 1 exit /b 1
cmake --build "%FFXAPI%\build_vk" --config Release --parallel
