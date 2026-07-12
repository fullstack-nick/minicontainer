#include "minicontainer/image.h"

#include "minicontainer/fs.h"
#include "minicontainer/subid.h"
#include "minicontainer/validate.h"

#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <openssl/evp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int path_join(char *output, size_t size, const char *left, const char *right,
                     struct mc_error *error) {
    const size_t left_length = strlen(left);
    const size_t right_length = strlen(right);

    if (left_length + 1U + right_length + 1U > size) {
        mc_error_set(error, MC_EXIT_USAGE, ENAMETOOLONG, "path-join", left,
                     "path is too long");
        return -1;
    }
    (void)memcpy(output, left, left_length);
    output[left_length] = '/';
    (void)memcpy(output + left_length + 1U, right, right_length + 1U);
    return 0;
}

static int archive_digest(const char *path, char output[65], struct mc_error *error) {
    unsigned char buffer[32768];
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_length = 0U;
    EVP_MD_CTX *context = NULL;
    int descriptor = -1;
    ssize_t count;
    size_t index;

    descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    context = EVP_MD_CTX_new();
    if (descriptor < 0 || context == NULL || EVP_DigestInit_ex(context, EVP_sha256(), NULL) != 1) {
        mc_error_set(error, MC_EXIT_RUNTIME, descriptor < 0 ? errno : 0, "image-digest", path,
                     "cannot initialize SHA-256");
        goto fail;
    }
    while ((count = read(descriptor, buffer, sizeof(buffer))) > 0) {
        if (EVP_DigestUpdate(context, buffer, (size_t)count) != 1) {
            mc_error_set(error, MC_EXIT_INTERNAL, 0, "image-digest", path,
                         "SHA-256 update failed");
            goto fail;
        }
    }
    if (count < 0 || EVP_DigestFinal_ex(context, digest, &digest_length) != 1 ||
        digest_length != 32U) {
        mc_error_set(error, MC_EXIT_RUNTIME, count < 0 ? errno : 0, "image-digest", path,
                     "SHA-256 finalization failed");
        goto fail;
    }
    for (index = 0U; index < digest_length; ++index) {
        (void)snprintf(output + (index * 2U), 3U, "%02x", digest[index]);
    }
    output[64] = '\0';
    EVP_MD_CTX_free(context);
    (void)close(descriptor);
    return 0;

fail:
    EVP_MD_CTX_free(context);
    if (descriptor >= 0) {
        (void)close(descriptor);
    }
    return -1;
}

static int validate_entry(struct archive_entry *entry, uint32_t uid_start, uint32_t gid_start,
                          struct mc_error *error) {
    const char *path = archive_entry_pathname(entry);
    const char *symlink = archive_entry_symlink(entry);
    const char *hardlink = archive_entry_hardlink(entry);
    const mode_t type = archive_entry_filetype(entry);
    const mode_t permissions = archive_entry_perm(entry);

    while (path != NULL && path[0] == '.' && path[1] == '/') {
        path += 2;
    }
    if (path == NULL || path[0] == '\0') {
        return 1;
    }
    if (!mc_safe_archive_path(path)) {
        mc_error_set(error, MC_EXIT_USAGE, 0, "image-import", path, "unsafe archive path");
        return -1;
    }
    archive_entry_set_pathname(entry, path);
    if ((symlink != NULL && !mc_link_stays_beneath(path, symlink)) ||
        (hardlink != NULL && !mc_safe_archive_path(hardlink))) {
        mc_error_set(error, MC_EXIT_USAGE, 0, "image-import", path, "unsafe archive link");
        return -1;
    }
    if (type == AE_IFBLK || type == AE_IFCHR || type == AE_IFIFO || type == AE_IFSOCK) {
        mc_error_set(error, MC_EXIT_USAGE, 0, "image-import", path,
                     "special files are forbidden");
        return -1;
    }
    if ((permissions & (S_ISUID | S_ISGID)) != 0U) {
        mc_error_set(error, MC_EXIT_USAGE, 0, "image-import", path,
                     "setuid and setgid entries are forbidden");
        return -1;
    }
    if (archive_entry_uid(entry) < 0 || archive_entry_uid(entry) > 65535 ||
        archive_entry_gid(entry) < 0 || archive_entry_gid(entry) > 65535) {
        mc_error_set(error, MC_EXIT_USAGE, 0, "image-import", path,
                     "archive owner is outside the container ID range");
        return -1;
    }
    archive_entry_set_uid(entry, (la_int64_t)uid_start + archive_entry_uid(entry));
    archive_entry_set_gid(entry, (la_int64_t)gid_start + archive_entry_gid(entry));
    return 0;
}

