param(
    [string]$SourceBranch = "SPHERICAL",
    [string]$ReleaseBranch = "release",
    [string]$Prefix = "SPHERICAL-SDK"
)

$ErrorActionPreference = "Stop"

# Always run from repo root regardless of caller working directory.
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Push-Location $repoRoot
try {
    $null = git rev-parse --is-inside-work-tree 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "Not a git repository: $repoRoot"
    }

    git switch $SourceBranch
    git push origin $SourceBranch

    $split = git subtree split --prefix=$Prefix $SourceBranch
    if (-not $split) { throw "subtree split failed" }

    git push origin "$split`:refs/heads/$ReleaseBranch" --force
    Write-Host "Published $Prefix from $SourceBranch to $ReleaseBranch at $split"
}
finally {
    Pop-Location
}
