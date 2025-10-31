#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

char *conv_sec(double t, char *st) {
    int h, m, s;

    s = (int)floor(t);
    if (t - s >= 0.5) s++;
    m = (int)floor(s / 60.0);
    s -= m * 60;
    h = (int)floor(m / 60.0);
    m -= h * 60;

    sprintf(st, "%dh %dm %ds", h, m, s);

    return st;
}

static double T_START = 0.0;

double get_time_sec() {
    struct timeval t;
    gettimeofday(&t, NULL);
    double time_in_sec = (double)t.tv_sec + (double)t.tv_usec / 1000000.0;

    // If this is the first time we're called, set the global start time.
    if (T_START == 0.0) {
        T_START = time_in_sec;
    }

    // Return time elapsed since the first call.
    return time_in_sec - T_START;
}

double get_wall_time() {
    struct timeval time;
    if (gettimeofday(&time, NULL)) {
        // Handle error
        return 0;
    }
    return (double)time.tv_sec + (double)time.tv_usec * .000001;
}

double get_cpu_time() { return (double)clock() / CLOCKS_PER_SEC; }

void print_proc_io() {
    int pid = getpid();
    char path[200];
    sprintf(path, "/proc/%d/io", pid);

    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        printf("I/O data unavailable\n");
        return;
    }

    long rchar = 0, wchar = 0, syscr = 0, syscw = 0, read_bytes = 0, write_bytes = 0;
    char line[256];

    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "rchar:"))
            sscanf(line, "rchar: %ld", &rchar);
        else if (strstr(line, "wchar:"))
            sscanf(line, "wchar: %ld", &wchar);
        else if (strstr(line, "syscr:"))
            sscanf(line, "syscr: %ld", &syscr);
        else if (strstr(line, "syscw:"))
            sscanf(line, "syscw: %ld", &syscw);
        else if (strstr(line, "read_bytes:"))
            sscanf(line, "read_bytes: %ld", &read_bytes);
        else if (strstr(line, "write_bytes:"))
            sscanf(line, "write_bytes: %ld", &write_bytes);
    }
    fclose(fp);

    printf("Process I/O:\n");
    printf("  Characters read:           %'12ld\n", rchar);
    printf("  Characters written:        %'12ld\n", wchar);
    printf("  Read syscalls:             %'12ld\n", syscr);
    printf("  Write syscalls:            %'12ld\n", syscw);
    printf("  Bytes read:                %'12ld (%'.1f MB)\n", read_bytes, read_bytes / (1024.0 * 1024.0));
    printf("  Bytes written:             %'12ld (%'.1f MB)\n", write_bytes,
           write_bytes / (1024.0 * 1024.0));
}

// Global variables to store previous I/O counters for difference calculation
long long io_counter_sda[11];
long long io_counter_sda_new[11];
long long io_counter_sdc[11];
long long io_counter_sdc_new[11];
int io_counters_initialized = 0;

void init_disk_io() {
    FILE *in = fopen("/proc/diskstats", "r");
    if (in == NULL) {
        return;
    }

    char word[256];
    int count = -1;
    int type = -1;  // 0 for sdc (swap), 1 for sdd (main disk)

    while (fscanf(in, "%s", word) != EOF) {
        if (count >= 0 && count < 11) {
            if (type == 0) {
                io_counter_sda[count] = atoll(word);  // Using sda array for sdc data
            } else if (type == 1) {
                io_counter_sdc[count] = atoll(word);  // Using sdc array for sdd data
            }
            count++;
        }
        if (strcmp(word, "sdc") == 0) {  // Monitor swap disk (memory pressure)
            type = 0;
            count = 0;
        } else if (strcmp(word, "sdd") == 0) {  // Monitor main disk (file I/O)
            type = 1;
            count = 0;
        }
    }
    fclose(in);
    io_counters_initialized = 1;
}

void print_disk_io() {
    FILE *in = fopen("/proc/diskstats", "r");
    if (in == NULL) {
        printf("Disk I/O data unavailable\n");
        return;
    }

    char word[256];
    int count = -1;
    int type = -1;  // 0 for sdc (swap), 1 for sdd (main disk)

    while (fscanf(in, "%s", word) != EOF) {
        if (count >= 0 && count < 11) {
            if (type == 0) {
                io_counter_sda_new[count] = atoll(word);  // Using sda array for sdc data
            } else if (type == 1) {
                io_counter_sdc_new[count] = atoll(word);  // Using sdc array for sdd data
            }
            count++;
        }
        if (strcmp(word, "sdc") == 0) {  // Monitor swap disk (memory pressure)
            type = 0;
            count = 0;
        } else if (strcmp(word, "sdd") == 0) {  // Monitor main disk (file I/O)
            type = 1;
            count = 0;
        }
    }
    fclose(in);

    if (!io_counters_initialized) {
        printf("Disk I/O counters not initialized\n");
        return;
    }

    printf("Disk I/O (sdc swap):\n");
    printf("  Read operations:           %'12lld\n", io_counter_sda_new[0] - io_counter_sda[0]);
    printf("  Read operations merged:    %'12lld\n", io_counter_sda_new[1] - io_counter_sda[1]);
    printf("  Sectors read:              %'12lld (%'.1f MB)\n",
           io_counter_sda_new[2] - io_counter_sda[2],
           (io_counter_sda_new[2] - io_counter_sda[2]) * 512.0 / (1024.0 * 1024.0));
    printf("  Read time:                 %'12lld ms\n", io_counter_sda_new[3] - io_counter_sda[3]);
    printf("  Write operations:          %'12lld\n", io_counter_sda_new[4] - io_counter_sda[4]);
    printf("  Write operations merged:   %'12lld\n", io_counter_sda_new[5] - io_counter_sda[5]);
    printf("  Sectors written:           %'12lld (%'.1f MB)\n",
           io_counter_sda_new[6] - io_counter_sda[6],
           (io_counter_sda_new[6] - io_counter_sda[6]) * 512.0 / (1024.0 * 1024.0));
    printf("  Write time:                %'12lld ms\n", io_counter_sda_new[7] - io_counter_sda[7]);
    printf("  I/Os (sectors read/written):  %'9lld\n", (io_counter_sda_new[2] - io_counter_sda[2]) + (io_counter_sda_new[6] - io_counter_sda[6]));
}

