/*
 * Signal-Based Balloon
 *
 * This balloon reads a simple profile file (just a list of memory values)
 * and changes its size every time it receives a signal on a FIFO pipe.
 *
 * --- COMPILE ---
 * g++ -o bin/balloon src/balloon.cpp -lrt -pthread
 */

#include <iostream>
#include <cstdlib>
#include <thread>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string>
#include <fcntl.h>
#include <fstream>
#include <vector>
#include <unistd.h>
#include <errno.h> // For errno
#include <chrono>

// --- Globals ---
unsigned long long CGROUP_MEMORY = 0;
int NUM_BALLOONS = 0;
int BALLOON_ID = 0;
unsigned long long MEMORY = 0ULL; // Current balloon size in bytes
int* dst = (int*)MAP_FAILED;      // The mmap'd memory

std::vector<unsigned long long> memory_profile;
int profile_index = 0;

/*
 * Calculates the balloon's mmap size (MEMORY) based on the
 * total cgroup "room" and the algorithm's "target" memory.
 */
unsigned long long set_memory_in_bytes(unsigned long long cgroup_memory, unsigned long long target_memory, int num_balloons) {
    if (cgroup_memory == 0 || num_balloons == 0) {
        std::cout << "Balloon: Invalid value! CgroupMem or NumBalloons is zero.\n";
        exit(1);
    }
    
    if (cgroup_memory > target_memory) {
        // Cgroup is larger than target. Inflate the balloon.
        // (cgroup - target) is the total "free" memory.
        unsigned long long share = (cgroup_memory - target_memory) / num_balloons;
        
        // We ensure it's at least 4 bytes so mmap doesn't fail.
        if (share <= 0) return 4ULL;
        return share;
    } else {
        // Cgroup is smaller or equal to target.
        // Deflate the balloon to a minimal size.
        return 4ULL; 
    }
}

// Simplified profile reader
void read_memory_profile(std::string mem_profile_filename) {
    double value_in_mb;
    std::ifstream input_mem_profile(mem_profile_filename.c_str());
    if (!input_mem_profile.is_open()) {
        std::cout << "Balloon Error: Cannot open memory profile file " << mem_profile_filename << std::endl;
        exit(1);
    }
    
    // Just read the list of memory values (in MiB)
    while (input_mem_profile >> value_in_mb) {
        // Convert MiB to bytes
        memory_profile.push_back((unsigned long long)(value_in_mb * 1048576)); 
    }
    input_mem_profile.close();
}

// Function to change the balloon's memory
void change_memory(unsigned long long new_target_memory_bytes) {
    // 1. Unmap old memory, if it exists
    if (dst != (int*)MAP_FAILED && MEMORY > 0) {
        munmap(dst, MEMORY);
    }

    // 2. Calculate new balloon size
    MEMORY = set_memory_in_bytes(CGROUP_MEMORY, new_target_memory_bytes, NUM_BALLOONS);
    std::cout << "Balloon: Target Memory (bytes): " << new_target_memory_bytes << " -> New Balloon Size (bytes): " << MEMORY << std::endl;

    // 3. Map new memory (anonymous: consume RAM directly with no backing file, so
    //    the balloon does no disk writeback and never shows up in disk-I/O accounting)
    if (MEMORY <= 0) {
        dst = (int*)MAP_FAILED;
        return;
    }

    dst = (int*)mmap(0, MEMORY, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (dst == (int*)MAP_FAILED) {
        perror("Balloon mmap error (continuing without claiming memory)");
        return;
    }
    
    // 5. Touch the new memory to "claim" it from the OS
    for (unsigned long i = 0; i < MEMORY / sizeof(int); i += 1000) {
        dst[i] = 1; // Write to the memory page
    }
    if (MEMORY > 0) {
        dst[(MEMORY / sizeof(int)) - 1] = 1;
    }
    
    std::cout << "Balloon: Done changing memory." << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 6) {
        std::cout << "Insufficient arguments! Usage: <CgroupMem> <NumBalloons> <BalloonID> <ProfileFile> <PipeFile>\n";
        exit(1);
    }

    // --- 1. Parse Arguments ---
    CGROUP_MEMORY = 1048576ULL * atol(argv[1]);
    NUM_BALLOONS = atoi(argv[2]);
    BALLOON_ID = atoi(argv[3]);
    std::string mem_profile_filename = argv[4];
    std::string pipe_filename = argv[5];
    std::string ack_pipe_filename = pipe_filename + "_ack";

    // --- 2. Read Profile ---
    read_memory_profile(mem_profile_filename);
    if (memory_profile.empty()) return 1;

    // --- 3. Set INITIAL memory state ---
    profile_index = 0;
    change_memory(memory_profile[profile_index]);

    // --- 5. Open pipe and wait for signals ---
    std::cout << "Balloon: Live. Waiting for signal on pipe: " << pipe_filename << std::endl;
    int fifo_fd = open(pipe_filename.c_str(), O_RDONLY);
    int ack_fd = open(ack_pipe_filename.c_str(), O_WRONLY);

    char buf[1]; 
    while (read(fifo_fd, buf, 1) > 0) {
        if (!memory_profile.empty()) {
            profile_index = (profile_index + 1) % memory_profile.size();
            std::cout << "Balloon: Moving to profile index " << profile_index << std::endl;
            
            change_memory(memory_profile[profile_index]);
            
            if (write(ack_fd, "K", 1) < 0) perror("Balloon Ack Write");
        }
    }

    // --- Cleanup ---
    std::cout << "Balloon: Pipe closed. Shutting down." << std::endl;
    if (dst != (int*)MAP_FAILED && MEMORY > 0) munmap(dst, MEMORY);
    close(fifo_fd);
    close(ack_fd);
    return 0;
}