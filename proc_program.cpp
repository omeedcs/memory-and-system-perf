#include <cstdio>
#include <cstdlib>
#include <linux/perf_event.h>
#include <sys/syscall.h>
// to directly invoke system calls, we need to include the header file.
# include <unistd.h>


 static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                             int cpu, int group_fd, unsigned long flags) {
           int ret;
           ret = syscall(SYS_perf_event_open, hw_event, pid, cpu,
                         group_fd, flags);
           return ret;
}

int main() {

        FILE *file;
        char buffer[1024];

        file = fopen("/proc/self/maps", "r");

        // for every system call, we need to check if the return code is
        // less than zero, if it is, we need to call perror (lab1 note)
        if (file == nullptr) {
                // nullptr is supposedly safer?
                perror("Error in system call fopen");
                return EXIT_FAILURE;
        }

        while (fgets(buffer, sizeof(buffer), file) != nullptr) {
                printf("%s", buffer);
        }

        fclose(file);
        return EXIT_SUCCESS;
}