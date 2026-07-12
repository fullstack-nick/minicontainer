terraform {
  required_version = "~> 1.15.0"
  required_providers {
    google = {
      source  = "hashicorp/google"
      version = "7.39.0"
    }
  }
}

provider "google" {
  project               = var.project_id
  region                = "us-west1"
  billing_project       = var.project_id
  user_project_override = true
}
