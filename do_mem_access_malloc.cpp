#include <stdio.h>
#include <stdlib.h>
#include <linux/perf_event.h>
#include <sys/syscall.h>
 // to directly invoke system calls, we need to include the header file.
#include <unistd.h>
#include <sched.h> // for cpu_set_t and other scheduling things.
#include <cstring> // for memset
#include <sys/ioctl.h> // for ioctl
#include <cstdint> // for uint64_t
#include <cinttypes> // for PRIu64
#include <sys/resource.h>


struct read_format {
  uint64_t nr;
  struct {
    uint64_t value;
    uint64_t id;
  } values[];
};

// Simple, fast random number generator, here so we can observe it using profiler
long x = 1, y = 4, z = 7, w = 13;

long simplerand(void) {
	long t = x;
	t ^= t << 11;
	t ^= t >> 8;
	x = y;
	y = z;
	z = w;
	w ^= w >> 19;
	w ^= t;
	return w;
}


// this function flushes the cache.
static int flush_the_cache() {
    size_t kb_to_flush = 64 * 1024;
    char* buffer = (char*) malloc(kb_to_flush);
    if (buffer == nullptr) {
        printf("Failed to allocate for cache flush.\n");
        return EXIT_FAILURE;
    }
    for (size_t i = 0; i < kb_to_flush; i++) {
        buffer[i] = (char) i;
    }
    free(buffer);
    return EXIT_SUCCESS;
}

// linux wrapper function to open a perf event.
static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                             int cpu, int group_fd, unsigned long flags) {
	int ret;
	ret = syscall(SYS_perf_event_open, hw_event, pid, cpu,
                         group_fd, flags);
	return ret;
}

// global var to change access patterns.
int opt_random_access = 1;

#define CACHE_LINE_SIZE 64
#define MEM_SIZE (1024 * 1024 * 1024)

// p points to a region that is 1GB (ideally)
void do_mem_access(char* p, int size) {
	int i, j, count, outer, locality;
   int ws_base = 0;
   int max_base = ((size / CACHE_LINE_SIZE) - 512);
	for(outer = 0; outer < (1<<20); ++outer) {
      long r = simplerand() % max_base;
      // Pick a starting offset
      if (opt_random_access) {
         ws_base = r;
      } else {
         ws_base += 512;
         if (ws_base >= max_base) {
            ws_base = 0;
         }
      }
      for(locality = 0; locality < 16; locality++) {
         volatile char *a;
         char c;
         for (i = 0; i < 512; i++) {
            // Working set of 512 cache lines, 32KB
            a = p + (ws_base + i) * CACHE_LINE_SIZE;
            if((i % 8) == 0) {
               *a = 1;
            } else {
               c = *a;
            }
         }
      }
   }
}

