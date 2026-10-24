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
$arguments = @(
    '-root', [IO.Path]::GetFullPath($ProfileDirectory),
    '-zip', $wineZipPath,
    '-mount_drive', $gameRoot, 'd',
    '-w', $guestWorkingDir,
    '-resolution', $Resolution,
    '/bin/wine', $guestExe
)
Write-Host 'BoxedWine x64: iniciando Prism3D com Wine completo / SDL / OpenGL.'
Write-Host "Executavel: $fullExe"
Write-Host "Wine root: $wineZipPath"
Write-Host ('Comando: "{0}" {1}' -f $BoxedWineExecutable, ($arguments -join ' '))
if ($DryRun) { exit 0 }

# This is the original upstream Wine runtime; game compatibility is unverified.
& $BoxedWineExecutable @arguments
if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
    Write-Warning "BoxedWine finalizou com codigo $LASTEXITCODE"
    exit $LASTEXITCODE
}
