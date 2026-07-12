param(
  [string]$ProjectId = "minicontainer-r7m5o9ld",
  [string]$ExpectedAccount = "nickaccturk@gmail.com"
)

$ErrorActionPreference = "Stop"
$account = (gcloud config get-value account 2>$null).Trim()
if ($account -cne $ExpectedAccount) {
  throw "Refusing GCP mutation: active account '$account' is not '$ExpectedAccount'."
}
$configuredProject = (gcloud config get-value project 2>$null).Trim()
if ($configuredProject -cne $ProjectId) {
  throw "Refusing GCP mutation: configured project '$configuredProject' is not '$ProjectId'."
}

Write-Output "GCP guard passed for $ExpectedAccount and $ProjectId."
