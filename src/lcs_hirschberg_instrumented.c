/*
Hirschberg's algorithm.
Last Update: Oct 04, 2005 ( Rezaul Alam Chowdhury, UT Austin )
*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>

#include "../include/util.h"

#define DEFAULT_BASE 32

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

void free_memory(int r) {
    int i;

    if (Z != NULL) free(Z);

    if (XR != NULL) free(XR);
    if (YR != NULL) free(YR);

    if (L1 != NULL) free(L1);
    if (L2 != NULL) free(L2);

    if (clen != NULL) free(clen);

    if (XS != NULL) {
        for (i = 0; i < r; i++)
            if (XS[i] != NULL) free(XS[i]);

        free(XS);
    }

    if (YS != NULL) {
        for (i = 0; i < r; i++)
            if (YS[i] != NULL) free(YS[i]);

        free(YS);
    }

    if (nxs == NULL) free(nxs);
    if (nys == NULL) free(nys);

    if (K != NULL) {
        for (i = 0; i < 2; i++)
            if (K[i] != NULL) free(K[i]);

        free(K);
    }

    if (ru != NULL) free(ru);

    if (zps != NULL) free(zps);
}

int allocate_memory(int m, int n, int r, int b) {
    int i, d, mm;

    mm = min(m, n);

    Z = (SYMBOL_TYPE *)malloc((mm + 2) * sizeof(SYMBOL_TYPE));

    XR = (SYMBOL_TYPE *)malloc((m + 2) * sizeof(SYMBOL_TYPE));
    YR = (SYMBOL_TYPE *)malloc((n + 2) * sizeof(SYMBOL_TYPE));

    L1 = (int *)malloc((n + 2) * sizeof(int));
    L2 = (int *)malloc((n + 2) * sizeof(int));

    clen = (int *)malloc((b + 1) * (b + 1) * sizeof(int));

    XS = (char **)malloc((r) * sizeof(char *));
    YS = (char **)malloc((r) * sizeof(char *));

    nxs = (int *)malloc((r) * sizeof(int));
    nys = (int *)malloc((r) * sizeof(int));

    K = (int **)malloc(2 * sizeof(int *));

    ru = (struct rusage *)malloc((r + 1) * sizeof(struct rusage));
    zps = (int *)malloc((r) * sizeof(int));

    if ((Z == NULL) || (XR == NULL) || (YR == NULL) || (L1 == NULL) || (L2 == NULL) ||
        (XS == NULL) || (YS == NULL) || (K == NULL) || (nxs == NULL) || (nys == NULL) ||
        (clen == NULL) || (ru == NULL) || (zps == NULL)) {
        printf("\nError: memory allocation failed!\n\n");
        free_memory(r);
        return 0;
    }

    for (i = 0; i < r; i++) {
        XS[i] = (char *)malloc((m + 2) * sizeof(char));
        YS[i] = (char *)malloc((n + 2) * sizeof(char));

        if ((XS[i] == NULL) || (YS[i] == NULL)) {
            printf("\nError: memory allocation failed!\n\n");
            free_memory(r);
            return 0;
        }
    }

    for (i = 0; i < 2; i++) {
        K[i] = (int *)malloc((n + 2) * sizeof(int));

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

    scanf("alphabet: %s\n\n", alpha);

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
    fscanf(fp, "%d\n", &i);
    for (i = 0; i < r; i++) {
        if (fscanf(fp, "%s\n", XS[i] + 1) != 1) return 0;
        nxs[i] = strlen(XS[i] + 1);
        printf("|X| = %d\n", nxs[i]);
    }
    fclose(fp);

    if ((fp = fopen(fname2, "r")) == NULL) return 0;
    fscanf(fp, "%d\n", &i);
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
    int i;

    nx = nxs[j];
    ny = nys[j];

    X = XS[j];
    Y = YS[j];
}

void ALG_B(int m, int n, SYMBOL_TYPE *XX, SYMBOL_TYPE *YY, int *LL) {

    // ADDED THIS LINE: Print a "START" timestamp to stderr.
    fprintf(stderr, "%.6f,START_SCAN\n", get_time_sec());

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

    // ADDED THIS LINE: Print an "END" timestamp to stderr.
    fprintf(stderr, "%.6f,END_SCAN\n", get_time_sec());
}

void ALG_C(int m, int n, SYMBOL_TYPE *XX, SYMBOL_TYPE *YY, SYMBOL_TYPE *XXR, SYMBOL_TYPE *YYR) {
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

        ALG_B(i, n, XX, YY, L1);
        ALG_B(m - i, n, XXR, YYR, L2);

        M = 0;
        k = -1;
        for (j = 0; j <= n; j++) {
            if (L1[j] + L2[n - j] > M) {
                k = j;
                M = L1[j] + L2[n - j];
            }
        }

        ALG_C(i, k, XX, YY, XXR + m - i, YYR + n - k);
        ALG_C(m - i, n - k, XX + i, YY + k, XXR, YYR);
    }
}

int lcs_hirschberg(void) {
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
    ALG_C(nx, ny, X + 1, Y + 1, XR + 1, YR + 1);

    Z[zp + 1] = 0;

    return zp;
}

int main(int argc, char *argv[]) {
    int i, l, m, n, r, b, prn;
    double ut, st, tt;
    char str[50];

    printf(
        "=====================================================================================\n");
    printf("Program: %s\n", argv[0]);

    if (argc < 3) {
        printf("\nError: not enough arguments!\n");
        printf("Specify: n ( = length of sequence ) and r ( = number of runs ).\n\n");
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

    l = BASE_N;
    LOG_BASE_N = 0;
    while (l > 1) {
        l >>= 1;
        LOG_BASE_N++;
    }

    if (argc > b + 4)
        prn = atoi(argv[b + 4]);
    else
        prn = 0;

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

    getrusage(RUSAGE_SELF, &ru[0]);

    // ADDED THIS LINE to initialize the global start time.
    get_time_sec();

    for (i = 0; i < r; i++) {
        init_disk_io();  // Initialize disk I/O counters
        init_page_faults();  // Initialize page fault counters
        double start = get_wall_time();
        copy_seq(i);
        l = lcs_hirschberg();
        zps[i] = l;
        double end = get_wall_time();
        getrusage(RUSAGE_SELF, &ru[i + 1]);

        printf("\n");
        printf("RUN %d RESULTS\n", i + 1);
        printf("Time:\n");
        printf("  Wall time:               %.4f seconds (%s)\n", end - start, conv_sec(end - start, str));

        double run_ut = ru[i + 1].ru_utime.tv_sec + (ru[i + 1].ru_utime.tv_usec * 0.000001) -
                        (ru[i].ru_utime.tv_sec + (ru[i].ru_utime.tv_usec * 0.000001));
        double run_st = ru[i + 1].ru_stime.tv_sec + (ru[i + 1].ru_stime.tv_usec * 0.000001) -
                        (ru[i].ru_stime.tv_sec + (ru[i].ru_stime.tv_usec * 0.000001));
        double run_tt = run_ut + run_st;

        printf("  User time:               %.4f seconds (%s)\n", run_ut, conv_sec(run_ut, str));
        printf("  System time:             %.4f seconds (%s)\n", run_st, conv_sec(run_st, str));
        printf("  Total time:              %.4f seconds (%s)\n", run_tt, conv_sec(run_tt, str));

        print_proc_io();
        print_disk_io();  // Show disk I/O activity difference
        print_mem_data();
    }

    ut = ru[r].ru_utime.tv_sec + (ru[r].ru_utime.tv_usec * 0.000001) -
         (ru[0].ru_utime.tv_sec + (ru[0].ru_utime.tv_usec * 0.000001));
    st = ru[r].ru_stime.tv_sec + (ru[r].ru_stime.tv_usec * 0.000001) -
         (ru[0].ru_stime.tv_sec + (ru[0].ru_stime.tv_usec * 0.000001));
    tt = ut + st;

    print_final_results(zps[r - 1], ut, st, tt, r, str);

    free_memory(r);

    return 0;
}