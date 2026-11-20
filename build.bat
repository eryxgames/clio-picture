@echo off
rem CLIo Picture with Microsoft's compiler, from a Developer Command Prompt (x64, or x64 to ARM64):
rem   build.bat        builds build\clio_picture.dll
rem   build.bat test   builds it and checks it against CLIo's values
rem /fp:strict keeps the floats as CLIo's Python rounds them (no fused multiply-add).
if not exist build mkdir build
cl /nologo /O2 /fp:strict /W3 /LD /Iinclude src\noise.c src\dots.c /Fobuild\ /Fe:build\clio_picture.dll || exit /b 1
if /i "%1"=="test" (
  cl /nologo /O2 /fp:strict /W3 /DCLIO_PICTURE_STATIC /Iinclude tests\test_picture.c src\noise.c src\dots.c /Fobuild\ /Fe:build\test_picture.exe || exit /b 1
  build\test_picture.exe || exit /b 1
)
