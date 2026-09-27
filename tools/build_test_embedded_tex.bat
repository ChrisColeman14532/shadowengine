@echo off
REM Build the embedded-texture lifetime test against the Release engine library.
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
cd /d "C:\Dev\shadow engine"

set VCPKG=vcpkg\installed\x64-windows

cl /nologo /std:c++17 /O2 /MD ^
  /I"engine\include" ^
  /I"%VCPKG%\include" ^
  /I"build\_deps\assimp-src\include" ^
  /I"build\_deps\glfw-src\include" ^
  tools\test_embedded_tex_cleanup.cpp ^
  /Fe:tools\test_embedded_tex.exe ^
  /link /LIBPATH:build\Release build\Release\engine.lib ^
    %VCPKG%\lib\assimp-vc143-mt.lib ^
    %VCPKG%\lib\glew32.lib ^
    opengl32.lib

endlocal
if %errorlevel%==0 echo BUILD OK
