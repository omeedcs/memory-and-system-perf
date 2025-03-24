#include <cstdio>
#include <cstdlib>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include <unistd.h>
#include <cstring>

 static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                             int cpu, int group_fd, unsigned long flags) {
           int ret;
           ret = syscall(SYS_perf_event_open, hw_event, pid, cpu,
                         group_fd, flags);
           return ret;
}


int main() {
	int fd;
	long long count;
	struct perf_event_attr pe;
	memset(&pe, 0, sizeof(pe));
	pe.type = PERF_TYPE_HARDWARE;
	pe.size = sizeof(pe);
	pe.config = PERF_COUNT_HW_INSTRUCTIONS;
	pe.disabled = 1;
	pe.exclude_kernel = 1;
	pe.exclude_hv = 1;
	fd = perf_event_open(&pe, 0, -1, -1, 0);
	if (fd == -1) {
		printf("ERROR\n");
	} else {
		printf("SUCCESS\n");
	}
	close(fd);
        printf("Welcome to Advanced OS Project by Omeed Tehrani\n");
        return 0;
}