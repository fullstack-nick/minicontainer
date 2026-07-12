resource "google_storage_bucket" "terraform_state" {
  name                        = "minicontainer-r7m5o9ld-tfstate"
  project                     = var.project_id
  location                    = "US-WEST1"
  storage_class               = "STANDARD"
  uniform_bucket_level_access = true
  public_access_prevention    = "enforced"
  force_destroy               = false

  versioning {
    enabled = true
  }

  lifecycle_rule {
    condition {
      days_since_noncurrent_time = 30
      num_newer_versions         = 1
    }
    action {
      type = "Delete"
    }
  }
}

output "bucket_name" {
  value = google_storage_bucket.terraform_state.name
}
