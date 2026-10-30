param(
    [ValidateSet('pequenos', 'oficial', 'zoom', 'todos')]
    [string] $Modo = 'pequenos',
    [Parameter(Mandatory = $true)]
    [string] $MpiExecutable,
    [Parameter(Mandatory = $true)]
    [string] $SerialExecutable,
    [Parameter(Mandatory = $true)]
    [string] $OpenmpExecutable,
    [string] $MpiExec = 'mpiexec.exe',
    [int] $Processes = 8,
    [string] $OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
foreach ($path in @($MpiExecutable, $SerialExecutable, $OpenmpExecutable, $MpiExec)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $command = Get-Command $path -ErrorAction SilentlyContinue
        if ($null -eq $command) { throw "Arquivo ou comando nao encontrado: $path" }
    }
}
$MpiExecutable = (Resolve-Path -LiteralPath $MpiExecutable).Path
$SerialExecutable = (Resolve-Path -LiteralPath $SerialExecutable).Path
$OpenmpExecutable = (Resolve-Path -LiteralPath $OpenmpExecutable).Path
$MpiExec = (Get-Command $MpiExec -ErrorAction SilentlyContinue).Source
if ([string]::IsNullOrWhiteSpace($MpiExec)) { $MpiExec = (Resolve-Path -LiteralPath $MpiExec).Path }

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $id = 'validacao_{0}_{1}_{2}' -f $env:USERNAME, $env:COMPUTERNAME,
        ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    $projectRoot = Split-Path $PSScriptRoot -Parent
    $OutputDirectory = Join-Path $projectRoot (Join-Path 'execucoes' $id)
}
$root = [System.IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $root) { throw "Recusando reutilizar resultados existentes: $root" }
New-Item -ItemType Directory -Path $root -Force | Out-Null
$rows = [System.Collections.Generic.List[object]]::new()

function Invoke-Saved {
    param([string] $Executable, [string[]] $Arguments, [string] $Work, [string] $Log)
    New-Item -ItemType Directory -Path (Join-Path $Work 'saidas/matrizes'),
        (Join-Path $Work 'saidas/imagens') -Force | Out-Null
    Push-Location $Work
    try {
        $text = & $Executable @Arguments 2>&1 | Out-String
        $code = $LASTEXITCODE
    } finally { Pop-Location }
    $commandArgv = ConvertTo-Json -InputObject (@($Executable) + @($Arguments)) -Compress
    Set-Content -LiteralPath $Log -Value ("COMMAND_ARGV_JSON: $commandArgv`n" + $text)
    if ($code -ne 0) { throw "Falha ($code): $Executable $($Arguments -join ' ')`n$text" }
    return $text
}

function Compare-Outputs {
    param([string] $Case, [string] $Strategy, [int] $ProcessCount,
          [string] $ReferenceWork, [string] $CandidateWork,
          [string] $CandidateBin, [string] $CandidatePgm,
          [ValidateSet('bytes', 'sha256')] [string] $Method)
    $referenceBin = Join-Path $ReferenceWork 'saidas/matrizes/mandelbrot_serial.bin'
    $referencePgm = Join-Path $ReferenceWork 'saidas/imagens/mandelbrot_serial.pgm'
    $bin = Join-Path $CandidateWork (Join-Path 'saidas/matrizes' $CandidateBin)
    $pgm = Join-Path $CandidateWork (Join-Path 'saidas/imagens' $CandidatePgm)
    foreach ($file in @($referenceBin, $referencePgm, $bin, $pgm)) {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Saida ausente: $file" }
    }
    if ($Method -eq 'bytes') {
        $referenceBytes = [System.IO.File]::ReadAllBytes($referenceBin)
        $candidateBytes = [System.IO.File]::ReadAllBytes($bin)
        $binEqual = $referenceBytes.Length -eq $candidateBytes.Length
        if ($binEqual) {
            for ($i = 0; $i -lt $referenceBytes.Length; ++$i) {
                if ($referenceBytes[$i] -ne $candidateBytes[$i]) { $binEqual = $false; break }
            }
        }
        $referenceBytes = [System.IO.File]::ReadAllBytes($referencePgm)
        $candidateBytes = [System.IO.File]::ReadAllBytes($pgm)
        $pgmEqual = $referenceBytes.Length -eq $candidateBytes.Length
        if ($pgmEqual) {
            for ($i = 0; $i -lt $referenceBytes.Length; ++$i) {
                if ($referenceBytes[$i] -ne $candidateBytes[$i]) { $pgmEqual = $false; break }
            }
        }
    } else {
        $binEqual = (Get-FileHash -LiteralPath $referenceBin -Algorithm SHA256).Hash -eq
                    (Get-FileHash -LiteralPath $bin -Algorithm SHA256).Hash
        $pgmEqual = (Get-FileHash -LiteralPath $referencePgm -Algorithm SHA256).Hash -eq
                    (Get-FileHash -LiteralPath $pgm -Algorithm SHA256).Hash
    }
    $rows.Add([pscustomobject]@{
        case = $Case; strategy = $Strategy; processes = $ProcessCount
        comparison = $Method; matrix_matches_serial = $binEqual
        pgm_matches_serial = $pgmEqual
        serial_matrix_sha256 = (Get-FileHash -LiteralPath $referenceBin -Algorithm SHA256).Hash
        candidate_matrix_sha256 = (Get-FileHash -LiteralPath $bin -Algorithm SHA256).Hash
        result = if ($binEqual -and $pgmEqual) { 'PASS' } else { 'FAIL' }
    })
    if (-not $binEqual -or -not $pgmEqual) { throw "Diferenca de saida em $Case/$Strategy/$ProcessCount." }
}

