@echo off
setlocal
cd /d "%~dp0"
echo EmuWasm - Prism3D / Pedal to the Metal - modo Windows XP
echo.
echo Alterando apenas a versao informada pelo Wine convidado para Windows XP.
echo O log ficara em %%LOCALAPPDATA%%\EmuWasm\Logs
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_prism3d.ps1" -WinXP -DebugWine %*
set "status=%ERRORLEVEL%"
if not "%status%"=="0" (
    echo.
    echo Codigo de saida: %status%
    echo Tente run_prism3d_winxp.bat -NoSound para isolar o audio.
    echo Tente run_prism3d_winxp.bat -Pentium3 para isolar a CPU emulada.
)
echo.
pause
exit /b %status%
