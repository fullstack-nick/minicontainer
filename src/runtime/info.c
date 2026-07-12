#include "minicontainer/info.h"

#include <stdio.h>
#include <net/if.h>
#include <sys/utsname.h>
#include <unistd.h>

int mc_print_info(int json) {
    struct utsname host;
    const int linux_host = uname(&host) == 0 && host.sysname[0] == 'L';
    FILE *cgroup = fopen("/sys/fs/cgroup/cgroup.controllers", "r");
    const int cgroup_v2 = cgroup != NULL;
    const int bridge_ready = if_nametoindex("mcbr0") != 0U;
    const int nft_available = access("/usr/sbin/nft", X_OK) == 0 || access("/usr/bin/nft", X_OK) == 0;

    if (cgroup != NULL) {
        (void)fclose(cgroup);
    }
    if (json != 0) {
        (void)printf("{\"linux\":%s,\"cgroup_version\":%d,\"bridge_ready\":%s,"
                     "\"nftables_available\":%s,\"ready\":%s}\n",
                     linux_host != 0 ? "true" : "false", cgroup_v2 != 0 ? 2 : 0,
                     bridge_ready != 0 ? "true" : "false",
                     nft_available != 0 ? "true" : "false",
                     linux_host != 0 && cgroup_v2 != 0 ? "true" : "false");
    } else {
        (void)printf("linux: %s\ncgroup v2: %s\nbridge: %s\nnftables: %s\nready: %s\n",
                     linux_host != 0 ? "yes" : "no", cgroup_v2 != 0 ? "yes" : "no",
                     bridge_ready != 0 ? "ready" : "not initialized",
                     nft_available != 0 ? "available" : "missing",
                     linux_host != 0 && cgroup_v2 != 0 ? "yes" : "no");
    }
    return linux_host != 0 && cgroup_v2 != 0 ? 0 : 3;
}
