@echo off
setlocal
cd /d "%~dp0"
echo EmuWasm - 18 Wheels of Steel / Prism3D via BoxedWine Windows x64
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_prism3d.ps1" %*
set "status=%ERRORLEVEL%"
if not "%status%"=="0" (
    echo.
    echo Falha ou configuracao incompleta. Codigo: %status%
    echo Veja a mensagem acima e confirme que o Wine filesystem completo foi instalado.
)
echo.
pause
exit /b %status%
