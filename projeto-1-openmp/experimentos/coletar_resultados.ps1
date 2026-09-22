param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("serial", "openmp")]
    [string] $Versao,

    [Parameter(Mandatory = $true)]
    [ValidatePattern("^[a-zA-Z0-9_-]+$")]
    [string] $Nome,

    [ValidateRange(1, 100)]
    [int] $Repeticoes = 5,

    [ValidateRange(0, 10)]
    [int] $Aquecimentos = 1,

    [string[]] $Argumentos = @()
)

$ErrorActionPreference = "Stop"

function Extrair-Valor {
    param(
        [string] $Texto,
        [string] $Padrao,
        [string] $Campo
    )

    $correspondencia = [regex]::Match($Texto, $Padrao)
    if (-not $correspondencia.Success) {
        throw "Nao foi possivel encontrar $Campo na saida do programa."
    }

    return $correspondencia.Groups[1].Value
}

$raiz = Split-Path -Parent $PSScriptRoot
$executavel = Join-Path $raiz "compilacao/mandelbrot_$Versao.exe"
$diretorioMedicoes = Join-Path $PSScriptRoot "medicoes"
$arquivoCsv = Join-Path $diretorioMedicoes "$Nome.csv"
$arquivoLog = Join-Path $diretorioMedicoes "$Nome.log"

if (-not (Test-Path $executavel -PathType Leaf)) {
    throw "Executavel nao encontrado: $executavel"
}

New-Item -ItemType Directory -Force -Path $diretorioMedicoes | Out-Null
Set-Content -Path $arquivoLog -Value ""

Push-Location $raiz
try {
    for ($aquecimento = 1; $aquecimento -le $Aquecimentos; ++$aquecimento) {
        & $executavel @Argumentos | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "O aquecimento $aquecimento terminou com codigo $LASTEXITCODE."
        }
    }

    $resultados = for ($repeticao = 1; $repeticao -le $Repeticoes; ++$repeticao) {
        $saida = & $executavel @Argumentos 2>&1 | Out-String
        $codigoSaida = $LASTEXITCODE

        Add-Content -Path $arquivoLog -Value "===== repeticao $repeticao ====="
        Add-Content -Path $arquivoLog -Value $saida.TrimEnd()

        if ($codigoSaida -ne 0) {
            throw "A repeticao $repeticao terminou com codigo $codigoSaida."
        }

        $tempoGeracao = Extrair-Valor `
            $saida `
            'Tempo de geracao: ([0-9]+(?:\.[0-9]+)?) segundos\.' `
            'tempo de geracao'
        $tempoEscrita = Extrair-Valor `
            $saida `
            'Tempo de escrita: ([0-9]+(?:\.[0-9]+)?) segundos\.' `
            'tempo de escrita'

        $threadsUsadas = ""
        $tempoMinimo = ""
        $tempoMedio = ""
        $tempoMaximo = ""
        $fatorBalanceamento = ""

        if ($Versao -eq "openmp") {
            $balanceamento = [regex]::Match(
                $saida,
                'Balanceamento: threads=([0-9]+), minimo=([0-9]+(?:\.[0-9]+)?) s, medio=([0-9]+(?:\.[0-9]+)?) s, maximo=([0-9]+(?:\.[0-9]+)?) s, fator_maximo_medio=([0-9]+(?:\.[0-9]+)?)\.'
            )

            if (-not $balanceamento.Success) {
                throw "Nao foi possivel encontrar o balanceamento na repeticao $repeticao."
            }

            $threadsUsadas = $balanceamento.Groups[1].Value
            $tempoMinimo = $balanceamento.Groups[2].Value
            $tempoMedio = $balanceamento.Groups[3].Value
            $tempoMaximo = $balanceamento.Groups[4].Value
            $fatorBalanceamento = $balanceamento.Groups[5].Value
        }

        [pscustomobject]@{
            nome = $Nome
            versao = $Versao
            repeticao = $repeticao
            argumentos = $Argumentos -join " "
            tempo_geracao_s = $tempoGeracao
            tempo_escrita_s = $tempoEscrita
            threads_usadas = $threadsUsadas
            tempo_minimo_thread_s = $tempoMinimo
            tempo_medio_thread_s = $tempoMedio
            tempo_maximo_thread_s = $tempoMaximo
            fator_maximo_medio = $fatorBalanceamento
        }
    }
} finally {
    Pop-Location
}

$resultados | Export-Csv `
    -Path $arquivoCsv `
    -Delimiter ';' `
    -NoTypeInformation `
    -Encoding UTF8

Write-Host "Medicoes salvas em $arquivoCsv"
Write-Host "Saidas completas salvas em $arquivoLog"

