/*
Hirschberg's algorithm.
Last Update: Oct 04, 2005 ( Rezaul Alam Chowdhury, UT Austin )

THIS FILE HAS BEEN INSTRUMENTED TO SEND SIGNALS TO THE CONTROLLER
*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>

// --- Includes for signaling ---
#include <csignal>   // For signal(), SIGPIPE
#include <fcntl.h>   // For open()
#include <unistd.h>  // For write(), close()
// ---

#include "../include/util.h"

#define DEFAULT_BASE 256

#define MAX_ALPHABET_SIZE 256

#define SYMBOL_TYPE char

#define max(a, b) ((a) > (b)) ? (a) : (b)
#define min(a, b) ((a) < (b)) ? (a) : (b)

#define BIDX(j, i) (((j) << LOG_BASE_N) + j + i)

int BASE_N;
int LOG_BASE_N;

SYMBOL_TYPE *X;
SYMBOL_TYPE *Y;
SYMBOL_TYPE *Z;

int nx, ny;

SYMBOL_TYPE *XR;
SYMBOL_TYPE *YR;

char **XS;
char **YS;

int *nxs;
int *nys;

int *L1;
int *L2;
int **K;

int zp;

int *clen;

struct rusage *ru;
int *zps;

char alpha[MAX_ALPHABET_SIZE + 1];

char *fname1;
char *fname2;

int ack_fd;

// Depth/subproblem-scaled memory profile parameters (set from argv).
long long g_base_bytes = 0;   // baseline (LOW) memory in bytes
long long g_cap_bytes = 0;    // maximum (HIGH) memory we will request, in bytes
int g_mode = 0;               // 0 = adversarial (raise during scan), 1 = benevolent (lower)
long long g_floor_bytes = 0;  // min memory.max that won't OOM, derived at runtime (see below)

/* The cgroup OOMs if memory.max drops below the process's NON-reclaimable resident
 * set: anonymous pages (stack + C++ runtime) plus the page tables. File-backed
 * buffers are always reclaimable, so they don't count. Read those two from
 * /proc/self/status (RssAnon + VmPTE) so the benevolent floor is derived, not magic,
 * and scales with the footprint (VmPTE grows with N). */
static long long read_nonreclaimable_bytes(void) {
    FILE *f = fopen("/proc/self/status", "r");
    if (f == NULL) return 0;
    long anon = 0, pte = 0, v;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "RssAnon: %ld kB", &v) == 1) anon = v;
        else if (sscanf(line, "VmPTE: %ld kB", &v) == 1) pte = v;
    }
    fclose(f);
    return (long long)(anon + pte) * 1024LL;
}

/*
 * Send an 8-byte memory target (in bytes) to the controller, then block until it
 * acknowledges so the memory.max change lands synchronously at the scan boundary.
 */
static int controller_gone = 0;

void send_value(int pipe_fd, long long value) {
    // If the controller has already disappeared, do not touch the pipe again.
    if (controller_gone) return;

    // 1. Send the memory target.
    if (write(pipe_fd, &value, sizeof(value)) != (ssize_t)sizeof(value)) {
        perror("LCS: Error writing to pipe!");
        controller_gone = 1;
        return;
    }

    // 2. Wait for the controller's ack (blocking) so memory.max is set before we proceed.
    char buf[1];
    ssize_t ack = read(ack_fd, buf, 1);
    if (ack <= 0) {
        // ack == 0 means EOF (controller closed the pipe / exited);
        // ack == -1 means a read error. Either way, the controller is gone.
        if (ack == -1) perror("LCS: Error reading ack from controller!");
        controller_gone = 1;
        return;
    }
}


void free_memory(int r) {
    int i;

    if (Z != NULL) ffree(Z);

    if (XR != NULL) ffree(XR);
    if (YR != NULL) ffree(YR);

    if (L1 != NULL) ffree(L1);
    if (L2 != NULL) ffree(L2);

    if (clen != NULL) ffree(clen);

    if (XS != NULL) {
        for (i = 0; i < r; i++)
            if (XS[i] != NULL) ffree(XS[i]);

        ffree(XS);
    }

    if (YS != NULL) {
        for (i = 0; i < r; i++)
            if (YS[i] != NULL) ffree(YS[i]);

        ffree(YS);
    }

    if (nxs != NULL) ffree(nxs);
    if (nys != NULL) ffree(nys);

    if (K != NULL) {
        for (i = 0; i < 2; i++)
            if (K[i] != NULL) ffree(K[i]);

        ffree(K);
    }

    if (ru != NULL) ffree(ru);

    if (zps != NULL) ffree(zps);
}

