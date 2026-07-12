locals {
  services = toset([
    "compute.googleapis.com",
    "iap.googleapis.com",
    "iam.googleapis.com",
    "oslogin.googleapis.com",
    "serviceusage.googleapis.com"
  ])
}

resource "google_project_service" "required" {
  for_each           = local.services
  project            = var.project_id
  service            = each.value
  disable_on_destroy = false
}

resource "google_compute_network" "main" {
  name                    = "minicontainer-vpc"
  auto_create_subnetworks = false
  depends_on              = [google_project_service.required]
}

resource "google_compute_subnetwork" "main" {
  name                     = "minicontainer-us-west1"
  region                   = var.region
  network                  = google_compute_network.main.id
  ip_cidr_range            = "10.42.0.0/24"
  private_ip_google_access = true
}

resource "google_compute_firewall" "iap" {
  name          = "minicontainer-iap"
  network       = google_compute_network.main.name
  source_ranges = ["35.235.240.0/20"]
  target_tags   = ["minicontainer"]
  allow {
    protocol = "tcp"
    ports    = ["22", "8080"]
  }
}

resource "google_service_account" "vm" {
  account_id   = "minicontainer-vm"
  display_name = "MiniContainer VM"
}

resource "google_project_iam_member" "iap" {
  project = var.project_id
  role    = "roles/iap.tunnelResourceAccessor"
  member  = "user:${var.operator_email}"
}

resource "google_project_iam_member" "os_admin" {
  project = var.project_id
  role    = "roles/compute.osAdminLogin"
  member  = "user:${var.operator_email}"
}

resource "google_compute_instance" "runtime" {
  name                      = "minicontainer-vm"
  machine_type              = "e2-micro"
  zone                      = var.zone
  can_ip_forward            = true
  allow_stopping_for_update = true
  tags                      = ["minicontainer"]

  boot_disk {
    initialize_params {
      image = "ubuntu-os-cloud/ubuntu-2404-lts-amd64"
      size  = 30
      type  = "pd-standard"
    }
  }
  network_interface {
    subnetwork = google_compute_subnetwork.main.id
  }
  service_account {
    email  = google_service_account.vm.email
    scopes = []
  }
  metadata = {
    enable-oslogin         = "TRUE"
    block-project-ssh-keys = "TRUE"
    serial-port-enable     = "FALSE"
  }
  shielded_instance_config {
    enable_secure_boot          = true
    enable_vtpm                 = true
    enable_integrity_monitoring = true
  }
  depends_on = [google_project_service.required]
}

resource "google_billing_budget" "monthly" {
  billing_account = var.billing_account
  display_name    = "MiniContainer monthly budget"
  budget_filter {
    projects = ["projects/${data.google_project.current.number}"]
  }
  amount {
    specified_amount {
      currency_code = "USD"
      units         = "10"
    }
  }
  threshold_rules {
    threshold_percent = 0.5
  }
  threshold_rules {
    threshold_percent = 0.8
  }
  threshold_rules {
    threshold_percent = 1.0
  }
  threshold_rules {
    threshold_percent = 1.0
    spend_basis       = "FORECASTED_SPEND"
  }
}

data "google_project" "current" {
  project_id = var.project_id
}
