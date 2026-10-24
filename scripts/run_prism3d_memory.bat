@echo off
setlocal
cd /d "%~dp0"
echo EmuWasm - Prism3D - BoxedWine master - memoria legada MMU
echo.
echo ATENCAO: Este BAT exige o BoxedWine.exe do workflow de compilacao MASTER.
echo A opcao -disableLinearMemory NAO existe no BoxedWine 26R1.
echo Mantem Windows XP e rastreia excecoes e DLLs sem o canal virtual.
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_prism3d.ps1" -WinXP -DebugWine -LegacyMMU %*
set "status=%ERRORLEVEL%"
echo.
echo Codigo do BoxedWine / Prism3D: %status%
echo Logs em %%LOCALAPPDATA%%\EmuWasm\Logs
echo Use run_prism3d_memory.bat -NoSound para testar sem audio.
echo Use run_prism3d_memory.bat -TraceVirtualMemory para log detalhado de memoria.
pause
exit /b %status%
