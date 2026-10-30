param(
    [ValidateSet('serial', 'openmp', 'mpi', 'todos')]
    [string] $Implementacao = 'todos',
    [ValidateSet('oficial', 'zoom', 'strong', 'weak', 'todos')]
    [string] $Conjunto = 'todos',
    [ValidateSet('contiguous', 'cyclic', 'both')]
    [string] $MpiStrategy = 'both',
    [Parameter(Mandatory = $true)] [string] $BinDirectory,
    [int] $Repetitions = 5,
    [int] $Warmups = 0,
    [int] $Threads = 8,
    [int] $Processes = 8,
    [int[]] $ScalingCounts = @(1, 2, 4, 8),
    [string] $MpiExec = 'mpiexec.exe',
    [string] $OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'

# --- validação dos parâmetros ---
if ($Repetitions -lt 1 -or $Warmups -lt 0 -or $Threads -lt 1 -or $Processes -lt 1) {
    throw 'Repetitions, threads e processes devem ser positivos; warmups nao pode ser negativo.'
}
$unicos = @($ScalingCounts | Select-Object -Unique)
if (@($ScalingCounts | Where-Object { $_ -notin 1, 2, 4, 8 }).Count -or $unicos.Count -ne @($ScalingCounts).Count) {
    throw 'ScalingCounts deve conter somente 1, 2, 4 ou 8, sem repetir.'
}
if ($Conjunto -in @('strong', 'weak', 'todos') -and 1 -notin $ScalingCounts) {
    throw 'ScalingCounts precisa incluir 1 para calcular as referencias de escalabilidade.'
}

$BinDirectory = (Resolve-Path -LiteralPath $BinDirectory).Path
$exe = @{
    serial = Join-Path $BinDirectory 'benchmark_serial.exe'
    openmp = Join-Path $BinDirectory 'benchmark_openmp.exe'
    mpi    = Join-Path $BinDirectory 'mandelbrot_mpi.exe'
}
$tipos = if ($Implementacao -eq 'todos') { @('serial', 'openmp', 'mpi') } else { @($Implementacao) }
foreach ($t in $tipos) {
    if (-not (Test-Path -LiteralPath $exe[$t] -PathType Leaf)) { throw "Executavel nao encontrado: $($exe[$t])" }
}
if ('mpi' -in $tipos) {
    $launcher = Get-Command $MpiExec -ErrorAction SilentlyContinue
    if (-not $launcher) { throw "mpiexec nao encontrado: $MpiExec" }
    $MpiExec = $launcher.Source
}

# --- pasta de saída ---
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $id = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
    $OutputDirectory = Join-Path $PSScriptRoot "medicoes/execucao-$env:USERNAME-$env:COMPUTERNAME-$id"
}
$root = [System.IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $root) { throw "Recusando sobrescrever resultados: $root" }
New-Item -ItemType Directory -Path (Join-Path $root 'logs') -Force | Out-Null

$serialRows = [System.Collections.Generic.List[object]]::new()
$openmpRows = [System.Collections.Generic.List[object]]::new()
$mpiRows = [System.Collections.Generic.List[object]]::new()
$rankRows = [System.Collections.Generic.List[object]]::new()

# --- funções ---
function Get-Conditions($nome) {
    $limites = @('-2', '1', '-1.5', '1.5')
    switch ($nome) {
        'oficial' { [pscustomobject]@{ name = 'official'; width = 4096; height = 4096; iter = 1000; bounds = $limites; workers = $null } }
        'zoom' {
            [pscustomobject]@{ name = 'zoom'; width = 4096; height = 4096; iter = 5000
                bounds = @('-0.745143887', '-0.742143887', '0.130325904', '0.133325904'); workers = $null }
        }
        'strong' {
            foreach ($n in $ScalingCounts) {
                [pscustomobject]@{ name = 'strong'; width = 4096; height = 4096; iter = 1000; bounds = $limites; workers = $n }
            }
        }
        'weak' {
            $lado = @{ 1 = 4096; 2 = 5793; 4 = 8192; 8 = 11585 }
            foreach ($n in $ScalingCounts) {
                [pscustomobject]@{ name = 'weak'; width = $lado[$n]; height = $lado[$n]; iter = 1000; bounds = $limites; workers = $n }
            }
        }
        'todos' { foreach ($s in 'oficial', 'zoom', 'strong', 'weak') { Get-Conditions $s } }
    }
}

