#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <string>
#include <vector>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>

static std::string memmax_path;

static void set_memory_max(long long bytes) {
    FILE *f = fopen(memmax_path.c_str(), "w");
    if (f == NULL) {
        perror("Controller: cannot open memory.max");
        return;
    }
    fprintf(f, "%lld\n", bytes);
    fclose(f);
}

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);

    if (argc < 4) {
        fprintf(stderr,
                "Usage: %s <cgroup_path> <profile_file_MiB> <pipe_file>\n",
                argv[0]);
        return 1;
    }

    std::string cgroup_path = argv[1];
    std::string profile_file = argv[2];
    std::string pipe_file = argv[3];
    std::string ack_file = pipe_file + "_ack";
    memmax_path = cgroup_path + "/memory.max";

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

    size_t idx = 0;
    set_memory_max(profile_bytes[0]);
    printf("Controller: live. memory.max=%lld bytes (profile idx 0 of %zu)\n",
           profile_bytes[0], profile_bytes.size());
    fflush(stdout);

    int fifo_fd = open(pipe_file.c_str(), O_RDONLY);
    int ack_fd = open(ack_file.c_str(), O_WRONLY);
    if (fifo_fd < 0 || ack_fd < 0) {
        perror("Controller: cannot open signal pipe");
        return 1;
    }

    char buf[1];
    while (read(fifo_fd, buf, 1) > 0) {
        idx = (idx + 1) % profile_bytes.size();
        set_memory_max(profile_bytes[idx]);
        if (write(ack_fd, "K", 1) < 0) {
            perror("Controller: ack write failed (LCS gone?)");
            break;
        }
    }

    close(fifo_fd);
    close(ack_fd);
    printf("Controller: pipe closed, shutting down.\n");
    return 0;
}
