param(
  [Parameter(Mandatory = $true)]
  [string]$BillingAccount,
  [string]$ProjectId = "minicontainer-r7m5o9ld",
  [string]$ExpectedAccount = "nickaccturk@gmail.com"
)

$ErrorActionPreference = "Stop"
$account = (gcloud config get-value account 2>$null).Trim()
if ($account -cne $ExpectedAccount) {
  throw "Refusing GCP mutation: active account '$account' is not '$ExpectedAccount'."
}

$project = gcloud projects describe $ProjectId --format="value(projectId)" 2>$null
if ($LASTEXITCODE -ne 0) {
  gcloud projects create $ProjectId --name=MiniContainer
  if ($LASTEXITCODE -ne 0) { throw "Failed to create GCP project '$ProjectId'." }
}
gcloud config set project $ProjectId | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Failed to configure GCP project '$ProjectId'." }
& "$PSScriptRoot/gcp-guard.ps1" -ProjectId $ProjectId -ExpectedAccount $ExpectedAccount

$linked = (gcloud billing projects list --billing-account=$BillingAccount --filter="projectId=$ProjectId AND billingEnabled=true" --format="value(projectId)").Trim()
if ($LASTEXITCODE -ne 0) { throw "Failed to inspect project billing state." }
if ($linked -cne $ProjectId) {
  gcloud billing projects link $ProjectId --billing-account=$BillingAccount
  if ($LASTEXITCODE -ne 0) { throw "Failed to link the GCP billing account." }
}
gcloud services enable cloudresourcemanager.googleapis.com serviceusage.googleapis.com compute.googleapis.com iam.googleapis.com iap.googleapis.com oslogin.googleapis.com storage.googleapis.com cloudbilling.googleapis.com billingbudgets.googleapis.com --project=$ProjectId
if ($LASTEXITCODE -ne 0) { throw "Failed to enable required GCP APIs." }

$repo = (Resolve-Path "$PSScriptRoot/..").Path
terraform "-chdir=$repo/infra/bootstrap" init
if ($LASTEXITCODE -ne 0) { throw "Terraform bootstrap initialization failed." }
terraform "-chdir=$repo/infra/bootstrap" apply -auto-approve
if ($LASTEXITCODE -ne 0) { throw "Terraform state-bucket apply failed." }
terraform "-chdir=$repo/infra/terraform" init -migrate-state -force-copy
if ($LASTEXITCODE -ne 0) { throw "Terraform backend initialization failed." }
