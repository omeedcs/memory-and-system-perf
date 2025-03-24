	struct rusage ru;
	if (getrusage(RUSAGE_SELF, &ru) != 0) {
    		perror("Error in system call getrusage");
    		return EXIT_FAILURE;
	} else {
    		printf("Utime (tv_sec): %ld.%09ld \n", ru.ru_utime.tv_sec);
    		printf("Utime (tv_usec): %ld.%09ld \n", ru.ru_utime.tv_usec);
    		printf("Stime (tv_sec): %ld.%09ld \n", ru.ru_stime.tv_sec);
    		printf("Stime (tv_usec): %ld.%09ld \n", ru.ru_stime.tv_usec);
    		printf("Maxrss: %ld \n", ru.ru_maxrss);
    		printf("Minflt: %ld \n", ru.ru_minflt);
    		printf("Majflt: %ld \n", ru.ru_majflt);
    		printf("Inblock: %ld \n", ru.ru_inblock);
    		printf("Outblock: %ld \n", ru.ru_oublock);
    		printf("Voluntary C.S: %ld \n", ru.ru_nvcsw);
    		printf("Involuntary C.S: %ld \n", ru.ru_nivcsw);
}