function Invoke-SmallSuite {
    $cases = @(
        [pscustomobject]@{ name='boundary_3x3'; width=3; height=3; iter=5; processes=@(1,2,3); bounds=@('--re-min','-2','--re-max','2','--im-min','-2','--im-max','2') },
        [pscustomobject]@{ name='single_pixel'; width=1; height=1; iter=1; processes=@(1,2,4); bounds=@('--re-min','3','--re-max','4','--im-min','0','--im-max','1') },
        [pscustomobject]@{ name='one_column_1x7'; width=1; height=7; iter=25; processes=@(2,3); bounds=@('--re-min','-0.7','--re-max','0.7','--im-min','-1','--im-max','1') },
        [pscustomobject]@{ name='nondivisible_11x17'; width=11; height=17; iter=80; processes=@(2,3,4,8); bounds=@('--re-min','-0.81','--re-max','-0.71','--im-min','0.05','--im-max','0.15') }
    )
    foreach ($case in $cases) {
        $caseRoot = Join-Path $root $case.name
        $serialWork = Join-Path $caseRoot 'serial'
        $args = @('--largura',"$($case.width)",'--altura',"$($case.height)",'--max-iter',"$($case.iter)") + $case.bounds
        Invoke-Saved $SerialExecutable $args $serialWork (Join-Path $caseRoot 'serial.log') | Out-Null
        foreach ($strategy in @('contiguous','cyclic')) {
            foreach ($p in $case.processes) {
                $work = Join-Path $caseRoot "mpi_${strategy}_np$p"
                $log = Join-Path $work 'mpi.log'
                $mpiArgs = @('-n',"$p",$MpiExecutable,'--strategy',$strategy) + $args
                Invoke-Saved $MpiExec $mpiArgs $work $log | Out-Null
                Compare-Outputs $case.name $strategy $p $serialWork $work `
                    "mandelbrot_mpi_${strategy}_$($case.width)x$($case.height)_np$p.bin" `
                    "mandelbrot_mpi_${strategy}_$($case.width)x$($case.height)_np$p.pgm" bytes
            }
        }
    }
}

function Invoke-FullCase {
    param([string] $Name, [int] $Width, [int] $Height, [int] $Iterations,
          [string[]] $Bounds)
    $caseRoot = Join-Path $root $Name
    $args = @('--largura',"$Width",'--altura',"$Height",'--max-iter',"$Iterations") + $Bounds
    $serialWork = Join-Path $caseRoot 'serial'
    Invoke-Saved $SerialExecutable $args $serialWork (Join-Path $caseRoot 'serial.log') | Out-Null

    $ompWork = Join-Path $caseRoot 'openmp_static_t8'
    Invoke-Saved $OpenmpExecutable (@('--threads','8','--schedule','static','--chunk','1') + $args) `
        $ompWork (Join-Path $ompWork 'openmp.log') | Out-Null
    Compare-Outputs $Name 'openmp_static' 8 $serialWork $ompWork `
        'mandelbrot_openmp_static.bin' 'mandelbrot_openmp_static.pgm' sha256

    foreach ($strategy in @('contiguous','cyclic')) {
        $work = Join-Path $caseRoot "mpi_${strategy}_np$Processes"
        $mpiArgs = @('-n',"$Processes",$MpiExecutable,'--strategy',$strategy) + $args
        Invoke-Saved $MpiExec $mpiArgs $work (Join-Path $work 'mpi.log') | Out-Null
        Compare-Outputs $Name $strategy $Processes $serialWork $work `
            "mandelbrot_mpi_${strategy}_${Width}x${Height}_np$Processes.bin" `
            "mandelbrot_mpi_${strategy}_${Width}x${Height}_np$Processes.pgm" sha256
    }
}

if ($Modo -in @('pequenos','todos')) { Invoke-SmallSuite }
if ($Modo -in @('oficial','todos')) {
    Invoke-FullCase 'official' 4096 4096 1000 @('--re-min','-2','--re-max','1','--im-min','-1.5','--im-max','1.5')
}
if ($Modo -in @('zoom','todos')) {
    Invoke-FullCase 'zoom' 4096 4096 5000 @('--re-min','-0.745143887','--re-max','-0.742143887','--im-min','0.130325904','--im-max','0.133325904')
}

$rows | Export-Csv -LiteralPath (Join-Path $root 'validacao.csv') -Delimiter ';' -NoTypeInformation -Encoding UTF8
@("mode=$Modo","operator=$env:USERNAME","computer=$env:COMPUTERNAME","date=$(Get-Date -Format o)",
  "mpi_executable=$MpiExecutable","serial_executable=$SerialExecutable",
  "openmp_executable=$OpenmpExecutable","mpi_launcher=$MpiExec","full_case_processes=$Processes") |
    Set-Content -LiteralPath (Join-Path $root 'metadata.txt')
Write-Host "Validacao concluida: $root"
Write-Host "Casos comparados: $($rows.Count)"
