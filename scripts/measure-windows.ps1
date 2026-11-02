param(
    [Parameter(Mandatory)][string]$Pdf,
    [string]$Exe = "$PSScriptRoot\..\out\portable\AirletPDF.exe",
    [ValidateSet('cold-after-reboot','warm')][string]$RunKind='warm',
    [int]$IdleSeconds=10
)
$ErrorActionPreference='Stop'
$Exe=[IO.Path]::GetFullPath($Exe);$Pdf=[IO.Path]::GetFullPath($Pdf)
$output=Join-Path $PSScriptRoot '..\measurements'
New-Item -ItemType Directory -Force $output | Out-Null
$id=Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$metrics=[IO.Path]::GetFullPath((Join-Path $output "$id-start.json"))
$clock=[Diagnostics.Stopwatch]::StartNew()
$p=Start-Process -FilePath $Exe -ArgumentList @("`"$Pdf`"","`"--metrics-file=$metrics`"") -PassThru
try {
    # This polling is an external benchmark harness, never part of Airlet PDF.
    while(!(Test-Path $metrics)) {
        if($clock.Elapsed.TotalSeconds -gt 60 -or $p.HasExited){throw 'První obraz nebyl zaznamenán do 60 s.'}
        Start-Sleep -Milliseconds 25;$p.Refresh()
    }
    $observed=$clock.Elapsed.TotalMilliseconds
    Start-Sleep -Seconds 3 # let visible sharp tiles and one neighbour finish
    $p.Refresh();$cpu=$p.TotalProcessorTime.TotalMilliseconds
    Start-Sleep -Seconds $IdleSeconds
    $p.Refresh();$idleCpu=$p.TotalProcessorTime.TotalMilliseconds-$cpu
    $record=[ordered]@{
        kind=$RunKind;startup=(Get-Content $metrics -Raw|ConvertFrom-Json)
        externally_observed_first_image_ms=$observed
        idle_window_seconds=$IdleSeconds;idle_process_cpu_ms=$idleCpu
        idle_cpu_one_core_percent=100*$idleCpu/($IdleSeconds*1000)
        working_set_bytes=$p.WorkingSet64;private_bytes=$p.PrivateMemorySize64
        os=[Environment]::OSVersion.VersionString
        cpu=(Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty Name)
        ram_bytes=(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory
        gpu=@(Get-CimInstance Win32_VideoController | Select-Object -ExpandProperty Name)
    }
    $json=Join-Path $output "$id-$RunKind.json"
    $record | ConvertTo-Json -Depth 6 | Set-Content $json -Encoding utf8
    Write-Host "Záznam: $json (bez cesty či obsahu PDF)"
} finally {
    if(!$p.HasExited){$null=$p.CloseMainWindow();if(!$p.WaitForExit(15000)){Write-Warning 'Ukončení trvá déle než 15 s; ověřte dekódování stránky.'}}
}
