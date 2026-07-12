output "instance_name" {
  value = google_compute_instance.runtime.name
}
output "instance_zone" {
  value = google_compute_instance.runtime.zone
}
output "instance_internal_ip" {
  value = google_compute_instance.runtime.network_interface[0].network_ip
}