static int extract_archive(const char *source, const char *destination,
                           uint32_t uid_start, uint32_t gid_start, struct mc_error *error) {
    struct archive *reader = NULL;
    struct archive *writer = NULL;
    struct archive_entry *entry = NULL;
    int cwd = -1;
    int status = -1;
    int result;

    cwd = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    reader = archive_read_new();
    writer = archive_write_disk_new();
    if (cwd < 0 || reader == NULL || writer == NULL) {
        mc_error_set(error, MC_EXIT_INTERNAL, errno, "image-import", source,
                     "cannot initialize extractor");
        goto cleanup;
    }
    archive_read_support_filter_all(reader);
    archive_read_support_format_tar(reader);
    archive_write_disk_set_options(writer, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM |
                                               ARCHIVE_EXTRACT_OWNER |
                                               ARCHIVE_EXTRACT_SECURE_NODOTDOT |
                                               ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS |
                                               ARCHIVE_EXTRACT_SECURE_SYMLINKS);
    if (archive_read_open_filename(reader, source, 10240U) != ARCHIVE_OK ||
        chdir(destination) != 0) {
        mc_error_set(error, MC_EXIT_RUNTIME, errno, "image-import", source,
                     "cannot open archive or destination");
        goto cleanup;
    }
    while ((result = archive_read_next_header(reader, &entry)) == ARCHIVE_OK) {
        const int validation = validate_entry(entry, uid_start, gid_start, error);
        if (validation > 0) {
            (void)archive_read_data_skip(reader);
            continue;
        }
        if (validation < 0 || archive_read_extract2(reader, entry, writer) != ARCHIVE_OK) {
            if (error->message[0] == '\0') {
                mc_error_set(error, MC_EXIT_RUNTIME, 0, "image-import",
                             archive_entry_pathname(entry), archive_error_string(writer));
            }
            goto cleanup;
        }
    }
    if (result != ARCHIVE_EOF) {
        mc_error_set(error, MC_EXIT_RUNTIME, 0, "image-import", source,
                     archive_error_string(reader));
        goto cleanup;
    }
    status = 0;

cleanup:
    if (cwd >= 0) {
        if (fchdir(cwd) != 0 && status == 0) {
            mc_error_set(error, MC_EXIT_RUNTIME, errno, "image-import", destination,
                         "cannot restore working directory");
            status = -1;
        }
        (void)close(cwd);
    }
    if (writer != NULL) {
        (void)archive_write_free(writer);
    }
    if (reader != NULL) {
        (void)archive_read_free(reader);
    }
    return status;
}

