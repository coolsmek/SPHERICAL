param(
    [string]$SourceBranch = "SPHERICAL",
    [string]$ReleaseBranch = "release",
    [string]$Prefix = "SPHERICAL-SDK"
)

$ErrorActionPreference = "Stop"

git switch $SourceBranch
git push origin $SourceBranch

$split = git subtree split --prefix=$Prefix $SourceBranch
if (-not $split) { throw "subtree split failed" }

git push origin "$split`:refs/heads/$ReleaseBranch" --force
Write-Host "Published $Prefix from $SourceBranch to $ReleaseBranch at $split"