int allocate_memory(int m, int n, int r, int b) {
    int i, mm;

    mm = min(m, n);

    Z = (SYMBOL_TYPE *)fmalloc((mm + 2) * sizeof(SYMBOL_TYPE));

    XR = (SYMBOL_TYPE *)fmalloc((m + 2) * sizeof(SYMBOL_TYPE));
    YR = (SYMBOL_TYPE *)fmalloc((n + 2) * sizeof(SYMBOL_TYPE));

    L1 = (int *)fmalloc((n + 2) * sizeof(int));
    L2 = (int *)fmalloc((n + 2) * sizeof(int));

    clen = (int *)fmalloc((b + 1) * (b + 1) * sizeof(int));

    XS = (char **)fmalloc((r) * sizeof(char *));
    YS = (char **)fmalloc((r) * sizeof(char *));

    nxs = (int *)fmalloc((r) * sizeof(int));
    nys = (int *)fmalloc((r) * sizeof(int));

    K = (int **)fmalloc(2 * sizeof(int *));

    ru = (struct rusage *)fmalloc((r + 1) * sizeof(struct rusage));
    zps = (int *)fmalloc((r) * sizeof(int));

    if ((Z == NULL) || (XR == NULL) || (YR == NULL) || (L1 == NULL) || (L2 == NULL) ||
        (XS == NULL) || (YS == NULL) || (K == NULL) || (nxs == NULL) || (nys == NULL) ||
        (clen == NULL) || (ru == NULL) || (zps == NULL)) {
        printf("\nError: memory allocation failed!\n\n");
        free_memory(r);
        return 0;
    }

    for (i = 0; i < r; i++) {
        XS[i] = (char *)fmalloc((m + 2) * sizeof(char));
        YS[i] = (char *)fmalloc((n + 2) * sizeof(char));

        if ((XS[i] == NULL) || (YS[i] == NULL)) {
            printf("\nError: memory allocation failed!\n\n");
            free_memory(r);
            return 0;
        }
    }

    for (i = 0; i < 2; i++) {
        K[i] = (int *)fmalloc((n + 2) * sizeof(int));

        if (K[i] == NULL) {
            printf("\nError: memory allocation failed!\n\n");
            free_memory(r);
            return 0;
        }
    }

    return 1;
}

int read_data(int r) {
    int i, d;

    if (scanf("alphabet: %s\n\n", alpha) != 1) { /* ignore: keep behavior */ }

    for (i = 0; i < r; i++) {
        if (scanf("sequence pair %d:\n\n", &d) != 1) return 0;
        if (scanf("X = %s\n", XS[i] + 1) != 1) return 0;
        nxs[i] = strlen(XS[i] + 1);
        if (scanf("Y = %s\n\n", YS[i] + 1) != 1) return 0;
        nys[i] = strlen(YS[i] + 1);
    }

    return 1;
}

int read_data_sep(int r) {
    int i;
    FILE *fp;

    if ((fp = fopen(fname1, "r")) == NULL) return 0;
    if (fscanf(fp, "%d\n", &i) != 1) { /* ignore: keep behavior */ }
    for (i = 0; i < r; i++) {
        if (fscanf(fp, "%s\n", XS[i] + 1) != 1) return 0;
        nxs[i] = strlen(XS[i] + 1);
        printf("|X| = %d\n", nxs[i]);
    }
    fclose(fp);

    if ((fp = fopen(fname2, "r")) == NULL) return 0;
    if (fscanf(fp, "%d\n", &i) != 1) { /* ignore: keep behavior */ }
    for (i = 0; i < r; i++) {
        if (fscanf(fp, "%s\n", YS[i] + 1) != 1) return 0;
        nys[i] = strlen(YS[i] + 1);
        printf("|Y| = %d\n", nys[i]);
    }
    fclose(fp);

    return 1;
}

