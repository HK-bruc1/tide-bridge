#Requires -Version 5.1

Set-StrictMode -Version 2.0

function Get-HostTestRepoRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
}

function Read-RepoFile {
    param(
        [Parameter(Mandatory)][string]$RepoRoot,
        [Parameter(Mandatory)][string]$RelativePath
    )

    return Get-Content -Raw -Encoding UTF8 -LiteralPath `
        (Join-Path $RepoRoot $RelativePath)
}

function Assert-Contract {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][bool]$Condition,
        [Parameter(Mandatory)][string]$Message
    )

    if (-not $Condition) {
        throw "${Name}: $Message"
    }
    Write-Host "PASS: $Name"
}

function Get-SourceSlice {
    param(
        [Parameter(Mandatory)][string]$Text,
        [Parameter(Mandatory)][string]$StartMarker,
        [Parameter(Mandatory)][string]$EndMarker,
        [switch]$Last
    )

    $start = if ($Last) {
        $Text.LastIndexOf($StartMarker, [System.StringComparison]::Ordinal)
    } else {
        $Text.IndexOf($StartMarker, [System.StringComparison]::Ordinal)
    }
    if ($start -lt 0) {
        throw "Source marker not found: $StartMarker"
    }

    $end = $Text.IndexOf(
        $EndMarker,
        $start + $StartMarker.Length,
        [System.StringComparison]::Ordinal
    )
    if ($end -lt 0) {
        throw "Source marker not found after '$StartMarker': $EndMarker"
    }
    return $Text.Substring($start, $end - $start)
}

function Test-TokensInOrder {
    param(
        [Parameter(Mandatory)][string]$Text,
        [Parameter(Mandatory)][string[]]$Tokens
    )

    $position = -1
    foreach ($token in $Tokens) {
        $position = $Text.IndexOf(
            $token,
            $position + 1,
            [System.StringComparison]::Ordinal
        )
        if ($position -lt 0) {
            return $false
        }
    }
    return $true
}
