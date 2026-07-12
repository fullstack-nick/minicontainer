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
if (!$project) {
  gcloud projects create $ProjectId --name=MiniContainer
}
gcloud config set project $ProjectId | Out-Null
& "$PSScriptRoot/gcp-guard.ps1" -ProjectId $ProjectId -ExpectedAccount $ExpectedAccount
gcloud billing projects link $ProjectId --billing-account=$BillingAccount
gcloud services enable cloudresourcemanager.googleapis.com serviceusage.googleapis.com compute.googleapis.com iam.googleapis.com iap.googleapis.com oslogin.googleapis.com storage.googleapis.com cloudbilling.googleapis.com billingbudgets.googleapis.com --project=$ProjectId

$repo = (Resolve-Path "$PSScriptRoot/..").Path
terraform -chdir="$repo/infra/bootstrap" init
terraform -chdir="$repo/infra/bootstrap" apply -auto-approve
terraform -chdir="$repo/infra/terraform" init -migrate-state -force-copy