void read_page_faults(long *minor_faults, long *major_faults) {
    int pid = getpid();
    char path[200];
    sprintf(path, "/proc/%d/stat", pid);

    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        *minor_faults = 0;
        *major_faults = 0;
        return;
    }

    // Skip first 9 fields, then read minor faults (field 10) and major faults (field 12)
    char dummy[256];
    for (int i = 0; i < 9; i++) {
        if (fscanf(fp, "%s", dummy) != 1) {
            *minor_faults = 0;
            *major_faults = 0;
            fclose(fp);
            return;
        }
    }

    // Read field 10 (minflt) and skip field 11 (cminflt), then read field 12 (majflt)
    if (fscanf(fp, "%ld %*s %ld", minor_faults, major_faults) != 2) {
        *minor_faults = 0;
        *major_faults = 0;
    }

    fclose(fp);
}

// Global variables for page fault tracking
long initial_minor_faults = 0;
long initial_major_faults = 0;
int page_faults_initialized = 0;

void init_page_faults() {
    read_page_faults(&initial_minor_faults, &initial_major_faults);
    page_faults_initialized = 1;
}

void print_mem_data() {
    char path[200];
    int pid = getpid();
    sprintf(path, "/proc/%d/statm", pid);

    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        printf("Memory data unavailable\n");
        return;
    }

    long size, resident, share, text, lib, data, dt;
    if (fscanf(fp, "%ld %ld %ld %ld %ld %ld %ld", &size, &resident, &share, &text, &lib, &data,
               &dt) != 7) {
        printf("Failed to parse memory data\n");
        fclose(fp);
        return;
    }
    fclose(fp);

    // Get page fault information
    long current_minor_faults, current_major_faults;
    read_page_faults(&current_minor_faults, &current_major_faults);

    long minor_diff = page_faults_initialized ? current_minor_faults - initial_minor_faults : 0;
    long major_diff = page_faults_initialized ? current_major_faults - initial_major_faults : 0;

    long page_size = sysconf(_SC_PAGESIZE);
    printf("Memory Usage:\n");
    printf("  Total (virtual):     %'12ld pages (%'.1f MB)\n", size,
           size * page_size / (1024.0 * 1024.0));
    printf("  Resident (RSS):      %'12ld pages (%'.1f MB)\n", resident,
           resident * page_size / (1024.0 * 1024.0));
    printf("  Shared:              %'12ld pages (%'.1f MB)\n", share,
           share * page_size / (1024.0 * 1024.0));
    printf("  Text (code):         %'12ld pages (%'.1f MB)\n", text,
           text * page_size / (1024.0 * 1024.0));
    printf("  Library:             %'12ld pages (%'.1f MB)\n", lib,
           lib * page_size / (1024.0 * 1024.0));
    printf("  Data + stack:        %'12ld pages (%'.1f MB)\n", data,
           data * page_size / (1024.0 * 1024.0));
    printf("  Dirty pages:         %'12ld pages (%'.1f MB)\n", dt,
           dt * page_size / (1024.0 * 1024.0));
    printf("  Minor page faults:   %'12ld (soft faults)\n", minor_diff);
    printf("  Major page faults:   %'12ld (hard faults)\n", major_diff);
}

void print_final_results(int lcs_length, double ut, double st, double tt, int r, char *str) {
    printf("\n");
    printf("FINAL RESULTS\n");
    printf("LCS Length: %d\n", lcs_length);

    printf("Overall execution time:\n");
    printf("  User time:               %.4f seconds (%s)\n", ut, conv_sec(ut, str));
    printf("  System time:             %.4f seconds (%s)\n", st, conv_sec(st, str));
    printf("  Total time:              %.4f seconds (%s)\n", tt, conv_sec(tt, str));

    printf("Average per run:\n");
    printf("  User time:               %.4f seconds (%s)\n", ut / r, conv_sec(ut / r, str));
    printf("  System time:             %.4f seconds (%s)\n", st / r, conv_sec(st / r, str));
    printf("  Total time:              %.4f seconds (%s)\n", tt / r, conv_sec(tt / r, str));
}
