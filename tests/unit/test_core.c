#include "minicontainer/validate.h"
#include "minicontainer/resource.h"
#include "minicontainer/network.h"

#include <stdio.h>
#include <netinet/in.h>

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
    {
        uint64_t value = 0U;
        expect(mc_parse_bytes("128MiB", &value) != 0 && value == UINT64_C(134217728),
               "IEC memory parsed");
        expect(mc_parse_bytes("18446744073709551615GiB", &value) == 0,
               "memory overflow rejected");
        expect(mc_parse_cpu_quota("0.5", &value) != 0 && value == UINT64_C(50000),
               "fractional CPU parsed without floating point");
        expect(mc_parse_cpu_quota("0", &value) == 0, "zero CPU rejected");
        expect(mc_parse_positive_u64("128", UINT64_C(4194304), &value) != 0 && value == 128U,
               "PID limit parsed");
    }
    {
        struct mc_publish published;
        expect(mc_parse_publish("127.0.0.1:8080:80/tcp", &published) != 0 &&
                   published.host_ipv4 == UINT32_C(0x7f000001) &&
                   published.host_port == 8080U && published.container_port == 80U &&
                   published.protocol == IPPROTO_TCP,
               "TCP publish parsed");
        expect(mc_parse_publish("0.0.0.0:5353:53/udp", &published) != 0 &&
                   published.protocol == IPPROTO_UDP,
               "UDP wildcard publish parsed");
        expect(mc_parse_publish("127.0.0.1:0:80/tcp", &published) == 0,
               "zero host port rejected");
        expect(mc_parse_publish("127.0.0.1:8080:80/sctp", &published) == 0,
               "unsupported publish protocol rejected");
    }
    if (failures == 0) {
        (void)puts("PASS core validation tests");
    }
    return failures == 0 ? 0 : 1;
}
