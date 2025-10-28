/*
Iterative LCS.
Last Update: June 28, 2005 ( Rezaul Alam Chowdhury, UT Austin )
*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>

#include "../include/util.h"

#define MAX_ALPHABET_SIZE 256

#define SYMBOL_TYPE char

#define max(a, b) ((a) > (b)) ? (a) : (b)
#define min(a, b) ((a) < (b)) ? (a) : (b)

SYMBOL_TYPE *X;
SYMBOL_TYPE *Y;
SYMBOL_TYPE *Z;

int nx, ny;

int xp, yp, zp;

char **XS;
char **YS;

int *nxs;
int *nys;

int **len;

struct rusage *ru;
int *zps;

char alpha[MAX_ALPHABET_SIZE + 1];

void free_memory(int r, int n) {
    int i;

    if (Z != NULL) free(Z);

    if (len != NULL) {
        for (i = 0; i <= n; i++)
            if (len[i] != NULL) free(len[i]);

        free(len);
    }

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

    if (nxs != NULL) free(nxs);
    if (nys != NULL) free(nys);

    if (ru != NULL) free(ru);

    if (zps != NULL) free(zps);
}

int allocate_memory(int m, int n, int r) {
    int i, d, mm;

    mm = min(m, n);

    Z = (SYMBOL_TYPE *)malloc((mm + 2) * sizeof(SYMBOL_TYPE));

    XS = (char **)malloc((r) * sizeof(char *));
    YS = (char **)malloc((r) * sizeof(char *));

    nxs = (int *)malloc((r) * sizeof(int));
    nys = (int *)malloc((r) * sizeof(int));

    len = (int **)malloc((n + 1) * sizeof(int *));

    ru = (struct rusage *)malloc((r + 1) * sizeof(struct rusage));
    zps = (int *)malloc((r) * sizeof(int));

    if ((Z == NULL) || (XS == NULL) || (YS == NULL) || (nxs == NULL) || (nys == NULL) ||
        (ru == NULL) || (zps == NULL)) {
        printf("\nError: memory allocation failed!\n\n");
        free_memory(r, n);
        return 0;
    }

    for (i = 0; i < r; i++) {
        XS[i] = (char *)malloc((m + 2) * sizeof(char));
        YS[i] = (char *)malloc((n + 2) * sizeof(char));

        if ((XS[i] == NULL) || (YS[i] == NULL)) {
            printf("\nError: memory allocation failed!\n\n");
            free_memory(r, n);
            return 0;
        }
    }

    for (i = 0; i <= n; i++) {
        len[i] = (int *)malloc((m + 1) * sizeof(int));

        if (len[i] == NULL) {
            printf("\nError: memory allocation failed!\n\n");
            free_memory(r, n);
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

int lcs_classic(int r) {
    int i, j, m, n;

    m = nxs[r];
    n = nys[r];

    X = XS[r];
    Y = YS[r];

    for (i = 0; i <= m; i++) len[0][i] = 0;

    for (j = 0; j <= n; j++) len[j][0] = 0;

    for (j = 1; j <= n; j++)
        for (i = 1; i <= m; i++) {
            if (X[i] == Y[j]) {
                len[j][i] = len[j - 1][i - 1] + 1;
            } else {
                if (len[j - 1][i] > len[j][i - 1]) {
                    len[j][i] = len[j - 1][i];
                } else {
                    len[j][i] = len[j][i - 1];
                }
            }
        }

    return len[n][m];
}

int main(int argc, char *argv[]) {
    int i, l, m, n, nn, r;
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
    r = atoi(argv[2]);
    m = n;

    if (n <= 0) {
        printf("%d\n", n);
        if (scanf("%d %d\n\n", &m, &n) != 2) {
            printf("%d %d\n", m, n);
            printf("\nError: cannot read sequence lengths!\n");
            return 0;
        }
    }

    if (m < n) {
        printf("\nError: m < n!\n");
        return 0;
    }

    if (!allocate_memory(m, n, r)) return 0;

    if (!read_data(r)) {
        printf("\nError: failed to read data!\n\n");
        free_memory(r, n);
        return 0;
    }

    printf("m = %d, n = %d\n", m, n);
    printf("Runs = %d\n", r);

    getrusage(RUSAGE_SELF, &ru[0]);

    for (i = 0; i < r; i++) {
        init_disk_io();  // Initialize disk I/O counters
        init_page_faults();  // Initialize page fault counters
        double start = get_wall_time();
        zps[i] = lcs_classic(i);
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

    free_memory(r, n);

    return 0;
}
