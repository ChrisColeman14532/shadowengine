@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "C:\Dev\shadow engine"
cl /nologo /EHsc /O2 /std:c++17 /I vcpkg\installed\x64-windows\include tools\test_skin.cpp /Fe:tools\test_skin.exe /Fo:tools\test_skin.obj /link vcpkg\installed\x64-windows\lib\assimp-vc143-mt.lib
