@echo off
REM gclient runhooks
set PATH=""
set PATH=C:\Python\Python27\;C:\ProgramData\chocolatey\bin;C:\Program Files\Far Manager\;C:\Python\Python37\Scripts\;C:\Python\Python37\;C:\app\client\product\11.2.0\client_1\bin;C:\app\client\product\12.2.0\client_64\bin;C:\Program Files (x86)\Common Files\Intel\Shared Files\cpp\bin\Intel64;C:\msys\64\mingw64;C:\Program Files (x86)\Common Files\Oracle\Java\javapath;C:\Program Files\Far Manager\;C:\Windows\system32;C:\Windows;C:\Windows\System32\Wbem;C:\Windows\System32\WindowsPowerShell\v1.0\;C:\Windows\System32\OpenSSH\;C:\Program Files\Microsoft SQL Server\130\Tools\Binn\;C:\Program Files\dotnet\;C:\Program Files\PuTTY\;;C:\Users\Robert\AppData\Local\Microsoft\WindowsApps;C:\Users\Robert\AppData\Local\Programs\Microsoft VS Code\bin;C:\Users\Robert\AppData\Roaming\npm;C:\Program Files\Fontlab\vfb2ufo;C:\Program Files\MATLAB\R2018b\runtime\win64;C:\Program Files (x86)\Microsoft SQL Server\130\Tools\Binn\;C:\Program Files (x86)\GitExtensions\;C:\WINDOWS\system32;C:\WINDOWS;C:\WINDOWS\System32\Wbem;C:\WINDOWS\System32\WindowsPowerShell\v1.0\;C:\Program Files\nodejs\;C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\;C:\Program Files\LLVM\bin;C:\Program Files\CMake\bin;C:\Program Files\NVIDIA Corporation\Nsight Compute 2019.4.0\;C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v10.1\bin;C:\Program Files (x86)\Windows Kits\8.1\Windows Performance Toolkit\;C:\Program Files\Git\cmd;C:\Program Files (x86)\NVIDIA Corporation\PhysX\Common;C:\Program Files (x86)\Intel\Intel(R) Management Engine Components\DAL;C:\Program Files\Intel\Intel(R) Management Engine Components\DAL;C:\Go\bin;C:\Program Files (x86)\CVSNT\;C:\Program Files (x86)\Nmap;C:\app\client\product\11.2.0.4\client_32\bin;;C:\intelFPGA\modelsim\win64;C:\Program Files (x86)\GeoMedia Objects\Program;C:\Users\Robert\.dotnet\tools;;C:\Users\Robert\go\bin

set LLVM=clang_win=\"C:\Program Files\LLVM\" cc = \"clang\" cxx = \"clang++\"
set ARGS=is_official_build=false is_component_build=true skia_use_system_libjpeg_turbo=false skia_use_system_expat=false skia_use_system_libjpeg_turbo=false skia_use_system_libpng=false skia_use_system_libwebp=false skia_use_system_zlib=false skia_use_system_icu=false skia_use_system_harfbuzz=false 

echo %LLVM% %ARGS% 

cd Skia\depot_tools\skia\ 


echo Release 32 bit:
rem call bin\gn gen out/Release32 --args="is_debug=false extra_cflags=[\"/MD\", \"/GS\"] target_cpu=\"x86\" %LLVM% %ARGS%" 

echo Debug 32 bit:
call  bin\gn gen out/Debug32 --args="is_debug=true extra_cflags=[\"/MDd\", \"/GS\"] target_cpu=\"x86\" %ARGS%"

echo Release 64 bit:
rem call bin\gn gen out/Release64 --args="is_debug=false extra_cflags=[\"/MD\", \"/GS\"]  %LLVM% %ARGS%"

echo Debug 64 bit:
rem call  bin\gn gen out/Debug64 --args="is_debug=true extra_cflags=[\"/MDd\", \"/GS\"] %ARGS%"


rem ninja -C out/Release32
ninja -C out/Debug32
rem ninja -C out/Release64
rem ninja -C out/Debug64