int get_m_n_sep(int *m, int *n) {
    FILE *fp;

    if ((fp = fopen(fname1, "r")) == NULL) return 0;
    if (fscanf(fp, "%d", m) != 1) return 0;
    fclose(fp);

    if ((fp = fopen(fname2, "r")) == NULL) return 0;
    if (fscanf(fp, "%d", n) != 1) return 0;
    fclose(fp);

    return 1;
}

void copy_seq(int j) {
    nx = nxs[j];
    ny = nys[j];

    X = XS[j];
    Y = YS[j];
}

// INSTRUMENTED: Added pipe_fd and signal calls
void ALG_B(int m, int n, SYMBOL_TYPE *XX, SYMBOL_TYPE *YY, int *LL, int pipe_fd) {

    // --- 1. SCAN START: set memory coupled to this m*n subproblem ---
    // Adversarial (paper Sec.3): give "enough memory to store each recursive
    // sub-problem during its linear scan" = base + (subproblem size), capped.
    // Benevolent: reduce to the linear working set (m+n) -- mirror of adversarial.
    long long scan_mem;
    if (g_mode == 1) {
        // benevolent: reduce to the linear working set O(m+n) -- mirror of the
        // adversarial O(m*n) area; only ever a decrease, with a sane floor.
        scan_mem = (long long)(m + n) * (long long)sizeof(int);
        if (scan_mem > g_base_bytes) scan_mem = g_base_bytes;
        if (g_floor_bytes == 0) g_floor_bytes = 2LL * read_nonreclaimable_bytes();
        if (scan_mem < g_floor_bytes) scan_mem = g_floor_bytes;  // >= 2x(RssAnon+VmPTE)
    } else {
        long long sub = g_base_bytes + (long long)m * (long long)n * (long long)sizeof(int);
        scan_mem = (sub < g_cap_bytes) ? sub : g_cap_bytes;
    }
    send_value(pipe_fd, scan_mem);

    int i, j;

    for (j = 0; j <= n; j++) {
        K[1][j] = 0;
    }

    for (i = 1; i <= m; i++) {
        for (j = 0; j <= n; j++) {
            K[0][j] = K[1][j];
        }
        for (j = 1; j <= n; j++) {
            if (XX[i - 1] == YY[j - 1]) {
                K[1][j] = K[0][j - 1] + 1;
            } else {
                K[1][j] = max(K[1][j - 1], K[0][j]);
            }
        }
    }

    for (j = 0; j <= n; j++) {
        LL[j] = K[1][j];
    }

    // --- 2. SCAN END: return to baseline memory ---
    send_value(pipe_fd, g_base_bytes);
}

// INSTRUMENTED: Added pipe_fd
void ALG_C(int m, int n, SYMBOL_TYPE *XX, SYMBOL_TYPE *YY, SYMBOL_TYPE *XXR, SYMBOL_TYPE *YYR, int pipe_fd) {
    int i, j, k, M;
    SYMBOL_TYPE s;

    if (n == 0) return;
    else if ((n <= BASE_N) && (m <= BASE_N)) {
        for (i = 0; i <= m; i++) {
            clen[BIDX(0, i)] = 0;
        }
        for (j = 0; j <= n; j++) {
            clen[BIDX(j, 0)] = 0;
        }

        for (j = 1; j <= n; j++) {
            for (i = 1, k = BIDX(j, 1); i <= m; i++, k++) {
                if (XX[i - 1] == YY[j - 1]) {
                    clen[k] = clen[k - BASE_N - 2] + 1;
                } else {
                    clen[k] = max(clen[k - BASE_N - 1], clen[k - 1]);
                }
            }
        }

        i = m;
        j = n;
        k = zp;

        while ((i > 0) && (j > 0)) {
            if (XX[i - 1] == YY[j - 1]) {
                Z[++zp] = XX[i - 1];
                i--;
                j--;
            } else if (clen[BIDX(j - 1, i)] > clen[BIDX(j, i - 1)]) {
                j--;
            } else {
                i--;
            }
        }

        for (i = k + 1, j = zp; i < j; i++, j--) {
            s = Z[i];
            Z[i] = Z[j];
            Z[j] = s;
        }
    }
    else if (m == 1) {
        for (j = 1; j <= n; j++) {
            if (XX[0] == YY[j - 1]) break;
        }
        if (j <= n) Z[++zp] = XX[0];
    } else {
        i = m >> 1;

        // Pass pipe_fd to ALG_B
        ALG_B(i, n, XX, YY, L1, pipe_fd);
        ALG_B(m - i, n, XXR, YYR, L2, pipe_fd);

        M = 0;
        k = -1;
        for (j = 0; j <= n; j++) {
            if (L1[j] + L2[n - j] > M) {
                k = j;
                M = L1[j] + L2[n - j];
            }
        }

        // Pass pipe_fd to recursive calls
        ALG_C(i, k, XX, YY, XXR + m - i, YYR + n - k, pipe_fd);
        ALG_C(m - i, n - k, XX + i, YY + k, XXR, YYR, pipe_fd);
    }
}

