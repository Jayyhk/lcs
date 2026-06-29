#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <unistd.h>
#include <sys/time.h>

static double now_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <cgroup_path> <trace_file>\n", argv[0]);
        return 1;
    }
    std::string memmax = std::string(argv[1]) + "/memory.max";

    std::vector<double> ts;
    std::vector<long long> mem;
    {
        std::ifstream in(argv[2]);
        double t;
        long long m;
        while (in >> t >> m) {
            ts.push_back(t);
            mem.push_back(m);
        }
    }
    if (ts.empty()) {
        fprintf(stderr, "mem_replay: empty trace\n");
        return 1;
    }

    double start = now_ms();
    for (size_t i = 0; i < ts.size(); i++) {
        double wait = ts[i] - (now_ms() - start);
        if (wait > 0) usleep((useconds_t)(wait * 1000.0));
        FILE *f = fopen(memmax.c_str(), "w");
        if (f != NULL) {
            fprintf(f, "%lld\n", mem[i]);
            fclose(f);
        }
    }
    return 0;
}
