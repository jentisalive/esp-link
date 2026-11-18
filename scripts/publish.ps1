param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$')]
    [string]$Repository
)

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
foreach ($name in @('git', 'gh')) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
        throw "Install $name first. See docs/GITHUB.md."
    }
}
gh auth status
if ($LASTEXITCODE -ne 0) { throw 'Run gh auth login first. Never put a GitHub token in source files.' }

if (Test-Path '.git') { throw 'A Git repository already exists. Publish it manually to avoid overwriting your work.' }
git init -b main
if ($LASTEXITCODE -ne 0) { throw 'git init failed.' }
git add .gitignore .gitattributes README.md .github ios firmware scripts docs
if ($LASTEXITCODE -ne 0) { throw 'git add failed.' }
git commit -m 'Initial ESP Link iOS app and uPesy ESP32 firmware'
if ($LASTEXITCODE -ne 0) { throw 'git commit failed. Configure git user.name and user.email, then commit and publish manually.' }
gh repo create $Repository --private --source . --remote origin --push
if ($LASTEXITCODE -ne 0) { throw 'Publishing failed. Local files and commit were preserved; inspect the output before retrying.' }
Write-Host "Published: https://github.com/$Repository"
Write-Host 'Open Actions > Build and test to download the unsigned IPA and firmware.'
