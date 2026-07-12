variable "project_id" {
  type    = string
  default = "minicontainer-r7m5o9ld"
  validation {
    condition     = var.project_id == "minicontainer-r7m5o9ld"
    error_message = "This stack may only mutate the locked MiniContainer project."
  }
}
variable "region" {
  type    = string
  default = "us-west1"
}
variable "zone" {
  type    = string
  default = "us-west1-a"
}
variable "operator_email" {
  type    = string
  default = "nickaccturk@gmail.com"
}
variable "billing_account" {
  type      = string
  sensitive = true
}
variable "enable_temporary_nat" {
  type        = bool
  default     = false
  description = "Stage-gated outbound NAT; must be false at every stage exit."
}
