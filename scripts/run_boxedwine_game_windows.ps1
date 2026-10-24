# Native Windows launcher using BoxedWine's COMPLETE Wine filesystem.
# Existing PE DLLs under wine\i386-windows alone are insufficient.
param(
    [string]$GameDirectory = "C:\GOG Games\18 Wheels of Steel Pedal to the Metal",
    [string]$Exe = "prism3d.exe",
    [string]$WineRootZip = "",
    [string]$ProfileDirectory = "",
    [string]$BoxedWineExecutable = "",
    [ValidatePattern('^\d{3,4}x\d{3,4}$')]
    [string]$Resolution = "1024x768",
    [string]$LogDirectory = "",
    [switch]$WinXP,
    [switch]$DebugWine,
    [switch]$TraceVirtualMemory,
    [switch]$LegacyMMU,
    [switch]$NoSound,
    [switch]$Pentium3,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $BoxedWineExecutable) {
    $BoxedWineExecutable = Join-Path $scriptDirectory 'Boxedwine.exe'
}
if (-not (Test-Path -LiteralPath $BoxedWineExecutable -PathType Leaf)) {
    throw "Boxedwine.exe nao encontrado: $BoxedWineExecutable. Baixe o artefato Windows x64."
}
if (-not (Test-Path -LiteralPath $GameDirectory -PathType Container)) {
    throw "Pasta do jogo nao encontrada: $GameDirectory"
}
$gameRoot = (Resolve-Path -LiteralPath $GameDirectory).Path