function New-Comando($tipo, $c, $n, $variante) {
    $dim = @("$($c.width)", "$($c.height)", "$($c.iter)") + $c.bounds
    if ($tipo -eq 'serial') { return @{ Exe = $exe.serial; Args = $dim } }
    if ($tipo -eq 'openmp') { return @{ Exe = $exe.openmp; Args = @("$n") + $dim + @('static', '1') } }
    $mpiArgs = @('-n', "$n", $exe.mpi, '--strategy', $variante, '--no-write',
        '--largura', "$($c.width)", '--altura', "$($c.height)", '--max-iter', "$($c.iter)",
        '--re-min', $c.bounds[0], '--re-max', $c.bounds[1], '--im-min', $c.bounds[2], '--im-max', $c.bounds[3])
    return @{ Exe = $MpiExec; Args = $mpiArgs }
}

function Invoke-Logged($cmd, $cmdJson, $log) {
    $texto = & $cmd.Exe @($cmd.Args) 2>&1 | Out-String
    $codigo = $LASTEXITCODE
    Set-Content -LiteralPath $log -Value ("COMMAND_ARGV_JSON: $cmdJson`n" + $texto)
    if ($codigo -ne 0) { throw "Falha ($codigo) ao executar $cmdJson`n$texto" }
    return $texto
}

function Get-Registro($texto, $prefixo, $log) {
    $linha = $texto -split "`r?`n" | Where-Object { $_ -like "$prefixo;*" } | Select-Object -Last 1
    if (-not $linha) { throw "Registro $prefixo ausente: $log" }
    return , ($linha -split ';')
}

function Get-Versao($programa) {
    $c = Get-Command $programa -ErrorAction SilentlyContinue
    if ($c) { return "$(& $c.Source --version 2>&1 | Select-Object -First 1)" }
    return 'unavailable'
}

# --- coleta ---
$indice = 0
foreach ($c in @(Get-Conditions $Conjunto)) {
    # o serial só roda nos cenários official e zoom
    $usar = if ($c.name -in @('official', 'zoom')) { $tipos } else { @($tipos | Where-Object { $_ -ne 'serial' }) }

    foreach ($tipo in $usar) {
        $n = 1
        $variantes = @('serial')
        if ($tipo -eq 'openmp') {
            $n = if ($c.workers) { $c.workers } else { $Threads }
            $variantes = @('static')
        } elseif ($tipo -eq 'mpi') {
            $n = if ($c.workers) { $c.workers } else { $Processes }
            $variantes = if ($MpiStrategy -eq 'both') { @('contiguous', 'cyclic') } else { @($MpiStrategy) }
        }

        foreach ($var in $variantes) {
            $cmd = New-Comando $tipo $c $n $var
            $cmdJson = ConvertTo-Json -InputObject (@($cmd.Exe) + $cmd.Args) -Compress

            for ($w = 1; $w -le $Warmups; $w++) {
                $log = Join-Path $root "logs/$($c.name)_${tipo}_${var}_n${n}_warmup$w.log"
                $null = Invoke-Logged $cmd $cmdJson $log
            }

            for ($rep = 1; $rep -le $Repetitions; $rep++) {
                $indice++
                $nomeLog = '{0:D3}_{1}_{2}_{3}_n{4}_rep{5}.log' -f $indice, $c.name, $tipo, $var, $n, $rep
                $log = Join-Path $root "logs/$nomeLog"
                $texto = Invoke-Logged $cmd $cmdJson $log

                if ($tipo -eq 'serial') {
                    $f = Get-Registro $texto 'SERIAL' $log
                    $serialRows.Add([pscustomobject]@{
                        experiment = $c.name; workers = 1; width = $c.width; height = $c.height
                        max_iter = $c.iter; repetition = $rep; total_s = [double]$f[4]
                        command = $cmdJson; log = $nomeLog })
                } elseif ($tipo -eq 'openmp') {
                    $f = Get-Registro $texto 'OMP' $log
                    $openmpRows.Add([pscustomobject]@{
                        experiment = $c.name; strategy = 'static'; threads = [int]$f[1]
                        width = [int]$f[2]; height = [int]$f[3]; max_iter = [int]$f[4]
                        schedule = $f[5]; chunk = [int]$f[6]; repetition = $rep
                        total_s = [double]$f[7]; compute_min_s = [double]$f[8]
                        compute_mean_s = [double]$f[9]; compute_max_s = [double]$f[10]
                        fbc_maxmin = [double]$f[12]; command = $cmdJson; log = $nomeLog })
                } else {
                    $f = Get-Registro $texto 'RUN' $log
                    $mpiRows.Add([pscustomobject]@{
                        experiment = $c.name; strategy = $var; processes = $n; width = $c.width
                        height = $c.height; max_iter = $c.iter; repetition = $rep
                        total_max_s = [double]$f[6]; compute_min_s = [double]$f[7]
                        compute_max_s = [double]$f[8]; distribution_max_s = [double]$f[9]
                        communication_max_s = [double]$f[10]; gather_max_s = [double]$f[11]
                        write_s = [double]$f[12]; fbc_all = [double]$f[13]
                        fbc_workers = [double]$f[14]; write_ok = [int]$f[15]
                        command = $cmdJson; log = $nomeLog })
                    foreach ($linha in ($texto -split "`r?`n" | Where-Object { $_ -like 'RANK;*' })) {
                        $r = $linha -split ';'
                        $rankRows.Add([pscustomobject]@{
                            experiment = $c.name; strategy = $var; processes = $n; width = $c.width
                            height = $c.height; max_iter = $c.iter; repetition = $rep
                            rank = [int]$r[3]; rows = [long]$r[4]; tasks = [long]$r[5]
                            compute_s = [double]$r[6]; distribution_s = [double]$r[7]
                            communication_s = [double]$r[8]; gather_s = [double]$r[9]
                            total_s = [double]$r[10]; log = $nomeLog })
                    }
                }
            }
        }
    }
}

