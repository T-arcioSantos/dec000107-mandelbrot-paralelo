param(
    [ValidateSet('compilar', 'validar', 'experimentos', 'todos')]
    [string] $Modo = 'todos',
    [ValidateSet('pequenos', 'oficial', 'zoom', 'todos')]
    [string] $Validacao = 'todos',
    [ValidateSet('serial', 'openmp', 'mpi', 'todos')]
    [string] $Implementacao = 'todos',
    [ValidateSet('oficial', 'zoom', 'strong', 'weak', 'todos')]
    [string] $Conjunto = 'todos',
    [ValidateSet('contiguous', 'cyclic', 'both')]
    [string] $MpiStrategy = 'both',
    [string] $BinDirectory = '',
    [string] $DiretorioExecucao = '',
    [string] $Operador = $env:USERNAME,
    [int] $Repetitions = 5,
    [int] $Warmups = 0,
    [int] $Threads = 8,
    [int] $Processes = 8,
    [string] $MpiExec = 'mpiexec.exe'
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path $PSScriptRoot).Path
$pwsh = Join-Path $PSHOME 'pwsh.exe'
$operatorId = ($Operador -replace '[^a-zA-Z0-9_-]', '_')
$machineId = if ($env:COMPUTERNAME) { $env:COMPUTERNAME } else { 'host-indisponivel' }
$invocationId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$allowedRunRoots = @(
    [System.IO.Path]::GetFullPath((Join-Path $projectRoot 'execucoes')),
    [System.IO.Path]::GetFullPath((Join-Path $projectRoot 'resultados'))
)
if ($DiretorioExecucao) {
    $runRoot = if ([System.IO.Path]::IsPathRooted($DiretorioExecucao)) {
        [System.IO.Path]::GetFullPath($DiretorioExecucao)
    } else {
        [System.IO.Path]::GetFullPath((Join-Path $projectRoot $DiretorioExecucao))
    }
    $allowed = $false
    foreach ($allowedRoot in $allowedRunRoots) {
        $relativeRunPath = [System.IO.Path]::GetRelativePath($allowedRoot, $runRoot)
        if ($relativeRunPath -ne '.' -and -not [System.IO.Path]::IsPathRooted($relativeRunPath) -and
            $relativeRunPath -ne '..' -and
            -not $relativeRunPath.StartsWith("..$([System.IO.Path]::DirectorySeparatorChar)")) {
            $allowed = $true
        }
    }
    if (-not $allowed) { throw 'DiretorioExecucao deve ficar dentro de execucoes ou resultados.' }
} else {
    $runRoot = Join-Path $allowedRunRoots[0] "execucao-$operatorId-$machineId-$invocationId"
}New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $runRoot 'logs'),
    (Join-Path $runRoot 'metadata') -Force | Out-Null

