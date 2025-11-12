# eBPF-Network-Filter-Research-NCSU
Time implementation

// Declare a time_t variable
    time_t rawtime;

    // Use the time() function to get the current time
    // It stores the result in the memory location pointed to by rawtime
    time(&rawtime);

    // You can print the raw time as a number of seconds since the epoch (Jan 1, 1970)
    printf("Seconds since epoch: %ld\n", rawtime);

    // To get a human-readable string, use ctime()
    printf("Current date and time: %s", ctime(&rawtime));
