param([string]$BuildDirectory = 'build-windows', [switch]$SkipInstaller)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Push-Location $root
try {
    $deps = & "$PSScriptRoot\prepare-dependencies.ps1"
    $env:PATH = "$($deps.QtRoot)\bin;$env:PATH"
    $build = [IO.Path]::GetFullPath((Join-Path $root $BuildDirectory))
    $stage = Join-Path $root 'out\portable'
    $dist = Join-Path $root 'dist'
    & cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64 `
        "-DCMAKE_PREFIX_PATH=$($deps.QtRoot)" "-DPDFIUM_ROOT=$($deps.PdfiumRoot)" -DBUILD_TESTING=ON
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure selhalo.' }
    & cmake --build $build --config Release --parallel 2
    if ($LASTEXITCODE -ne 0) { throw 'Kompilace selhala.' }
    $env:QT_QPA_PLATFORM = 'offscreen'
    & ctest --test-dir $build -C Release --output-on-failure
    $testExitCode = $LASTEXITCODE
    $testLog = Join-Path $build 'airlet-tests.txt'
    if (Test-Path $testLog) { Get-Content $testLog }
    if ($testExitCode -ne 0) { throw 'Testy selhaly; viz podrobný výstup výše.' }
    Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
    if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
    & cmake --install $build --config Release --prefix $stage
    if ($LASTEXITCODE -ne 0) { throw 'Staging selhal.' }
    & "$($deps.QtRoot)\bin\windeployqt.exe" --release --no-compiler-runtime --no-translations --no-opengl-sw `
        --no-system-d3d-compiler --no-quick-import --skip-plugin-types generic --dir $stage "$stage\AirletPDF.exe"
    if ($LASTEXITCODE -ne 0) { throw 'windeployqt selhalo.' }
    & "$($deps.QtRoot)\bin\windeployqt.exe" --release --no-compiler-runtime --no-translations --no-opengl-sw `
        --no-system-d3d-compiler --no-quick-import --skip-plugin-types generic --dir $stage "$stage\AirletUpdate.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Nasazení aktualizačního procesu selhalo.' }
    # App-local MSVC runtime: no machine-wide installer or administrator rights.
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vsRoot) { throw 'Nelze nalézt VS 2022 runtime pro app-local nasazení.' }
    $redist = Get-ChildItem (Join-Path $vsRoot 'VC\Redist\MSVC') -Directory |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (!$redist) { throw 'Chybí redistribuovatelné MSVC runtime knihovny.' }
    $crt = Join-Path $redist.FullName 'x64\Microsoft.VC143.CRT'
    Copy-Item "$crt\*.dll" $stage
    # Ship complete corresponding Qt sources alongside the binaries, including
    # embedded third-party sources/licenses. No written-offer placeholder.
    New-Item -ItemType Directory -Force "$stage\sources" | Out-Null
    Copy-Item $deps.QtSource "$stage\sources\qtbase-everywhere-src-6.8.3.tar.xz"
    Copy-Item "$root\dependencies.lock.json" $stage
    New-Item -ItemType Directory -Force $dist | Out-Null
    Compress-Archive -Path "$stage\*" -DestinationPath "$dist\AirletPDF-0.6.0-x64-portable.zip" -Force
    if (!$SkipInstaller) {
        $iscc = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
        if (!(Test-Path $iscc)) { $iscc = (Get-Command ISCC.exe -ErrorAction Stop).Source }
        & $iscc "/DSourceDir=$stage" "/DOutputDir=$dist" "$root\installer\AirletPDF.iss"
        if ($LASTEXITCODE -ne 0) { throw 'Inno Setup selhal.' }
    }
    if (!$SkipInstaller) {
        $installer = Get-Item "$dist\AirletPDF-Setup-0.6.0-x64.exe"
        $manifest = [ordered]@{
            schema = 1; version = '0.6.0'; installer = [ordered]@{
                name = $installer.Name
                size = $installer.Length
                sha256 = (Get-FileHash $installer.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            }
        }
        [IO.File]::WriteAllText((Join-Path $dist 'airlet-update.json'), ($manifest | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
    }
    Get-ChildItem $dist -File | Where-Object Name -ne 'SHA256SUMS.txt' | ForEach-Object {
        $hash = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash  $($_.Name)"
    } | Set-Content "$dist\SHA256SUMS.txt" -Encoding ascii
    Write-Host "Hotovo: $dist"
} finally { Pop-Location }
