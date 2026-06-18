@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >/dev/null
set "VULKAN_SDK=C:\VulkanSDK\1.4.350.0"
set "PATH=C:\Program Files\LLVM\bin;%VULKAN_SDK%\Bin;%PATH%"
cmake -S "E:\Repositories\nhl-legacy-recomp" -B "out\build\win-amd64-vk-ffx" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ "-DCMAKE_PREFIX_PATH=E:/Tools/rexglue-sdk/src/out/install/win-amd64-ffx" -DNHLLEGACY_VULKAN_BACKEND=ON >out\build\_anim_cfg.log 2>&1
if errorlevel 1 (echo CONFIG_FAIL & exit /b 1)
ninja -C "out\build\win-amd64-vk-ffx" "CMakeFiles/nhllegacy.dir/gpu/hooks/anim_capture.cpp.obj" >out\build\_anim_obj.log 2>&1
if errorlevel 1 (echo COMPILE_FAIL & exit /b 1)
echo COMPILE_OK
