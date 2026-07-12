terraform {
  backend "gcs" {
    bucket = "minicontainer-r7m5o9ld-tfstate"
    prefix = "foundation"
  }
}
