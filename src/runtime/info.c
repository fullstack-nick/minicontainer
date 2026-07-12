#include "minicontainer/info.h"

#include <stdio.h>
#include <sys/utsname.h>

int mc_print_info(int json) {
    struct utsname host;
    const int linux_host = uname(&host) == 0 && host.sysname[0] == 'L';
    FILE *cgroup = fopen("/sys/fs/cgroup/cgroup.controllers", "r");
    const int cgroup_v2 = cgroup != NULL;

    if (cgroup != NULL) {
        (void)fclose(cgroup);
    }
    if (json != 0) {
        (void)printf("{\"linux\":%s,\"cgroup_version\":%d,\"ready\":%s}\n",
                     linux_host != 0 ? "true" : "false", cgroup_v2 != 0 ? 2 : 0,
                     linux_host != 0 && cgroup_v2 != 0 ? "true" : "false");
    } else {
        (void)printf("linux: %s\ncgroup v2: %s\nready: %s\n",
                     linux_host != 0 ? "yes" : "no", cgroup_v2 != 0 ? "yes" : "no",
                     linux_host != 0 && cgroup_v2 != 0 ? "yes" : "no");
    }
    return linux_host != 0 && cgroup_v2 != 0 ? 0 : 3;
}

