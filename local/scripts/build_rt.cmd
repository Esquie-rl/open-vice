@echo off
chcp 65001 >nul
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "CM=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake"
set "RT=C:\Users\MSI\Desktop\GTA VI\GTA6.Xbox.v1.0.2.1\Leonida\runtime"
"%CM%\CMake\bin\cmake.exe" -S "%RT%" -B "%RT%\build" -G Ninja "-DCMAKE_MAKE_PROGRAM=%CM%\Ninja\ninja.exe" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLEONIDA_VULKAN_HEADERS_FETCH=ON -DLEONIDA_REQUIRE_VULKAN=ON -DLEONIDA_SDL3_FETCH=ON || exit /b 1
"%CM%\CMake\bin\cmake.exe" --build "%RT%\build" -j 16 || exit /b 1
echo BUILD_OK