# Reject absolute paths and parent traversal before passing a guest command.
$parts = $Exe.Replace('/', '\').Split('\')
if (-not $Exe -or [IO.Path]::IsPathRooted($Exe) -or
    $Exe.Contains(':') -or $parts -contains '..' -or $parts -contains '') {
    throw "EXE deve ser um caminho relativo dentro da pasta do jogo."
}
$fullExe = [IO.Path]::GetFullPath((Join-Path $gameRoot $Exe))
$prefix = $gameRoot.TrimEnd('\') + '\'
if (-not $fullExe.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or
    -not (Test-Path -LiteralPath $fullExe -PathType Leaf)) {
    throw "EXE nao encontrado dentro da pasta do jogo: $Exe"
}
if ([IO.Path]::GetExtension($Exe).ToLowerInvariant() -ne '.exe') {
    throw "O programa deve ser um EXE do Windows."
}

# Default BoxedWine Windows filesystem location documented by upstream.
if (-not $WineRootZip) {
    $fileSystems = Join-Path $env:APPDATA 'Boxedwine\FileSystems2'
    if (Test-Path -LiteralPath $fileSystems -PathType Container) {
        $choices = @(Get-ChildItem -LiteralPath $fileSystems -Filter '*.zip' -File)
        if ($choices.Count -eq 1) {
            $WineRootZip = $choices[0].FullName
        } elseif ($choices.Count -gt 1) {
            $priority = @($choices | Where-Object { $_.Name -match 'Wine' })
            if ($priority.Count -eq 1) {
                $WineRootZip = $priority[0].FullName
            } else {
                Write-Host 'ZIPs de Wine existentes:'
                $choices | ForEach-Object { Write-Host (' - ' + $_.FullName) }
                throw 'Selecione um ZIP completo por -WineRootZip "C:\...\Wine.zip".'
            }
        }
    }
}

if (-not $WineRootZip -or -not (Test-Path -LiteralPath $WineRootZip -PathType Leaf)) {
    Write-Warning 'As DLLs wine\i386-windows NAO sao um filesystem Wine completo.'
    Write-Host 'Abra Boxedwine.exe sem argumentos para baixar um Wine filesystem e execute este script novamente.'
    if (-not $DryRun) { & $BoxedWineExecutable }
    exit 2
}
if ([IO.Path]::GetExtension($WineRootZip).ToLowerInvariant() -ne '.zip') {
    throw "O filesystem Wine precisa ser um arquivo ZIP."
}
$wineZipPath = (Resolve-Path -LiteralPath $WineRootZip).Path
if (-not $ProfileDirectory) {
    $ProfileDirectory = Join-Path $env:LOCALAPPDATA 'EmuWasm\Prism3D'
}
if (-not $DryRun) {
    New-Item -ItemType Directory -Path $ProfileDirectory -Force | Out-Null
}

# BoxedWine -mount_drive exports the installed game as guest Windows drive D:.
# Unlike linking Wine DLL exports only, /bin/wine executes Windows APIs.
$guestExe = 'D:\' + $Exe.Replace('/', '\')
$relativeDirectory = [IO.Path]::GetDirectoryName($Exe.Replace('/', '\'))
$guestWorkingDir = if ($relativeDirectory) { '/mnt/drive_d/' + $relativeDirectory.Replace('\', '/') } else { '/mnt/drive_d' }
# Enable per-run diagnostics without deleting existing game saves or Wine config.
# -log is supported by the actual 26R1 Windows binary. Wine debug channels
# are guest environment variables, NOT Windows host environment variables.
if (-not $LogDirectory) {
    $LogDirectory = Join-Path $env:LOCALAPPDATA 'EmuWasm\Logs'
}
$LogDirectory = [IO.Path]::GetFullPath($LogDirectory)
if (-not $DryRun) {
    New-Item -ItemType Directory -Path $LogDirectory -Force | Out-Null
}
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$runLog = Join-Path $LogDirectory ("prism3d-{0}.log" -f $timestamp)
$launcherLog = Join-Path $LogDirectory ("launcher-{0}.log" -f $timestamp)
if (-not $DryRun) {
    # PowerShell creates this log BEFORE calling any guest program.
    # It remains available even if BoxedWine fails before writing its log.
    Set-Content -LiteralPath $launcherLog -Value @(
        "EmuWasm Prism3D Windows launcher"
        "Time: $(Get-Date -Format o)"
        "Game: $fullExe"
        "BoxedWine: $BoxedWineExecutable"
        "Wine filesystem: $wineZipPath"
        "Wine log: $runLog"
        "WindowsXP: $WinXP"
        "LegacyMMU: $LegacyMMU"
        "TraceVirtualMemory: $TraceVirtualMemory"
    ) -Encoding UTF8
}

$commonArguments = @(
    '-root', [IO.Path]::GetFullPath($ProfileDirectory),
    '-zip', $wineZipPath,
    '-mount_drive', $gameRoot, 'd',
    '-w', $guestWorkingDir,
    '-resolution', $Resolution
)
# The +virtual channel generates enormous logs during a 150 MiB allocation
# and adds substantial overhead. Enable it only for explicit tracing.
if ($TraceVirtualMemory) {
    $commonArguments += @('-env', 'WINEDEBUG=+seh,+loaddll,+virtual')
} elseif ($DebugWine) {
    $commonArguments += @('-env', 'WINEDEBUG=+seh,+loaddll')
}
if ($LegacyMMU) {
    # Available in newer BoxedWine master builds. NOT supported by 26R1.
    $commonArguments += '-disableLinearMemory'
}
if ($NoSound) {
    $commonArguments += '-nosound'
}
if ($Pentium3) {
    $commonArguments += '-p3'
}

# Wine 11 normally reports a newer Windows version, whereas this 2004 game
# originally targeted Windows XP. Set the actual Wine registry value rather
# than pretending to change the host Windows version. This changes ONLY
# the dedicated BoxedWine guest profile, not the host registry.
$winXPArguments = @(
    $commonArguments + @(
        '-log', (Join-Path $LogDirectory 'wine-set-winxp.log'),
        '/bin/wine', 'reg', 'add', 'HKCU\Software\Wine',
        '/v', 'Version', '/t', 'REG_SZ', '/d', 'winxp', '/f'
    )
)
$arguments = @(
    $commonArguments + @(
        '-log', $runLog,
        '/bin/wine', $guestExe
    )
)

Write-Host 'BoxedWine x64: iniciando Prism3D com Wine completo / SDL / OpenGL.' -ForegroundColor Cyan
Write-Host "Executavel: $fullExe"
Write-Host "Wine root: $wineZipPath"
Write-Host "Diretorio convidado: $guestWorkingDir"
if ($LegacyMMU) {
    Write-Host 'Memoria: modo legado MMU/guarded cache (BoxedWine master)' -ForegroundColor Yellow
    Write-Warning 'A opcao -disableLinearMemory NAO funciona no BoxedWine 26R1. Use o pacote compilado do master.'
}
Write-Host "Log do jogo: $runLog"
Write-Host "Log do iniciador: $launcherLog"
if (-not (Test-Path -LiteralPath (Join-Path $gameRoot 'base.scs') -PathType Leaf)) {
    Write-Warning 'base.scs nao encontrado na raiz do jogo. Confira se toda a instalacao esta presente.'
}
if (-not (Test-Path -LiteralPath (Join-Path $gameRoot 'data.scs') -PathType Leaf)) {
    Write-Warning 'data.scs nao encontrado na raiz do jogo. Confira os arquivos de dados.'
}
if ($WinXP) {
    Write-Host 'Compatibilidade: configurando Windows XP no registro do Wine convidado.'
    Write-Host ('Comando WinXP: "{0}" {1}' -f $BoxedWineExecutable, ($winXPArguments -join ' '))
}
Write-Host ('Comando: "{0}" {1}' -f $BoxedWineExecutable, ($arguments -join ' '))
if ($DryRun) { exit 0 }

if ($WinXP) {
    $wineRegLog = Join-Path $LogDirectory 'wine-set-winxp.log'
    Add-Content -LiteralPath $launcherLog -Value 'Stage: Windows XP registry setup started'
    & $BoxedWineExecutable @winXPArguments
    $setupExitCode = $LASTEXITCODE

    # The 26R1 upstream console emulator may return status 1 on shutdown
    # even when Wine reg.exe reports success. Do not abort solely for that.
    $registrySucceeded = $false
    if (Test-Path -LiteralPath $wineRegLog -PathType Leaf) {
        $registrySucceeded = [bool](Select-String -LiteralPath $wineRegLog -Pattern 'reg: The operation completed successfully' -SimpleMatch -Quiet)
    }
    Add-Content -LiteralPath $launcherLog -Value @(
        "Stage: Windows XP registry setup returned $setupExitCode"
        "Wine reg.exe completion confirmed: $registrySucceeded"
    )
    if (-not $registrySucceeded) {
        Write-Warning "Windows XP nao confirmado (BoxedWine exit $setupExitCode). Executando o jogo para obter log. Veja $wineRegLog"
    } elseif ($setupExitCode -ne 0) {
        Write-Warning "Wine reg.exe concluiu corretamente, mas BoxedWine retornou $setupExitCode. Continuando para o jogo."
    }
}
# Launch the actual upstream Wine runtime. Game compatibility is unverified.
Add-Content -LiteralPath $launcherLog -Value "Stage: Prism3D launch requested: $(Get-Date -Format o)"
Write-Host "Iniciando /bin/wine $guestExe ..."
try {
    & $BoxedWineExecutable @arguments
    $gameExitCode = $LASTEXITCODE
} catch {
    Add-Content -LiteralPath $launcherLog -Value "Stage: BoxedWine process failed to start: $($_.Exception.Message)"
    throw
}
Add-Content -LiteralPath $launcherLog -Value @(
    "Stage: Prism3D emulator process returned $gameExitCode"
    "Wine game log created: $(Test-Path -LiteralPath $runLog -PathType Leaf)"
)
if (-not (Test-Path -LiteralPath $runLog -PathType Leaf)) {
    Write-Warning "O log nativo do BoxedWine nao foi criado; consulte o log do iniciador: $launcherLog"
}
if ($null -ne $gameExitCode -and $gameExitCode -ne 0) {
    Write-Warning "Prism3D/BoxedWine finalizou com codigo $gameExitCode"
    if (Test-Path -LiteralPath $runLog -PathType Leaf) {
        Write-Host "Ultimas 50 linhas do log $runLog" -ForegroundColor Yellow
        Get-Content -LiteralPath $runLog -Tail 50 | ForEach-Object { Write-Host $_ }
        $patterns = @(
            '\*\*\* ERROR \*\*\*',
            'Unhandled exception',
            'page fault',
            'out of memory',
            "Can't load library",
            'err:module'
        )
        $errors = @(Select-String -LiteralPath $runLog -Pattern $patterns -ErrorAction SilentlyContinue)
        if ($errors.Count -gt 0) {
            Write-Host 'Possiveis causas relevantes:' -ForegroundColor Yellow
            $errors | Select-Object -Last 15 | ForEach-Object { Write-Host $_.Line }
        } else {
            Write-Host 'Nenhum erro explicito encontrado no log. A parada apos memory pool nao prova falta de RAM.'
        }
    }
    Write-Host 'Proximos testes: comparar -LegacyMMU (BoxedWine master), -NoSound e -Pentium3. -TraceVirtualMemory gera log extenso.'
    exit $gameExitCode
}
