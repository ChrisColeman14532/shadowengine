@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "C:\Dev\shadow engine"
cl /nologo /EHsc /O2 /I vcpkg\installed\x64-windows\include tools\dump_fbx.cpp /Fe:tools\dump_fbx.exe /Fo:tools\dump_fbx.obj /link vcpkg\installed\x64-windows\lib\assimp-vc143-mt.lib
if errorlevel 1 exit /b 1
set PATH=C:\Dev\shadow engine\vcpkg\installed\x64-windows\bin;%PATH%
if "%~1"=="" goto :run_default
tools\dump_fbx.exe "%~1"
goto :eof
:run_default
tools\dump_fbx.exe "C:\Users\colem\Downloads\Idle.fbx"