int mc_image_import(const char *name, const char *archive_path,
                    struct mc_image_result *result, struct mc_error *error) {
    char image_base[PATH_MAX];
    char digest_base[PATH_MAX];
    char marker_path[PATH_MAX];
    char staging_base[PATH_MAX];
    char staging_rootfs[PATH_MAX];
    char staging_marker[PATH_MAX];
    char staging_name[96];
    char names_dir[PATH_MAX];
    char name_path[PATH_MAX];
    char digest_record[80];
    struct stat metadata;
    uint32_t uid_start;
    uint32_t gid_start;

    if (!mc_valid_name(name)) {
        mc_error_set(error, MC_EXIT_USAGE, 0, "image-import", name, "invalid image name");
        return -1;
    }
    if (archive_path == NULL || result == NULL || stat(archive_path, &metadata) != 0 ||
        !S_ISREG(metadata.st_mode)) {
        mc_error_set(error, MC_EXIT_NOT_FOUND, errno, "image-import", archive_path,
                     "archive is not a regular file");
        return -1;
    }
    if (archive_digest(archive_path, result->digest, error) != 0) {
        return -1;
    }
    if (mc_subid_range(&uid_start, &gid_start, error) != 0) {
        return -1;
    }
    if (path_join(digest_base, sizeof(digest_base), mc_state_dir(), "images/sha256", error) !=
            0 ||
        path_join(image_base, sizeof(image_base), digest_base, result->digest, error) != 0 ||
        path_join(result->rootfs, sizeof(result->rootfs), image_base, "rootfs", error) != 0 ||
        path_join(marker_path, sizeof(marker_path), image_base, ".complete", error) != 0 ||
        path_join(names_dir, sizeof(names_dir), mc_state_dir(), "images/names", error) != 0) {
        return -1;
    }
    if (mc_mkdir_p(digest_base, 0755, error) != 0 || mc_mkdir_p(names_dir, 0755, error) != 0) {
        return -1;
    }
    if (access(marker_path, R_OK) != 0) {
        if (access(image_base, F_OK) == 0) {
            mc_error_set(error, MC_EXIT_CONFLICT, 0, "image-import", image_base,
                         "incomplete image directory already exists; run garbage collection");
            return -1;
        }
        (void)snprintf(staging_name, sizeof(staging_name), "%s.import.%ld", result->digest,
                       (long)getpid());
        if (path_join(staging_base, sizeof(staging_base), digest_base, staging_name, error) != 0 ||
            path_join(staging_rootfs, sizeof(staging_rootfs), staging_base, "rootfs", error) != 0 ||
            path_join(staging_marker, sizeof(staging_marker), staging_base, ".complete", error) !=
                0 ||
            mc_mkdir_p(staging_rootfs, 0755, error) != 0) {
            return -1;
        }
        if (chown(staging_rootfs, (uid_t)uid_start, (gid_t)gid_start) != 0 ||
            extract_archive(archive_path, staging_rootfs, uid_start, gid_start, error) != 0 ||
            mc_write_atomic(staging_marker, result->digest, strlen(result->digest), 0444, error) !=
                0) {
            return -1;
        }
        if (chmod(staging_base, 0555) != 0 || chmod(staging_rootfs, 0555) != 0 ||
            rename(staging_base, image_base) != 0) {
            mc_error_set(error, MC_EXIT_RUNTIME, errno, "image-import", image_base,
                         "cannot publish immutable image");
            return -1;
        }
    }
    if (path_join(name_path, sizeof(name_path), names_dir, name, error) != 0) {
        return -1;
    }
    (void)snprintf(digest_record, sizeof(digest_record), "sha256:%s\n", result->digest);
    return mc_write_atomic(name_path, digest_record, strlen(digest_record), 0644, error);
}

int mc_image_resolve(const char *name, char *rootfs, unsigned long rootfs_size,
                     struct mc_error *error) {
    char names_dir[PATH_MAX];
    char name_path[PATH_MAX];
    char digest_dir[PATH_MAX];
    char image_dir[PATH_MAX];
    char marker[PATH_MAX];
    char record[80];
    int descriptor;
    ssize_t count;
    size_t digest_length;

    if (!mc_valid_name(name) || rootfs == NULL || rootfs_size == 0UL) {
        mc_error_set(error, MC_EXIT_USAGE, 0, "image-resolve", name, "invalid image name");
        return -1;
    }
    if (path_join(names_dir, sizeof(names_dir), mc_state_dir(), "images/names", error) != 0 ||
        path_join(name_path, sizeof(name_path), names_dir, name, error) != 0) {
        return -1;
    }
    descriptor = open(name_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        mc_error_set(error, MC_EXIT_NOT_FOUND, errno, "image-resolve", name,
                     "image name does not exist");
        return -1;
    }
    count = read(descriptor, record, sizeof(record) - 1U);
    (void)close(descriptor);
    if (count < 0) {
        mc_error_set(error, MC_EXIT_RUNTIME, errno, "image-resolve", name,
                     "cannot read image record");
        return -1;
    }
    record[(size_t)count] = '\0';
    digest_length = strcspn(record, "\r\n");
    record[digest_length] = '\0';
    if (digest_length != 71U || strncmp(record, "sha256:", 7U) != 0) {
        mc_error_set(error, MC_EXIT_RUNTIME, 0, "image-resolve", name,
                     "image record has an invalid digest");
        return -1;
    }
    if (path_join(digest_dir, sizeof(digest_dir), mc_state_dir(), "images/sha256", error) != 0 ||
        path_join(image_dir, sizeof(image_dir), digest_dir, record + 7, error) != 0 ||
        path_join(rootfs, (size_t)rootfs_size, image_dir, "rootfs", error) != 0 ||
        path_join(marker, sizeof(marker), image_dir, ".complete", error) != 0) {
        return -1;
    }
    if (access(marker, R_OK) != 0) {
        mc_error_set(error, MC_EXIT_RUNTIME, errno, "image-resolve", name,
                     "image is incomplete");
        return -1;
    }
    return 0;
}
