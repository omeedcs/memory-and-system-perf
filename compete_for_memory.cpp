#include <stdio.h>
#include <stdlib.h>
// allows us to use affinity and getcpu() to test.
#include <sched.h>
#include <unistd.h>
#include <iostream>
#include <numeric> // For std::accumulate
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <math.h>
// include for mmap
#include <sys/mman.h>




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

long get_mem_size() {
    long page_sz = sysconf(_SC_PAGE_SIZE);
    long physical_pages = sysconf(_SC_PHYS_PAGES);
    return physical_pages * page_sz;
}

int compete_for_memory(void* unused) {
long mem_size = get_mem_size();
int page_sz = sysconf(_SC_PAGE_SIZE);
printf("Total memsize is %3.2f GBs\n", (double)mem_size/(1024*1024*1024));
fflush(stdout);
char* p = (char*) mmap(NULL, mem_size, PROT_READ | PROT_WRITE,
                MAP_NORESERVE|MAP_PRIVATE|MAP_ANONYMOUS, -1, (off_t) 0);
if (p == MAP_FAILED)
    perror("Failed anon MMAP competition");

int i = 0;
while(1) {
    volatile char *a;
    long r = simplerand() % (mem_size/page_sz);
    char c;
    if( i >= mem_size/page_sz ) {
        i = 0;
    }
    // One read and write per page
    //a = p + i * page_sz; // sequential access
    a = p + r * page_sz;
    c += *a;
    if((i%8) == 0) {
        *a = 1;
    }
    i++;
}
return 0;
}

int main() {
    // Set the CPU affinity to CPU 6
    fork() 

    int cpu_id = 6;
    pid_t pid = 0;
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cpu_id, &mask);
    size_t cpusetsize = sizeof(mask);
    if(sched_setaffinity(pid, cpusetsize, &mask) == -1) {
        perror("Oh no. CPU Set Operation Failed.");
        return EXIT_FAILURE;
    }

    // unused pointer
    void* unused = NULL;
    compete_for_memory(unused);
}