// main execution thread.
int main() {


    // (1) lock the program to a specific CPU.
    int cpu_id = 4;
    pid_t pid = 0;
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cpu_id, &mask);
    size_t cpusetsize = sizeof(mask);
    if (sched_setaffinity(pid, cpusetsize, &mask) == -1) {
        perror("Oh no. CPU Set Operation Failed.");
        return EXIT_FAILURE;
    } else {
        printf("CPU Set Operation Successful.\n");
    }  

    int trials = 5; 
    for (int i = 0; i < trials; i++) {

        // (2) flush the cache.
        int result = flush_the_cache();
        if (result == EXIT_FAILURE) {
            perror("Oh no. Cache Flush Failed.");
            return EXIT_FAILURE;
        } else {
            printf("Cache Flush Successful.\n");
        }

        
        // (3) allocate memory pointer for accessing.
        char* p = (char*) malloc(MEM_SIZE);
        if (p == nullptr) {
            perror("Failure in malloc of pointer p.");
            return EXIT_FAILURE;
        } else {
            printf("Success in malloc of pointer p.\n");
            printf("Beginning memory access...\n");
            printf("------------------------\n");
        }

        //   L1-dcache-load-misses                              [Hardware cache event]
        //   L1-dcache-loads                                    [Hardware cache event]
        //   L1-dcache-prefetch-misses                          [Hardware cache event]
        //   L1-dcache-prefetches                               [Hardware cache event]
        //   L1-dcache-store-misses                             [Hardware cache event]
        //   L1-dcache-stores                                   [Hardware cache event]
        //   ... 
        //   dTLB-load-misses                                   [Hardware cache event]
        //   dTLB-loads                                         [Hardware cache event]
        //   dTLB-store-misses                                  [Hardware cache event]
        //   dTLB-stores                                        [Hardware cache event]

        char buf[4096];
        struct read_format* rf = (struct read_format*) buf;

        // CREATE A LEADER EVENT.
        int fd_leader;
        uint64_t id_leader;
        struct perf_event_attr pe_leader;
        memset(&pe_leader, 0, sizeof(pe_leader));
        pe_leader.type = PERF_TYPE_HARDWARE;
        pe_leader.size = sizeof(pe_leader);
        pe_leader.config = PERF_TYPE_HW_CACHE;
        // LEADER IS DISABLED!
        pe_leader.disabled = 1;
        pe_leader.exclude_kernel = 1;
        pe_leader.exclude_hv = 1;
        pe_leader.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;
        fd_leader = perf_event_open(&pe_leader, 0, -1, -1, 0);
        ioctl(fd_leader, PERF_EVENT_IOC_ID, &id_leader);
        if (fd_leader == -1) {
            perror("Perf Event Open Failed. L1 HW Cache Leader Event. \n");
            return EXIT_FAILURE;
        } else {
            printf("Perf Event Open Successful.\n");
            printf("L1 HW Cache Leader Event.\n");
            printf("------------------------\n");

        }


        // L1 HW CACHE READ MISS
        int fd;
        uint64_t id;
        long long count;
        struct perf_event_attr pe;
        memset(&pe, 0, sizeof(pe));
        // measuring hardware CPU cache event.
        pe.type = PERF_TYPE_HW_CACHE;
        pe.size = sizeof(pe);
        // to calculate the appropriate config, we use this formula:
        // config = (perf_hw_cache_id) | (perf_hw_cache_op_id << 8) | (perf_hw_cache_op_result_id << 16);
        pe.config = (PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16));
        pe.disabled = 0; 
        pe.exclude_kernel = 1;
        pe.exclude_hv = 1;
        pe.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;
        fd = perf_event_open(&pe, 0, -1, fd_leader, 0);
        ioctl(fd, PERF_EVENT_IOC_ID, &id);
        if (fd == -1) {
            perror("Perf Event Open Failed. L1 HW Cache Read Misses. \n");
            return EXIT_FAILURE;
        } else {
            printf("Perf Event Open Successful.\n");
            printf("L1 HW Cache Read Misses.\n");
            printf("------------------------\n");

        }

        // L1 HW CACHE READ ACCESS
        int fd_2;
        long long count_2;
        uint64_t id_2;
        struct perf_event_attr pe_2;
        memset(&pe_2, 0, sizeof(pe_2));
        // measuring hardware CPU cache event.
        pe_2.type = PERF_TYPE_HW_CACHE;
        pe_2.size = sizeof(pe_2);
        pe_2.config = (PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16));
        pe_2.disabled = 0;
        pe_2.exclude_kernel = 1;
        pe_2.exclude_hv = 1;
        pe_2.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;
        fd_2 = perf_event_open(&pe_2, 0, -1, fd_leader, 0);
        ioctl(fd_2, PERF_EVENT_IOC_ID, &id_2);
        if (fd_2 == -1) {
            perror("Perf Event Open Failed. L1 HW Cache Read Accesses. \n");
            return EXIT_FAILURE;
        } else {
            printf("Perf Event Open Successful.\n");
            printf("L1 HW Cache Read Accesses.\n");
            printf("------------------------\n");
        }

        // L1 HW CACHE WRITE MISS
        int fd_3;
        uint64_t id_3;
        long long count_3;
        struct perf_event_attr pe_3;
        memset(&pe_3, 0, sizeof(pe_3));
        // measuring hardware CPU cache event.
        pe_3.type = PERF_TYPE_HW_CACHE;
        pe_3.size = sizeof(pe_3);
        pe_3.config = (PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_WRITE << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16));
        pe_3.disabled = 0;
        pe_3.exclude_kernel = 1;
        pe_3.exclude_hv = 1;
        pe_3.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;
        fd_3 = perf_event_open(&pe_3, 0, -1, fd_leader, 0);
        ioctl(fd_3, PERF_EVENT_IOC_ID, &id_3);
        if (fd_3 == -1) {
            perror("Perf Event Open Failed. L1 HW Cache Write Misses. \n");
            return EXIT_FAILURE;
        } else {
            printf("Perf Event Open Successful.\n");
            printf("L1 HW Cache Write Misses.\n");
            printf("------------------------\n");
        }

        // CREATE A NEW LEADER EVENT.
        int fd_leader_2;
        uint64_t id_leader_2;
        struct perf_event_attr pe_leader_2;
        memset(&pe_leader_2, 0, sizeof(pe_leader_2));
        pe_leader_2.type = PERF_TYPE_HARDWARE;
        pe_leader_2.size = sizeof(pe_leader_2);
        pe_leader_2.config = PERF_TYPE_HW_CACHE;
        // LEADER IS DISABLED!
        pe_leader_2.disabled = 1;
        pe_leader_2.exclude_kernel = 1;
        pe_leader_2.exclude_hv = 1;
        pe_leader_2.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;
        fd_leader_2 = perf_event_open(&pe_leader_2, 0, -1, -1, 0);
        ioctl(fd_leader_2, PERF_EVENT_IOC_ID, &id_leader_2);
        if (fd_leader_2 == -1) {
            perror("Perf Event Open Failed. L1 HW Cache Leader Event. \n");
            return EXIT_FAILURE;
        } else {
            printf("Perf Event Open Successful.\n");
            printf("L1 HW Cache Leader Event.\n");
            printf("------------------------\n");
        }

        // L1 HW CACHE WRITE ACCESS
        int fd_4;
        uint64_t id_4;
        long long count_4;
        struct perf_event_attr pe_4;
        memset(&pe_4, 0, sizeof(pe_4));
        // measuring hardware CPU cache event.
        pe_4.type = PERF_TYPE_HW_CACHE;
        pe_4.size = sizeof(pe_4);
        pe_4.config = PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_WRITE << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16);
        pe_4.disabled = 0;
        pe_4.exclude_kernel = 1;
        pe_4.exclude_hv = 1;
        pe_4.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;
        fd_4 = perf_event_open(&pe_4, 0, -1, fd_leader_2, 0);
        ioctl(fd_4, PERF_EVENT_IOC_ID, &id_4);
        if (fd_4 == -1) {
            perror("Perf Event Open Failed. L1 HW Cache Write Accesses. \n");
            return EXIT_FAILURE;
        } else {
            printf("Perf Event Open Successful.\n");
            printf("L1 HW Cache Write Accesses.\n");
            printf("------------------------\n");
        }

        // L1 HW CACHE PREFETCH MISS
        int fd_5;
        uint64_t id_5;
        long long count_5;
        struct perf_event_attr pe_5;
        memset(&pe_5, 0, sizeof(pe_5));
        // measuring hardware CPU cache event.
        pe_5.type = PERF_TYPE_HW_CACHE;
        pe_5.size = sizeof(pe_5);
        pe_5.config = (PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_PREFETCH << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16));
        pe_5.disabled = 0;
        pe_5.exclude_kernel = 1;
        pe_5.exclude_hv = 1;
        pe_5.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;
        fd_5 = perf_event_open(&pe_5, 0, -1, fd_leader_2, 0);
        ioctl(fd_5, PERF_EVENT_IOC_ID, &id_5);
        if (fd_5 == -1) {
            perror("Perf Event Open Failed. L1 HW Cache Prefetch Misses. \n");
            return EXIT_FAILURE;
        } else {
            printf("Perf Event Open Successful.\n");
            printf("L1 HW Cache Prefetch Misses.\n");
            printf("------------------------\n");
        }

        // L1 HW CACHE PREFETCH ACCESS
        int fd_6;
        uint64_t id_6;
        long long count_6;
        struct perf_event_attr pe_6;
        memset(&pe_6, 0, sizeof(pe_6));
        // measuring hardware CPU cache event.
        pe_6.type = PERF_TYPE_HW_CACHE;
        pe_6.size = sizeof(pe_6);
        pe_6.config = (PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_PREFETCH << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16));
        pe_6.disabled = 0;
        pe_6.exclude_kernel = 1;
        pe_6.exclude_hv = 1;
        pe_6.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;
        fd_6 = perf_event_open(&pe_6, 0, -1, fd_leader_2, 0);
        ioctl(fd_6, PERF_EVENT_IOC_ID, &id_6);
        if (fd_6 == -1) {
            perror("Perf Event Open Failed. L1 HW Cache Prefetch Accesses. \n");
            // return EXIT_FAILURE;
        } else {
            printf("Perf Event Open Successful.\n");
            printf("L1 HW Cache Prefetch Accesses.\n");
            printf("------------------------\n");
        }

        // CREATE A LEADER EVENT.
        int fd_leader_3;
        uint64_t id_leader_3;
        struct perf_event_attr pe_leader_3;
        memset(&pe_leader_3, 0, sizeof(pe_leader_3));
        pe_leader_3.type = PERF_TYPE_HARDWARE;
        pe_leader_3.size = sizeof(pe_leader_3);
        pe_leader_3.config = PERF_TYPE_HW_CACHE;
        // LEADER IS DISABLED!
        pe_leader_3.disabled = 1;
        pe_leader_3.exclude_kernel = 1;
        pe_leader_3.exclude_hv = 1;
        pe_leader_3.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;
        fd_leader_3 = perf_event_open(&pe_leader_3, 0, -1, -1, 0);
        ioctl(fd_leader_3, PERF_EVENT_IOC_ID, &id_leader_3);
        if (fd_leader_3 == -1) {
            perror("Perf Event Open Failed. Data TLB Leader Event. \n");
            return EXIT_FAILURE;
        } else {
            printf("Perf Event Open Successful.\n");
            printf("Data TLB Leader Event.\n");
            printf("------------------------\n");
        }


        // Data TLB Load Misses
        int fd_7;
        uint64_t id_7;
        long long count_7;
        struct perf_event_attr pe_7;
        memset(&pe_7, 0, sizeof(pe_7));
        // measuring hardware CPU cache event.
        pe_7.type = PERF_TYPE_HW_CACHE;
        pe_7.size = sizeof(pe_7);
        pe_7.config = (PERF_COUNT_HW_CACHE_DTLB | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16));
        pe_7.disabled = 0;
        pe_7.exclude_kernel = 1;
        pe_7.exclude_hv = 1;
        pe_7.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;
        fd_7 = perf_event_open(&pe_7, 0, -1, fd_leader_3, 0);
        ioctl(fd_7, PERF_EVENT_IOC_ID, &id_7);
        if (fd_7 == -1) {
            perror("Perf Event Open Failed. Data TLB Load Misses. \n");
            return EXIT_FAILURE;
        } else {
            printf("Perf Event Open Successful.\n");
            printf("Data TLB Load Misses.\n");
            printf("------------------------\n");
        }

        // Data TLB Load Accesses
        int fd_8;
        uint64_t id_8;
        long long count_8;
        struct perf_event_attr pe_8;
        memset(&pe_8, 0, sizeof(pe_8));
        // measuring hardware CPU cache event.
        pe_8.type = PERF_TYPE_HW_CACHE;
        pe_8.size = sizeof(pe_8);
        pe_8.config = (PERF_COUNT_HW_CACHE_DTLB | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16));
        pe_8.disabled = 0;
        pe_8.exclude_kernel = 1;
        pe_8.exclude_hv = 1;
        pe_8.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;
        fd_8 = perf_event_open(&pe_8, 0, -1, fd_leader_3, 0);
        ioctl(fd_8, PERF_EVENT_IOC_ID, &id_8);
        if (fd_8 == -1) {
            perror("Perf Event Open Failed.");
            return EXIT_FAILURE;
        } else {
            printf("Perf Event Open Successful. \n");
            printf("Data TLB Load Accesses.\n");
            printf("------------------------\n");
        }

        // Data TLB Store Misses
        int fd_9;
        uint64_t id_9;
        long long count_9;
        struct perf_event_attr pe_9;
        memset(&pe_9, 0, sizeof(pe_9));
        // measuring hardware CPU cache event.
        pe_9.type = PERF_TYPE_HW_CACHE;
        pe_9.size = sizeof(pe_9);
        pe_9.config = (PERF_COUNT_HW_CACHE_DTLB | (PERF_COUNT_HW_CACHE_OP_WRITE << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16));
        pe_9.disabled = 0;
        pe_9.exclude_kernel = 1;
        pe_9.exclude_hv = 1;
        pe_9.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;
        fd_9 = perf_event_open(&pe_9, 0, -1, fd_leader_3, 0);
        ioctl(fd_9, PERF_EVENT_IOC_ID, &id_9);
        if (fd_9 == -1) {
            perror("Perf Event Open Failed. Data TLB Store Misses");
            return EXIT_FAILURE;
        } else {
            printf("Perf Event Open Successful.\n");
            printf("Data TLB Store Misses.\n");
            printf("------------------------\n");
        }

        int fd_leader_4;
        uint64_t id_leader_4;
        struct perf_event_attr pe_leader_4;
        memset(&pe_leader_4, 0, sizeof(pe_leader_4));
        pe_leader_4.type = PERF_TYPE_HARDWARE;
        pe_leader_4.size = sizeof(pe_leader_4);
        pe_leader_4.config = PERF_TYPE_HW_CACHE;
        // LEADER IS DISABLED!
        pe_leader_4.disabled = 1;
        pe_leader_4.exclude_kernel = 1;
        pe_leader_4.exclude_hv = 1;
        pe_leader_4.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;
        fd_leader_4 = perf_event_open(&pe_leader_4, 0, -1, -1, 0);
        ioctl(fd_leader_4, PERF_EVENT_IOC_ID, &id_leader_4);
        if (fd_leader_4 == -1) {
            perror("Perf Event Open Failed. Data TLB Leader Event. \n");
            return EXIT_FAILURE;
        } else {
            printf("Perf Event Open Successful.\n");
            printf("Data TLB Leader Event.\n");
            printf("------------------------\n");
        }

        // Data TLB Store Accesses
        int fd_10;
        uint64_t id_10;
        long long count_10;
        struct perf_event_attr pe_10;
        memset(&pe_10, 0, sizeof(pe_10));
        // measuring hardware CPU cache event.
        pe_10.type = PERF_TYPE_HW_CACHE;
        pe_10.size = sizeof(pe_10);
        pe_10.config = (PERF_COUNT_HW_CACHE_DTLB | (PERF_COUNT_HW_CACHE_OP_WRITE << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16));
        pe_10.disabled = 0;
        pe_10.exclude_kernel = 1;
        pe_10.exclude_hv = 1;
        pe_10.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID; 
        fd_10 = perf_event_open(&pe_10, 0, -1, fd_leader_4, 0);
        ioctl(fd_10, PERF_EVENT_IOC_ID, &id_10);
        if (fd_10 == -1) {
            perror("Perf Event Open Failed. Data TLB Store Accesses.");
            return EXIT_FAILURE;
        } else {
            printf("Perf Event Open Successful.\n");
            printf("Data TLB Store Accesses.\n");
            printf("------------------------\n");
        }

    FILE *file = fopen("metrics.csv", "w");
    if (file == nullptr) {
        perror("Error in system call fopen");
        return EXIT_FAILURE;
    } 

    fprintf(file, "L1D Read Misses, L1D Read Accesses, L1D Write Misses, L1D Write Accesses, L1D Prefetch Misses, L1D Prefetch Accesses, DTLB Load Misses, DTLB Load Accesses, DTLB Store Misses, DTLB Store Accesses\n");


      // RESOURCE USAGE PRIOR TO I/O CONTROL + FUNCTION CALL
    struct rusage ru;
    // printf("------------------------\n");
    // printf("Resource Usage Prior to I/O Control + Function Call\n");
    // fprintf(file, "Resource Usage Prior to I/O Control + Function Call\n");
    printf("------------------------\n");
	if (getrusage(RUSAGE_SELF, &ru) != 0) {
    		perror("Error in system call getrusage");
    		return EXIT_FAILURE;
	} else {
    		// printf("Utime (tv_sec): %ld.%09ld \n", ru.ru_utime.tv_sec);
            // fprintf(file, "%ld.%09ld, ", ru.ru_utime.tv_sec, ru.ru_utime.tv_usec);
    		// printf("Utime (tv_usec): %ld.%09ld \n", ru.ru_utime.tv_usec);
            // fprintf(file, "%ld.%09ld, ", ru.ru_utime.tv_sec, ru.ru_utime.tv_usec);
    		// printf("Stime (tv_sec): %ld.%09ld \n", ru.ru_stime.tv_sec);
            // fprintf(file, "%ld.%09ld, ", ru.ru_stime.tv_sec, ru.ru_stime.tv_usec);
    		// printf("Stime (tv_usec): %ld.%09ld \n", ru.ru_stime.tv_usec);
            // fprintf(file, "%ld.%09ld, ", ru.ru_stime.tv_sec, ru.ru_stime.tv_usec);
    		// printf("Maxrss: %ld \n", ru.ru_maxrss);
            // fprintf(file, "%ld, ", ru.ru_maxrss);
    		// printf("Minflt: %ld \n", ru.ru_minflt);
            // fprintf(file, "%ld, ", ru.ru_minflt);
    		// printf("Majflt: %ld \n", ru.ru_majflt);
            // fprintf(file, "%ld, ", ru.ru_majflt);
    		// printf("Inblock: %ld \n", ru.ru_inblock);
            // fprintf(file, "%ld, ", ru.ru_inblock);
    		// printf("Outblock: %ld \n", ru.ru_oublock);
            // fprintf(file, "%ld, ", ru.ru_oublock);
    		// printf("Voluntary C.S: %ld \n", ru.ru_nvcsw);
            // fprintf(file, "%ld, ", ru.ru_nvcsw);
    		// printf("Involuntary C.S: %ld \n", ru.ru_nivcsw);
            // fprintf(file, "%ld, ", ru.ru_nivcsw);
            printf("%ld.%09ld\n", ru.ru_utime.tv_sec, ru.ru_utime.tv_usec);
            printf("%ld.%09ld\n", ru.ru_utime.tv_usec);
            printf("%ld.%09ld\n", ru.ru_stime.tv_sec, ru.ru_stime.tv_usec);
            printf("%ld.%09ld\n", ru.ru_stime.tv_usec);
            printf("%ld\n", ru.ru_maxrss);
            printf("%ld\n", ru.ru_minflt);
            printf("%ld\n", ru.ru_majflt);
            printf("%ld\n", ru.ru_inblock);
            printf("%ld\n", ru.ru_oublock);
            printf("%ld\n", ru.ru_nvcsw);
            printf("%ld\n", ru.ru_nivcsw);
    }

        // begin i/o control, leader controls all flow.
        ioctl(fd_leader, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
        ioctl(fd_leader_2, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
        ioctl(fd_leader_3, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
        ioctl(fd_leader_4, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);

        ioctl(fd_leader, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
        ioctl(fd_leader_2, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
        ioctl(fd_leader_3, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
        ioctl(fd_leader_4, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);

        
        do_mem_access(p, MEM_SIZE);

        ioctl(fd_leader, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
        ioctl(fd_leader_2, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
        ioctl(fd_leader_3, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
        ioctl(fd_leader_4, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);

        // RESOURCE USAGE AFTER I/O + FUNCTION CALL
        struct rusage ru_2;
        // printf("------------------------\n");
        // printf("Resource Usage After I/O Control + Function Call\n");
        // fprintf(file, "Resource Usage After I/O Control + Function Call\n");
     
        if (getrusage(RUSAGE_SELF, &ru_2) != 0) {
                perror("Error in system call getrusage");
                return EXIT_FAILURE;
        } else {
                // printf("Utime (tv_sec): %ld.%09ld \n", ru_2.ru_utime.tv_sec);
                // fprintf(file, "%ld.%09ld, ", ru_2.ru_utime.tv_sec, ru_2.ru_utime.tv_usec);
                // printf("Utime (tv_usec): %ld.%09ld \n", ru_2.ru_utime.tv_usec);
                // fprintf(file, "%ld.%09ld, ", ru_2.ru_utime.tv_sec, ru_2.ru_utime.tv_usec);
                // printf("Stime (tv_sec): %ld.%09ld \n", ru_2.ru_stime.tv_sec);
                // fprintf(file, "%ld.%09ld, ", ru_2.ru_stime.tv_sec, ru_2.ru_stime.tv_usec);
                // printf("Stime (tv_usec): %ld.%09ld \n", ru_2.ru_stime.tv_usec);
                // fprintf(file, "%ld.%09ld, ", ru_2.ru_stime.tv_sec, ru_2.ru_stime.tv_usec);
                // printf("Maxrss: %ld \n", ru_2.ru_maxrss);
                // fprintf(file, "%ld, ", ru_2.ru_maxrss);
                // printf("Minflt: %ld \n", ru_2.ru_minflt);
                // fprintf(file, "%ld, ", ru_2.ru_minflt);
                // printf("Majflt: %ld \n", ru_2.ru_majflt);
                // fprintf(file, "%ld, ", ru_2.ru_majflt);
                // printf("Inblock: %ld \n", ru_2.ru_inblock);
                // fprintf(file, "%ld, ", ru_2.ru_inblock);
                // printf("Outblock: %ld \n", ru_2.ru_oublock);
                // fprintf(file, "%ld, ", ru_2.ru_oublock);
                // printf("Voluntary C.S: %ld \n", ru_2.ru_nvcsw);
                // fprintf(file, "%ld, ", ru_2.ru_nvcsw);
                // printf("Involuntary C.S: %ld \n", ru_2.ru_nivcsw);
                // fprintf(file, "%ld, ", ru_2.ru_nivcsw);
                printf("%ld.%09ld\n", ru.ru_utime.tv_sec, ru.ru_utime.tv_usec);
                printf("%ld.%09ld\n", ru.ru_utime.tv_usec);
                printf("%ld.%09ld\n", ru.ru_stime.tv_sec, ru.ru_stime.tv_usec);
                printf("%ld.%09ld\n", ru.ru_stime.tv_usec);
                printf("%ld\n", ru.ru_maxrss);
                printf("%ld\n", ru.ru_minflt);
                printf("%ld\n", ru.ru_majflt);
                printf("%ld\n", ru.ru_inblock);
                printf("%ld\n", ru.ru_oublock);
                printf("%ld\n", ru.ru_nvcsw);
                printf("%ld\n", ru.ru_nivcsw);
        }

        uint64_t val1, val2, val3, val4, val5, val6, val7, val8, val9, val10;
        
        read(fd_leader, buf, sizeof(buf));
        for (int i = 0; i < rf->nr; i++) {
            if (rf->values[i].id == id) {
                val1 = rf->values[i].value;
            } else if (rf->values[i].id == id_2) {
                val2 = rf->values[i].value;
            } else if (rf->values[i].id == id_3) {
                val3 = rf->values[i].value;
            }
        }

        read(fd_leader_2, buf, sizeof(buf));
        for (int i = 0; i < rf->nr; i++) {
            if (rf->values[i].id == id_4) {
                val4 = rf->values[i].value;
            } else if (rf->values[i].id == id_5) {
                val5 = rf->values[i].value;
            } else if (rf->values[i].id == id_6) {
                val6 = rf->values[i].value;
            }
        }

        read(fd_leader_3, buf, sizeof(buf));
        for (int i = 0; i < rf->nr; i++) {
            if (rf->values[i].id == id_7) {
                val7 = rf->values[i].value;
            } else if (rf->values[i].id == id_8) {
                val8 = rf->values[i].value;
            } else if (rf->values[i].id == id_9) {
                val9 = rf->values[i].value;
            }
        }

        read(fd_leader_4, buf, sizeof(buf));
        for (int i = 0; i < rf->nr; i++) {
            if (rf->values[i].id == id_10) {
                val10 = rf->values[i].value;
            }
        }

        printf("%" PRIu64 "\n", val1);
        printf("%" PRIu64 "\n", val2);
        printf("%" PRIu64 "\n", val3);
        printf("%" PRIu64 "\n", val4);
        printf("%" PRIu64 "\n", val5);
        printf("%" PRIu64 "\n", val6);
        printf("%" PRIu64 "\n", val7);
        printf("%" PRIu64 "\n", val8);
        printf("%" PRIu64 "\n", val9);
        printf("%" PRIu64 "\n", val10);
        // printf("------------------------\n");
        // printf("Current Values for Trial %d\n", i);
        // fprintf(file, "Current Values for Trial %d\n", i);
        // printf("------------------------\n");
        // printf("L1 HW Cache Read Misses: %" PRIu64 "\n", val1);
        // fprintf(file, "L1 HW Cache Read Misses,%" PRIu64 "\n", val1);
        // printf("L1 HW Cache Read Accesses: %" PRIu64 "\n", val2);
        // fprintf(file, "L1 HW Cache Read Accesses,%" PRIu64 "\n", val2);
        // printf("L1 HW Cache Write Misses: %" PRIu64 "\n", val3);
        // fprintf(file, "L1 HW Cache Write Misses,%" PRIu64 "\n", val3);
        // printf("L1 HW Cache Write Accesses: %" PRIu64 "\n", val4);
        // fprintf(file, "L1 HW Cache Write Accesses,%" PRIu64 "\n", val4);
        // printf("L1 HW Cache Prefetch Misses: %" PRIu64 "\n", val5);
        // fprintf(file, "L1 HW Cache Prefetch Misses,%" PRIu64 "\n", val5);
        // printf("L1 HW Cache Prefetch Accesses: %" PRIu64 "\n", val6);
        // fprintf(file, "L1 HW Cache Prefetch Accesses,%" PRIu64 "\n", val6);
        // printf("Data TLB Load Misses: %" PRIu64 "\n", val7);
        // fprintf(file, "Data TLB Load Misses,%" PRIu64 "\n", val7);
        // printf("Data TLB Load Accesses: %" PRIu64 "\n", val8);
        // fprintf(file, "Data TLB Load Accesses,%" PRIu64 "\n", val8);
        // printf("Data TLB Store Misses: %" PRIu64 "\n", val9);
        // fprintf(file, "Data TLB Store Misses,%" PRIu64 "\n", val9);
        // printf("Data TLB Store Accesses: %" PRIu64 "\n", val10);
        // fprintf(file, "Data TLB Store Accesses,%" PRIu64 "\n", val10);
        // printf("------------------------\n");
        printf("------------------------\n");
        close(fd_leader);
        close(fd_leader_2);
        close(fd_leader_3);
        close(fd_leader_4);
        fclose(file);

        // deallocate
        // deallocate memory pointer.
        free(p);
        // if (munmap(p, MEM_SIZE) == -1) {
        //     perror("Oh no. Memory Deallocation Failed.");
        //     return EXIT_FAILURE;
        // } else {
        //     printf("Memory Deallocation Successful.\n");
        // }

        printf("------------------------\n");
        printf("Trial %d complete.\n", i);
        printf("------------------------\n");
    }


    // standard deviation of the values.
    
    printf("All Trials Complete.\n");
    
	return EXIT_SUCCESS;
}