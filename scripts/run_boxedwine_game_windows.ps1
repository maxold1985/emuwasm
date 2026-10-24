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

$commonArguments = @(
    '-root', [IO.Path]::GetFullPath($ProfileDirectory),
    '-zip', $wineZipPath,
    '-mount_drive', $gameRoot, 'd',
    '-w', $guestWorkingDir,
    '-resolution', $Resolution
)
if ($DebugWine) {
    $commonArguments += @('-env', 'WINEDEBUG=+seh,+loaddll,+virtual')
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
Write-Host "Log: $runLog"
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
    & $BoxedWineExecutable @winXPArguments
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Falha ao configurar Windows XP. Consulte $(Join-Path $LogDirectory 'wine-set-winxp.log')"
        exit $LASTEXITCODE
    }
}
# Launch the actual upstream Wine runtime. Game compatibility is unverified.
& $BoxedWineExecutable @arguments
$gameExitCode = $LASTEXITCODE
if ($null -ne $gameExitCode -and $gameExitCode -ne 0) {
    Write-Warning "Prism3D/BoxedWine finalizou com codigo $gameExitCode"
    if (Test-Path -LiteralPath $runLog -PathType Leaf) {
        Write-Host "Ultimas 50 linhas do log $runLog" -ForegroundColor Yellow
        Get-Content -LiteralPath $runLog -Tail 50 | ForEach-Object { Write-Host $_ }
        $errors = @(Select-String -LiteralPath $runLog -Pattern 