// INSTRUMENTED: Added pipe_fd
int lcs_hirschberg(int pipe_fd) {
    int i;

    for (i = 1; i <= nx; i++) {
        XR[i] = X[nx - i + 1];
    }
    XR[nx + 1] = 0;

    for (i = 1; i <= ny; i++) {
        YR[i] = Y[ny - i + 1];
    }
    YR[ny + 1] = 0;

    zp = 0;
    // Pass pipe_fd to ALG_C
    ALG_C(nx, ny, X + 1, Y + 1, XR + 1, YR + 1, pipe_fd);

    Z[zp + 1] = 0;

    return zp;
}

int main(int argc, char *argv[]) {
    int i, l, m, n, r, b;
    double ut, st, tt;
    char str[50];

    // Ignore SIGPIPE so a write() to a closed pipe returns an error instead of
    // killing the process if the controller has already exited.
    signal(SIGPIPE, SIG_IGN);

    printf(
        "=====================================================================================\n");
    printf("Program: %s (Signal-Instrumented)\n", argv[0]);

    // INSTRUMENTED: Changed arg check from 3 to 5
    if (argc < 5) {
        printf("\nError: not enough arguments!\n");
        printf("Specify: n ( = length of sequence ), r ( = number of runs ), base ( = base case ), and pipe ( = /path/to/pipe )\n\n");
        return 0;
    }

    n = atoi(argv[1]);
    if (n == -1) {
        fname1 = argv[2];
        fname2 = argv[3];
        b = 2;
    } else
        b = 0;

    r = atoi(argv[b + 2]);
    m = n;

    if (n == 0) {
        if (scanf("%d %d\n\n", &m, &n) != 2) {
            printf("\nError: cannot read sequence lengths!\n");
            return 0;
        }
    } else if (n == -1) {
        if (!get_m_n_sep(&m, &n)) {
            printf("\nError: cannot read sequence lengths!\n");
            return 0;
        }
    }

    if (argc > b + 3) {
        BASE_N = atoi(argv[b + 3]);
        if (BASE_N <= 0) BASE_N = DEFAULT_BASE;
    } else
        BASE_N = DEFAULT_BASE;

    if ((BASE_N & (BASE_N - 1)) != 0) {
        fprintf(stderr, "Error: BASE_CASE (%d) must be a power of two.\n", BASE_N);
        exit(1);
    }

    l = BASE_N;
    LOG_BASE_N = 0;
    while (l > 1) {
        l >>= 1;
        LOG_BASE_N++;
    }

    // INSTRUMENTED: Get pipe filename from argv[4]
    char* pipe_filename = argv[b + 4];

    // INSTRUMENTED: depth/subproblem-scaled profile parameters.
    //   argv[b+5] = base (LOW) MiB, argv[b+6] = cap (HIGH) MiB, argv[b+7] = mode (0 adv, 1 ben)
    long long base_mib = (argc > b + 5) ? atoll(argv[b + 5]) : 2;
    long long cap_mib  = (argc > b + 6) ? atoll(argv[b + 6]) : 64;
    g_mode = (argc > b + 7) ? atoi(argv[b + 7]) : 0;
    g_base_bytes = base_mib * 1048576LL;
    g_cap_bytes = cap_mib * 1048576LL;

    if (!allocate_memory(m, n, r, BASE_N)) return 0;

    if (b == 0) {
        if (!read_data(r)) {
            printf("\nError: failed to read data!\n\n");
            free_memory(r);
            return 0;
        }
    } else {
        if (!read_data_sep(r)) {
            printf("\nError: failed to read data!\n\n");
            free_memory(r);
            return 0;
        }
    }

    printf("m = %d, n = %d\n", m, n);
    printf("Runs = %d, base case = %d\n", r, BASE_N);
    // printf("Pipe = %s\n", pipe_filename); // Print pipe name

    // --- INSTRUMENTED: Open the Pipe for Writing ---
    // This connects to the controller, which is waiting (blocked)
    // on the other end of this pipe.
    int pipe_fd = open(pipe_filename, O_WRONLY);
    if (pipe_fd < 0) {
        perror("LCS: Failed to open pipe for writing. Is controller running?");
        return 1;
    }
    // printf("LCS: Connected to controller pipe.\n");

    // Open ACK pipe for reading
    char ack_pipe_filename[256];
    sprintf(ack_pipe_filename, "%s_ack", pipe_filename);
    ack_fd = open(ack_pipe_filename, O_RDONLY);
    if (ack_fd < 0) {
        perror("LCS: Failed to open ack pipe for reading");
        return 1;
    }
    // printf("LCS: Connected to ack pipe.\n");


    getrusage(RUSAGE_SELF, &ru[0]);

    double total_wall_time = 0.0;

    for (i = 0; i < r; i++) {
        init_disk_io();  // Initialize disk I/O counters
        init_page_faults();  // Initialize page fault counters
        double start = get_wall_time();
        copy_seq(i);
        
        // INSTRUMENTED: Pass pipe_fd to algorithm
        l = lcs_hirschberg(pipe_fd);
        
        zps[i] = l;
        double end = get_wall_time();
        getrusage(RUSAGE_SELF, &ru[i + 1]);

        printf("\n");
        printf("RUN %d RESULTS\n", i + 1);
        printf("Time:\n");
        printf("  Wall time:                %.4f seconds (%s)\n", end - start, conv_sec(end - start, str));

        total_wall_time += (end - start);

        double run_ut = ru[i + 1].ru_utime.tv_sec + (ru[i + 1].ru_utime.tv_usec * 0.000001) -
                        (ru[i].ru_utime.tv_sec + (ru[i].ru_utime.tv_usec * 0.000001));
        double run_st = ru[i + 1].ru_stime.tv_sec + (ru[i + 1].ru_stime.tv_usec * 0.000001) -
                        (ru[i].ru_stime.tv_sec + (ru[i].ru_stime.tv_usec * 0.000001));
        double run_tt = run_ut + run_st;

        printf("  User time:                %.4f seconds (%s)\n", run_ut, conv_sec(run_ut, str));
        printf("  System time:              %.4f seconds (%s)\n", run_st, conv_sec(run_st, str));
        printf("  Total time:               %.4f seconds (%s)\n", run_tt, conv_sec(run_tt, str));

        print_proc_io();
        print_disk_io();  // Show disk I/O activity difference
        print_mem_data();
    }

    ut = ru[r].ru_utime.tv_sec + (ru[r].ru_utime.tv_usec * 0.000001) -
         (ru[0].ru_utime.tv_sec + (ru[0].ru_utime.tv_usec * 0.000001));
    st = ru[r].ru_stime.tv_sec + (ru[r].ru_stime.tv_usec * 0.000001) -
         (ru[0].ru_stime.tv_sec + (ru[0].ru_stime.tv_usec * 0.000001));
    tt = ut + st;

    print_final_results(zps[r - 1], ut, st, tt, total_wall_time, r, str);

    // --- INSTRUMENTED: Close the Pipe ---
    // This tells the controller's 'read()' loop to exit,
    // allowing the controller to clean up and quit.
    // printf("LCS: Algorithm complete. Closing pipe.\n");
    close(pipe_fd);
    close(ack_fd);

    free_memory(r);

    return 0;
}
