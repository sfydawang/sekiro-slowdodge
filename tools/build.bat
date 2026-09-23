@echo off
setlocal
set GXX=C:\Users\47297\AppData\Local\Programs\mingw64\bin\g++.exe
cd /d "%~dp0"

echo compiling hook.dll ...
%GXX% -shared -O2 -static -static-libgcc -static-libstdc++ -Wall -o hook.dll hook.cpp -lkernel32 || goto :err

echo.
echo OK  -^> %CD%\hook.dll
exit /b 0

:err
echo.
echo BUILD FAILED
exit /b 1
