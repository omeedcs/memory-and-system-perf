#include <stdio.h>
#include <stdlib.h>
// allows us to use affinity and getcpu() to test.
#include <sched.h>
#include <unistd.h>
#include <iostream>
#include <numeric> // For std::accumulate

int main() {
    int cpu_id = 5;
    pid_t pid = 0;
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cpu_id, &mask);
    size_t cpusetsize = sizeof(mask);
    if(sched_setaffinity(pid, cpusetsize, &mask) == -1) {
        perror("Oh no. CPU Set Operation Failed.");
        return EXIT_FAILURE;
    }

    while (true) {
        // allocate some memory
        size_t arraySize = 1000000;
        int* array = (int*) malloc(arraySize * sizeof(int));
        if (array == nullptr) {
            printf("Failed to allocate for cache flush.\n");
            return EXIT_FAILURE;
        }
        // fill the array with some values
        for (size_t i = 0; i < arraySize; i++) {
            array[i] = i;
        }
        // iterate over the array and sum the values
        for (size_t i = 0; i < arraySize; i++) {
            array[i] = array[i] + 1;
        }
        // free the memory
        free(array);
        return 0;
        
    }

}