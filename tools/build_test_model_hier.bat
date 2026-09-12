@echo off
rem Builds tools\test_model_hier.exe — headless validation of the FBX
rem parent-node import (links against build\Release\engine.lib).
rem Build the engine first:  cmake --build build --config Release
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "C:\Dev\shadow engine"
rem /MD required: engine.lib is built with MultiThreadedDLL (/MD).
cl /nologo /EHsc /O2 /MD /std:c++17 ^
    /I engine\include /I engine\src /I vcpkg\installed\x64-windows\include /I build\_deps\glfw-src\include ^
    tools\test_model_hier.cpp ^
    /Fe:tools\test_model_hier.exe /Fo:tools\test_model_hier.obj ^
    /link build\Release\engine.lib build\_deps\glfw-build\src\Release\glfw3.lib ^
        vcpkg\installed\x64-windows\lib\glew32.lib ^
        vcpkg\installed\x64-windows\lib\assimp-vc143-mt.lib ^
        opengl32.lib
if %errorlevel% neq 0 exit /b %errorlevel%
echo Built tools\test_model_hier.exe
