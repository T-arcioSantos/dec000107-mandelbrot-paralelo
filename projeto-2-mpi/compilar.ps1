param([string] $OutputDirectory = '')

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path $PSScriptRoot).Path
$gccCandidates = @(Get-Command gcc.exe -All -ErrorAction SilentlyContinue |
    ForEach-Object { $_.Source } |
    Where-Object { $_ -match '[\\/]ucrt64[\\/]bin[\\/]gcc\.exe$' })
if ($env:MSYS2_ROOT) {
    $gccCandidates += Join-Path $env:MSYS2_ROOT 'ucrt64\bin\gcc.exe'
}
$gccCandidates += 'C:\msys64\ucrt64\bin\gcc.exe'
$gcc = $gccCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $gcc) {
    throw 'GCC do ambiente MSYS2 UCRT64 nao encontrado. Adicione ucrt64\bin ao PATH ou defina MSYS2_ROOT.'
}
$ucrtRoot = Split-Path (Split-Path $gcc -Parent) -Parent
$common = Join-Path $projectRoot '..\projeto-1-openmp\codigo\comum'
$serial = Join-Path $projectRoot '..\projeto-1-openmp\codigo\serial\mandelbrot_serial.c'
$openmp = Join-Path $projectRoot '..\projeto-1-openmp\codigo\openmp'
$mpi = Join-Path $projectRoot 'mpi'
$experiments = Join-Path $projectRoot 'experimentos'
$mpiInclude = Join-Path $ucrtRoot 'include'
$mpiLib = Join-Path $ucrtRoot 'lib'
if (-not (Test-Path -LiteralPath (Join-Path $mpiInclude 'mpi.h')) -or
    -not (Test-Path -LiteralPath (Join-Path $mpiLib 'libmsmpi.dll.a'))) {
    throw "SDK MS-MPI ausente no prefixo UCRT64: $ucrtRoot"
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $suffix = [guid]::NewGuid().ToString('N').Substring(0, 8)
    $OutputDirectory = Join-Path 'compilacao' "build-ucrt64-$stamp-$suffix"
}
$out = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
    [IO.Path]::GetFullPath($OutputDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))
}
if (Test-Path -LiteralPath $out) {
    throw "Recusando reutilizar pasta de compilacao existente: $out"
}
New-Item -ItemType Directory -Path $out | Out-Null
$flags = @('-std=c11','-O2','-Wall','-Wextra','-Wpedantic','-Wconversion','-ffp-contract=off',"-I$common")
$targets = @('mandelbrot_serial.exe','mandelbrot_openmp.exe','mandelbrot_mpi.exe','benchmark_serial.exe','benchmark_openmp.exe')
function Build([string[]] $CompilerArgs, [string] $Name) {
    Write-Host "Compilando $Name"
    & $gcc @CompilerArgs
    if ($LASTEXITCODE -ne 0) { throw "Falha ao compilar $Name ($LASTEXITCODE)" }
}
$kernel = Join-Path $common 'mandelbrot.c'
$io = Join-Path $common 'mandelbrot_io.c'
Build ($flags + @('-fopenmp',$kernel,$io,$serial,'-o',(Join-Path $out 'mandelbrot_serial.exe'),'-lm','-fopenmp')) 'Serial UCRT64'
Build ($flags + @('-fopenmp',"-I$openmp",$kernel,$io,(Join-Path $openmp 'mandelbrot_openmp.c'),(Join-Path $openmp 'mandelbrot_openmp_main.c'),'-o',(Join-Path $out 'mandelbrot_openmp.exe'),'-lm','-fopenmp')) 'OpenMP UCRT64'
Build ($flags + @("-I$mpiInclude",$kernel,$io,(Join-Path $mpi 'mandelbrot_mpi_main.c'),(Join-Path $mpi 'mandelbrot_mpi.c'),"-L$mpiLib",'-l:libmsmpi.dll.a','-o',(Join-Path $out 'mandelbrot_mpi.exe'))) 'MPI UCRT64'
Build ($flags + @('-fopenmp',$kernel,(Join-Path $experiments 'benchmark_serial.c'),'-o',(Join-Path $out 'benchmark_serial.exe'),'-lm','-fopenmp')) 'benchmark Serial UCRT64'
Build ($flags + @('-fopenmp',"-I$openmp",$kernel,(Join-Path $openmp 'mandelbrot_openmp.c'),(Join-Path $experiments 'benchmark_openmp.c'),'-o',(Join-Path $out 'benchmark_openmp.exe'),'-lm','-fopenmp')) 'benchmark OpenMP UCRT64'
@("compiler=$gcc", "version=$(& $gcc --version | Select-Object -First 1)", 'runtime=MSYS2 UCRT64', 'mpi=MS-MPI; direct GCC link to ucrt64/lib/libmsmpi.dll.a', 'flags=-std=c11 -O2 -Wall -Wextra -Wpedantic -Wconversion -ffp-contract=off; -fopenmp for Serial/OpenMP executables and benchmarks', "built=$(Get-Date -Format o)", "targets=$($targets -join ',')") | Set-Content -LiteralPath (Join-Path $out 'build_complete.txt')
Write-Host "Executaveis UCRT64 criados em $out"
