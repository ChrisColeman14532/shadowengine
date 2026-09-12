@echo off
REM Build the animation-load test against the Release engine library.
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
cd /d "C:\Dev\shadow engine"

set VCPKG=vcpkg\installed\x64-windows

cl /nologo /std:c++17 /O2 /MD ^
  /I"engine\include" ^
  /I"%VCPKG%\include" ^
  /I"_deps\assimp-src\include" ^
  /I"build\_deps\assimp-src\include" ^
  /I"build\_deps\glfw-src\include" ^
  tools\test_anim_load.cpp ^
  /Fe:tools\test_anim_load.exe ^
  /link /LIBPATH:build\Release build\Release\engine.lib ^
    %VCPKG%\lib\assimp-vc143-mt.lib ^
    /LIBPATH:build\_deps\glfw-build\src\Release glfw3.lib ^
    /LIBPATH:"%VCPKG%\lib" glfw3.lib ^
    /LIBPATH:"%VCPKG%\lib" glew32.lib ^
    opengl32.lib

endlocal
if %errorlevel%==0 echo BUILD OK
