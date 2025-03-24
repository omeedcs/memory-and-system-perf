#include <stdio.h>
#include <stdlib.h>
// allows us to use affinity and getcpu() to test.
#include <sched.h>
#include <unistd.h>

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

int main() {

	int result = flush_the_cache();
	if (result == EXIT_SUCCESS) {
		printf("flushed successfully\n");

	}
	int cpu_id = 4;
	pid_t pid = 0;
	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(cpu_id, &mask);
	size_t cpusetsize = sizeof(mask);
	if(sched_setaffinity(pid, cpusetsize, &mask) == -1) {
		perror("Oh no. CPU Set Operation Failed.");
		return EXIT_FAILURE;
	}

	// some basic operations :)
        for (int i = 0; i < 10; i++) {
                printf("%d", i);
        }
        printf("\n");
        for (int i = 0; i < 10; i++) {
                printf("%d", i);
        }
        printf("\n");

	printf("Basic Operations Complete. \n");
	printf("CPU that was used: %d\n", sched_getcpu());

	return 0;
}