$osText = [System.Environment]::OSVersion.VersionString
$cpuText = 'unavailable'
try {
    $osInfo = Get-CimInstance Win32_OperatingSystem -ErrorAction Stop
    $osText = "$($osInfo.Caption) $($osInfo.Version)"
} catch { }
try {
    $cpuInfo = Get-CimInstance Win32_Processor -ErrorAction Stop | Select-Object -First 1
    if ($cpuInfo) { $cpuText = $cpuInfo.Name }
} catch { }
if ($cpuText -eq 'unavailable') {
    try {
        $cpuText = (Get-ItemProperty -LiteralPath 'HKLM:\HARDWARE\DESCRIPTION\System\CentralProcessor\0' `
            -Name ProcessorNameString -ErrorAction Stop).ProcessorNameString.Trim()
    } catch { }
}
$gccCandidates = @(Get-Command gcc.exe -All -ErrorAction SilentlyContinue |
    ForEach-Object { $_.Source } |
    Where-Object { $_ -match '[\\/]ucrt64[\\/]bin[\\/]gcc\.exe$' })
if ($env:MSYS2_ROOT) { $gccCandidates += Join-Path $env:MSYS2_ROOT 'ucrt64\bin\gcc.exe' }
$gccCandidates += 'C:\msys64\ucrt64\bin\gcc.exe'
$gccPath = $gccCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
$ucrtBin = if ($gccPath) { Split-Path $gccPath -Parent } else { 'unavailable' }
if ($gccPath) { $env:PATH = $ucrtBin + [System.IO.Path]::PathSeparator + $env:PATH }
$mpiccPath = 'not used (GCC UCRT64 links directly to the MS-MPI SDK)'
$gccVersion = if ($gccPath) { (& $gccPath --version 2>&1 | Select-Object -First 1) } else { 'unavailable' }
$mpiVersion = 'unavailable'
$mpiCommand = Get-Command $MpiExec -ErrorAction SilentlyContinue
if ($mpiCommand) {
    $MpiExec = $mpiCommand.Source
    try {
        $version = (Get-Item -LiteralPath $MpiExec).VersionInfo.ProductVersion
        if ($version) { $mpiVersion = $version }
    } catch { }
}
$environmentMetadata = Join-Path $runRoot 'metadata/environment.txt'
if (-not (Test-Path -LiteralPath $environmentMetadata)) {
    @("execution=$(Split-Path -Leaf $runRoot)", "operator=$Operador", "computer=$machineId",
      "date=$(Get-Date -Format o)", "os=$osText", "cpu=$cpuText",
      "logical_processors=$([System.Environment]::ProcessorCount)", "gcc_path=$gccPath",
      "gcc_version=$gccVersion", "ucrt_runtime_bin=$ucrtBin", "mpicc_path=$mpiccPath", "mpi_launcher=$MpiExec",
      "mpi_launcher_version=$mpiVersion", "powershell=$($PSVersionTable.PSVersion)") |
        Set-Content -LiteralPath $environmentMetadata
}

function Invoke-Step {
    param([string] $ScriptPath, [string[]] $Arguments, [string] $LogName)
    $logPath = Join-Path $runRoot "logs/${LogName}_$invocationId.log"
    Write-Host "Executando $LogName..."
    & $pwsh -NoProfile -File $ScriptPath @Arguments 2>&1 | Tee-Object -FilePath $logPath
    if ($LASTEXITCODE -ne 0) { throw "Etapa $LogName falhou (codigo $LASTEXITCODE). Consulte $logPath" }
}

$compileScript = Join-Path $projectRoot 'compilar.ps1'
$validateScript = Join-Path $projectRoot 'experimentos/validar.ps1'
$experimentScript = Join-Path $projectRoot 'experimentos/coletar.ps1'
$binPath = if ($BinDirectory) { (Resolve-Path -LiteralPath $BinDirectory).Path } else { Join-Path $runRoot 'bin' }

if ($Modo -in @('compilar','validar','experimentos','todos')) {
    if ($Modo -in @('compilar','todos')) {
        Invoke-Step $compileScript @('-OutputDirectory', (Join-Path $runRoot 'bin')) 'compilar'
        $binPath = Join-Path $runRoot 'bin'
        if (-not (Test-Path -LiteralPath (Join-Path $binPath 'build_complete.txt') -PathType Leaf)) {
            throw "Compilacao sem marcador de conclusao: $binPath"
        }
    } else {
        $expectedExecutables = if ($Modo -eq 'validar') {
            @('mandelbrot_mpi.exe','mandelbrot_serial.exe','mandelbrot_openmp.exe')
        } else {
            $needed = @()
            if ($Implementacao -in @('serial','todos')) { $needed += 'benchmark_serial.exe' }
            if ($Implementacao -in @('openmp','todos')) { $needed += 'benchmark_openmp.exe' }
            if ($Implementacao -in @('mpi','todos')) { $needed += 'mandelbrot_mpi.exe' }
            $needed
        }
        if (-not $BinDirectory -and -not (Test-Path -LiteralPath $binPath -PathType Container)) {
            Invoke-Step $compileScript @('-OutputDirectory', $binPath) 'compilar'
            if (-not (Test-Path -LiteralPath (Join-Path $binPath 'build_complete.txt') -PathType Leaf)) {
                throw "Compilacao sem marcador de conclusao: $binPath"
            }
        } elseif (-not $BinDirectory -and
                  -not (Test-Path -LiteralPath (Join-Path $binPath 'build_complete.txt') -PathType Leaf)) {
            throw "A pasta bin nao tem build_complete.txt; ela pode conter uma compilacao parcial ou antiga. Informe -BinDirectory explicitamente ou use uma nova execucao."
        }
        $missingExecutables = @($expectedExecutables | Where-Object {
            -not (Test-Path -LiteralPath (Join-Path $binPath $_) -PathType Leaf)
        })
        if ($missingExecutables.Count) {
            throw "Executaveis ausentes em ${binPath}: $($missingExecutables -join ', ')"
        }
    }
}

if ($Modo -in @('validar','todos')) {
    $validationOutput = Join-Path $runRoot 'validacao'
    if (Test-Path -LiteralPath $validationOutput) { $validationOutput = Join-Path $validationOutput "quick-check-$invocationId" }
    Invoke-Step $validateScript @('-Modo', $(if ($Modo -eq 'todos') { 'pequenos' } else { $Validacao }),
        '-MpiExecutable', (Join-Path $binPath 'mandelbrot_mpi.exe'),
        '-SerialExecutable', (Join-Path $binPath 'mandelbrot_serial.exe'),
        '-OpenmpExecutable', (Join-Path $binPath 'mandelbrot_openmp.exe'),
        '-MpiExec', $MpiExec, '-Processes', "$Processes",
        '-OutputDirectory', $validationOutput) 'validar'
}

if ($Modo -in @('experimentos','todos')) {
    $expOutput = Join-Path $runRoot 'dados'
    Invoke-Step $experimentScript @('-Implementacao', $(if ($Modo -eq 'todos') { 'todos' } else { $Implementacao }),
        '-Conjunto', $(if ($Modo -eq 'todos') { 'todos' } else { $Conjunto }),
        '-MpiStrategy', $MpiStrategy,
        '-BinDirectory', $binPath, '-Repetitions', "$Repetitions", '-Warmups', "$Warmups",
        '-Threads', "$Threads", '-Processes', "$Processes", '-MpiExec', $MpiExec,
        '-OutputDirectory', $expOutput) 'experimentos'
}


Write-Host "Execucao: $runRoot"
