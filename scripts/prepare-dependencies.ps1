param([string]$DependencyRoot = (Join-Path $PSScriptRoot '..\.deps'))
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$DependencyRoot = [IO.Path]::GetFullPath($DependencyRoot)
New-Item -ItemType Directory -Force $DependencyRoot | Out-Null
$lock = Get-Content (Join-Path $PSScriptRoot '..\dependencies.lock.json') -Raw | ConvertFrom-Json
$sevenZipCommand = Get-Command 7z.exe -ErrorAction SilentlyContinue
$sevenZip = if ($sevenZipCommand) { $sevenZipCommand.Source } else { 'C:\Program Files\7-Zip\7z.exe' }
if (!(Test-Path $sevenZip)) { throw 'Je potřeba 7-Zip v PATH nebo Program Files\7-Zip.' }

function Get-VerifiedFile($entry) {
    $target = Join-Path $DependencyRoot $entry.name
    if (!(Test-Path $target)) {
        $partial = "$target.part"
        Invoke-WebRequest -Uri $entry.url -OutFile $partial
        $hash = (Get-FileHash $partial -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($hash -ne $entry.sha256) { Remove-Item $partial; throw "SHA256 nesouhlasí: $($entry.name)" }
        Move-Item $partial $target
    }
    if ((Get-FileHash $target -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) {
        throw "Nesprávný existující archiv: $target. Odstraň jej a zkus znovu."
    }
    return $target
}

$qtArchive = Get-VerifiedFile $lock.files.qt_windows
$pdfArchive = Get-VerifiedFile $lock.files.pdfium_win
$qtSource = Get-VerifiedFile $lock.files.qt_source
$qtRoot = Join-Path $DependencyRoot 'qt-6.8.3-msvc2022_64'
$pdfRoot = Join-Path $DependencyRoot 'pdfium-8125-win-x64'
if (!(Test-Path (Join-Path $qtRoot 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    & $sevenZip x -y "-o$qtRoot" $qtArchive | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Rozbalení Qt selhalo.' }
}
# Qt's binary archive needs its prefix relocated; compatible user replacements
# remain permitted. No integrity enforcement is installed in the application.
Set-Content (Join-Path $qtRoot 'bin\qt.conf') "[Paths]`nPrefix=..`n" -Encoding ascii
if (!(Test-Path (Join-Path $pdfRoot 'bin\pdfium.dll'))) {
    New-Item -ItemType Directory -Force $pdfRoot | Out-Null
    & tar -xf $pdfArchive -C $pdfRoot
    if ($LASTEXITCODE -ne 0) { throw 'Rozbalení PDFium selhalo.' }
}
[pscustomobject]@{QtRoot=$qtRoot; PdfiumRoot=$pdfRoot; QtSource=$qtSource}
