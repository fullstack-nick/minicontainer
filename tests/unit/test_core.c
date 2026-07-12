#include "minicontainer/validate.h"

#include <stdio.h>

static int failures = 0;

static void expect(int condition, const char *name) {
    if (!condition) {
        (void)fprintf(stderr, "FAIL %s\n", name);
        ++failures;
    }
}

int main(void) {
    expect(mc_valid_name("alpine-3.24") != 0, "valid image name");
    expect(mc_valid_name("Upper") == 0, "uppercase image name rejected");
    expect(mc_valid_name("../escape") == 0, "traversing image name rejected");
    expect(mc_safe_archive_path("bin/busybox") != 0, "safe archive path");
    expect(mc_safe_archive_path("/etc/passwd") == 0, "absolute archive path rejected");
    expect(mc_safe_archive_path("a/../b") == 0, "dotdot archive path rejected");
    expect(mc_safe_archive_path("a//b") == 0, "empty archive component rejected");
    expect(mc_safe_link_target("usr/bin/busybox") != 0, "safe link target");
    expect(mc_safe_link_target("/bin/busybox") != 0, "container-absolute link target");
    expect(mc_safe_link_target("../../host") == 0, "escaping link rejected");
    expect(mc_link_stays_beneath("usr/share/keys/x86/key", "../key") != 0,
           "relative link remaining in root accepted");
    expect(mc_link_stays_beneath("usr/key", "../../../../host") == 0,
           "relative link escaping root rejected");
    expect(mc_valid_environment("DEMO=value") != 0, "valid environment assignment");
    expect(mc_valid_environment("9BAD=value") == 0, "invalid environment key rejected");
    {
        unsigned int user = 0U;
        unsigned int group = 0U;
        expect(mc_parse_user("1000:1001", &user, &group) != 0 && user == 1000U &&
                   group == 1001U,
               "numeric user and group parsed");
        expect(mc_parse_user("70000", &user, &group) == 0, "out-of-range user rejected");
    }
    if (failures == 0) {
        (void)puts("PASS core validation tests");
    }
    return failures == 0 ? 0 : 1;
}
