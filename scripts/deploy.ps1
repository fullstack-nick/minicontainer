param(
  [string]$ProjectId = "minicontainer-r7m5o9ld",
  [string]$Zone = "us-west1-a",
  [string]$VmName = "minicontainer-vm"
)

$ErrorActionPreference = "Stop"
& "$PSScriptRoot/gcp-guard.ps1" -ProjectId $ProjectId
$repo = (Resolve-Path "$PSScriptRoot/..").Path
$package = Join-Path $repo "dist/minicontainer_1.0.0_amd64.deb"
$checksum = "$package.sha256"
if (!(Test-Path $package) -or !(Test-Path $checksum)) { throw "Build the release artifact first." }
$expected = ((Get-Content $checksum).Split(' ')[0]).ToLowerInvariant()
$actual = (Get-FileHash -Algorithm SHA256 $package).Hash.ToLowerInvariant()
if ($actual -cne $expected) { throw "Package checksum mismatch." }

gcloud compute scp $package "${VmName}:/tmp/minicontainer.deb" --project=$ProjectId --zone=$Zone --tunnel-through-iap
if ($LASTEXITCODE -ne 0) { throw "Artifact upload failed." }
gcloud compute ssh $VmName --project=$ProjectId --zone=$Zone --tunnel-through-iap --command="sudo dpkg -i /tmp/minicontainer.deb && minicontainer version --json && sha256sum /tmp/minicontainer.deb"
if ($LASTEXITCODE -ne 0) { throw "Remote package installation or verification failed." }
