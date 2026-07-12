variable "project_id" {
  type    = string
  default = "minicontainer-r7m5o9ld"
  validation {
    condition     = var.project_id == "minicontainer-r7m5o9ld"
    error_message = "Bootstrap is locked to the MiniContainer project."
  }
}