# --- CSVs ---
$saidas = [ordered]@{
    'serial_raw.csv'    = $serialRows
    'openmp_raw.csv'    = $openmpRows
    'mpi_raw_runs.csv'  = $mpiRows
    'mpi_raw_ranks.csv' = $rankRows
}
foreach ($nome in $saidas.Keys) {
    if ($saidas[$nome].Count) {
        $saidas[$nome] | Export-Csv -LiteralPath (Join-Path $root $nome) -Delimiter ';' -NoTypeInformation -Encoding UTF8
    }
}

# --- metadados ---
$so = [System.Environment]::OSVersion.VersionString
$cpu = 'unavailable'
try {
    $so = (Get-CimInstance Win32_OperatingSystem -ErrorAction Stop | ForEach-Object { "$($_.Caption) $($_.Version)" })
    $cpu = (Get-CimInstance Win32_Processor -ErrorAction Stop | Select-Object -First 1).Name
} catch { }

@("id=$(Split-Path -Leaf $root)", "operator=$env:USERNAME", "computer=$env:COMPUTERNAME",
    "date=$(Get-Date -Format o)", "os=$so", "cpu=$cpu",
    "logical_processors=$([System.Environment]::ProcessorCount)",
    "gcc_version=$(Get-Versao 'gcc.exe')", "mpicc_version=$(Get-Versao 'mpicc.exe')",
    "binary_directory=$BinDirectory", "mpi_launcher=$MpiExec",
    "repetitions=$Repetitions", "warmups=$Warmups", "implementations=$Implementacao",
    "suite=$Conjunto", "threads=$Threads", "processes=$Processes", "mpi_strategy=$MpiStrategy",
    "scaling_counts=$($ScalingCounts -join ',')",
    "serial_runs=$($serialRows.Count)", "openmp_runs=$($openmpRows.Count)",
    "mpi_runs=$($mpiRows.Count)", "mpi_rank_rows=$($rankRows.Count)",
    'mpi_clock=MPI_Wtime', 'mpi_total=maximum rank total_s',
    'timed_allocation=excluded', 'timed_image_write=excluded', 'write_disabled=true',
    'mpi_total_definition=configuration_broadcast_plus_post_allocation_barrier_through_strategy_collection; maximum_rank; excludes_allocation_final_metrics_gather_and_image_write',
    'mpi_compute=local_mandelbrot_kernel_only',
    'mpi_distribution=config_broadcast_plus_local_partition_calculation',
    'mpi_communication=configuration_broadcast_plus_elapsed_inside_MPI_Gatherv_including_collective_wait',
    'mpi_gather=elapsed_MPI_Gatherv; cyclic_also_includes_root_line_reordering',
    'serial_openmp_total=matrix_generation_call_only') |
    Set-Content -LiteralPath (Join-Path $root 'metadata.txt')

Write-Host "Experimentos concluidos: $root"
Write-Host "Serial=$($serialRows.Count) OpenMP=$($openmpRows.Count) MPI=$($mpiRows.Count) ranks=$($rankRows.Count)"