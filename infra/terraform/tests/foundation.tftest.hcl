mock_provider "google" {}

run "locked_private_free_tier_topology" {
  command = plan

  variables {
    billing_account = "000000-000000-000000"
  }

  assert {
    condition     = google_compute_instance.runtime.machine_type == "e2-micro"
    error_message = "Final runtime VM must remain e2-micro."
  }
  assert {
    condition     = google_compute_instance.runtime.zone == "us-west1-a"
    error_message = "Runtime VM must remain in the locked free-tier zone."
  }
  assert {
    condition     = length(google_compute_instance.runtime.network_interface[0].access_config) == 0
    error_message = "Runtime VM must not have an external address."
  }
  assert {
    condition     = google_compute_instance.runtime.boot_disk[0].initialize_params[0].size == 30
    error_message = "Boot disk must remain within the 30 GB free-tier allowance."
  }
  assert {
    condition     = google_compute_firewall.iap.source_ranges == toset(["35.235.240.0/20"])
    error_message = "Ingress must remain restricted to the IAP TCP forwarding range."
  }
  assert {
    condition     = length(google_compute_router.temporary) == 0 && length(google_compute_router_nat.temporary) == 0
    error_message = "Temporary Cloud NAT must be absent from the default/final topology."
  }
}
