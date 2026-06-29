#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <string>
#include <vector>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>

static std::string memmax_path;
static FILE *trace_fp = NULL;
static double t0 = 0.0;

static double now_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static void set_memory_max(long long bytes) {
    FILE *f = fopen(memmax_path.c_str(), "w");
    if (f != NULL) {
        fprintf(f, "%lld\n", bytes);
        fclose(f);
    } else {
        perror("Controller: cannot open memory.max");
    }
    if (trace_fp != NULL) {
        fprintf(trace_fp, "%.1f %lld\n", now_ms() - t0, bytes);
        fflush(trace_fp);
    }
}

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);

    if (argc < 4) {
        fprintf(stderr,
                "Usage: %s <cgroup_path> <profile_file_MiB> <pipe_file> [trace_out]\n",
                argv[0]);
        return 1;
    }

    std::string cgroup_path = argv[1];
    std::string profile_file = argv[2];
    std::string pipe_file = argv[3];
    std::string ack_file = pipe_file + "_ack";
    memmax_path = cgroup_path + "/memory.max";

    if (argc >= 5) {
        trace_fp = fopen(argv[4], "w");
        if (trace_fp == NULL) perror("Controller: cannot open trace file");
    }

    std::vector<long long> profile_bytes;
    {
        std::ifstream in(profile_file.c_str());
        if (!in.is_open()) {
            fprintf(stderr, "Controller: cannot open profile %s\n", profile_file.c_str());
            return 1;
        }
        double mib;
        while (in >> mib) {
            profile_bytes.push_back((long long)(mib * 1048576.0));
        }
    }
    if (profile_bytes.empty()) {
        fprintf(stderr, "Controller: profile is empty\n");
        return 1;
    }

    t0 = now_ms();
    set_memory_max(profile_bytes[0]);  // initial baseline memory (profile line 1)
    fflush(stdout);

    int fifo_fd = open(pipe_file.c_str(), O_RDONLY);
    int ack_fd = open(ack_file.c_str(), O_WRONLY);
    if (fifo_fd < 0 || ack_fd < 0) {
        perror("Controller: cannot open signal pipe");
        return 1;
    }

    // Each message from the LCS is an 8-byte memory target (in bytes) computed from
    // the current subproblem. Apply it to memory.max directly, record it, and ack.
    long long value;
    ssize_t got;
    while ((got = read(fifo_fd, &value, sizeof(value))) > 0) {
        while (got < (ssize_t)sizeof(value)) {
            ssize_t more = read(fifo_fd, ((char *)&value) + got, sizeof(value) - got);
            if (more <= 0) { got = -1; break; }
            got += more;
        }
        if (got != (ssize_t)sizeof(value)) break;
        set_memory_max(value);
        if (write(ack_fd, "K", 1) < 0) {
            perror("Controller: ack write failed (LCS gone?)");
            break;
        }
    }

    close(fifo_fd);
    close(ack_fd);
    if (trace_fp != NULL) fclose(trace_fp);
    return 0;
}
