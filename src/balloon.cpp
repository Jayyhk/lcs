/*
 * Signal-Based Balloon
 *
 * This balloon reads a simple profile file (just a list of memory values)
 * and changes its size every time it receives a signal on a FIFO pipe.
 *
 * --- COMPILE ---
 * g++ -o bin/balloon src/balloon.cpp -lrt -pthread
 *
 * --- SCRIPT ARGUMENTS ---
 * Your script should call this with 5 arguments:
 * argv[1]: CgroupMem (MB) (e.g., 2048)
 * argv[2]: NumBalloons (e.g., 1)
 * argv[3]: BalloonID (e.g., 1)
 * argv[4]: ProfileFile (e.g., "res/adversarial/adversarial_profile.txt")
 * argv[5]: PipeFile (e.g., "/tmp/balloon_signal")
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

// --- Globals ---
unsigned long long CGROUP_MEMORY = 0;
int NUM_BALLOONS = 0;
int BALLOON_ID = 0;
unsigned long long MEMORY = 0ULL; // Current balloon size in bytes
int* dst = (int*)MAP_FAILED;      // The mmap'd memory

// No 'times' vector, just a simple list of target memory values in bytes
std::vector<unsigned long long> memory_profile = std::vector<unsigned long long>();
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
    
    // std::cout << "Balloon: Cgroup: " << cgroup_memory << ", Target: " << target_memory << std::endl;
    
    if (cgroup_memory > target_memory) {
        // Cgroup is larger than target. Inflate the balloon.
        // (cgroup - target) is the total "free" memory.
        // Divide by num_balloons to get this balloon's share.
        // Magic number 256*1024 is ~256KB buffer for OS overhead.
        unsigned long long result = (cgroup_memory - target_memory) / num_balloons - 256 * 1024;
        
        // Ensure we don't return a negative (underflow) or tiny value
        if (result > cgroup_memory) return 0ULL; // Handle underflow
        return result;
    } else {
        // Cgroup is smaller or equal to target.
        // We want to give the algorithm all the memory.
        // Deflate the balloon to a minimal size (magic number 50 bytes).
        return 50ULL; 
    }
}

/*
 * Reads the simple profile file (e.g., "2048 \n 4 \n 2048")
 * and fills the memory_profile vector.
 */
// Simplified profile reader
void read_memory_profile(std::string mem_profile_filename) {
    double value_in_mb;
    std::ifstream input_mem_profile = std::ifstream(mem_profile_filename.c_str());
    if (!input_mem_profile.is_open()) {
        std::cout << "Balloon Error: Cannot open memory profile file " << mem_profile_filename << std::endl;
        exit(1);
    }
    
    // Just read the list of memory values (in MiB)
    while (input_mem_profile >> value_in_mb) {
        std::cout << "Balloon: Profile value read: " << value_in_mb << " MiB" << std::endl;
        // Convert MiB to bytes
        memory_profile.push_back((unsigned long long)(value_in_mb * 1048576)); 
    }
    
    input_mem_profile.close();
    std::cout << "Balloon: Read " << memory_profile.size() << " memory values from profile." << std::endl;
}

/*
 * Unmaps the old balloon, calculates the new size,
 * truncates the file, and maps the new balloon.
 */
// Function to change the balloon's memory
void change_memory(int fdout, unsigned long long new_target_memory_bytes) {
    // 1. Unmap old memory, if it exists
    if (dst != (int*)MAP_FAILED && MEMORY > 0) {
        munmap(dst, MEMORY);
    }

    // 2. Calculate new balloon size
    MEMORY = set_memory_in_bytes(CGROUP_MEMORY, new_target_memory_bytes, NUM_BALLOONS);
    std::cout << "Balloon: Target Memory (bytes): " << new_target_memory_bytes << " -> New Balloon Size (bytes): " << MEMORY << std::endl;

    // 3. Truncate file to the new balloon size
    if (ftruncate(fdout, MEMORY) != 0) {
        perror("Balloon ftruncate error");
        exit(1);
    }
    
    // 4. Map new memory
    // If MEMORY is 0 (or tiny), mmap will fail. We check for this.
    // Our set_memory_in_bytes() always returns at least 50, so MEMORY > 0.
    // But as a safety check:
    if (MEMORY <= 0) {
        dst = (int*)MAP_FAILED; // Set to failed so we don't try to unmap/touch it
        std::cout << "Balloon is fully deflated (size 0)." << std::endl;
        return;
    }

    dst = (int*)mmap(0, MEMORY, PROT_READ | PROT_WRITE, MAP_SHARED, fdout, 0);
    if (dst == (int*)MAP_FAILED) {
        perror("Balloon mmap error");
        exit(1);
    }
    
    // 5. Touch the new memory to "claim" it from the OS
    // This is the loop that actually "inflates" the balloon.
    for (unsigned long i = 0; i < MEMORY / sizeof(int); i += 1000) {
        dst[i] = 1; // Write to the memory page
    }
    
    // Also write to the last page to ensure full range
    if (MEMORY > 0) {
        dst[(MEMORY / sizeof(int)) - 1] = 1;
    }
    
    std::cout << "Balloon: Done changing memory." << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "Starting signal-based balloon program" << std::endl;
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

    if (CGROUP_MEMORY == 0 || NUM_BALLOONS == 0) {
        std::cout << "Error: CgroupMem and NumBalloons must be > 0" << std::endl;
        return 1;
    }

    // --- 2. Read Profile ---
    read_memory_profile(mem_profile_filename);
    if (memory_profile.empty()) {
        std::cout << "Error: Memory profile is empty. File: " << mem_profile_filename << std::endl;
        return 1;
    }

    // --- 3. Setup balloon mmap file ---
    // Use /tmp for the balloon data file, it's safer
    std::string filename = "/tmp/balloon_data" + std::to_string(BALLOON_ID);
    int fdout;
    if ((fdout = open(filename.c_str(), O_RDWR | O_CREAT, 0x0666)) < 0) {
        perror("Balloon can't create/open balloon data file");
        return 1;
    }

    // --- 4. Set INITIAL memory state ---
    // Set balloon to the *first* memory value in the profile (e.g., 2048 or 4)
    profile_index = 0;
    change_memory(fdout, memory_profile[profile_index]);

    // --- 5. Open pipe and wait for signals ---
    std::cout << "Balloon: Live. Waiting for signal on pipe: " << pipe_filename << std::endl;
    int fifo_fd = open(pipe_filename.c_str(), O_RDONLY);
    if (fifo_fd < 0) {
        perror("Balloon: Failed to open pipe for reading");
        return 1;
    }

    char buf[1]; // Buffer for the signal
    
    // This loop blocks on read() until a signal arrives or the pipe is closed
    // read() > 0 means we got a signal
    // read() == 0 means the other end (LCS) closed the pipe
    while (read(fifo_fd, buf, 1) > 0) {
        
        // --- A signal was received! ---
        profile_index++; // Move to the next memory value
        
        if (profile_index < memory_profile.size()) {
            // We have more values in our profile, apply the next one
            std::cout << "Balloon: Signal " << profile_index << " received! Moving to next memory state." << std::endl;
            change_memory(fdout, memory_profile[profile_index]);
        } else {
            // We are out of profile values.
            // Hold the last state.
            std::cout << "Balloon: Signal received, but end of profile reached. Holding last state." << std::endl;
            // We don't do anything, just loop and wait for pipe to close
        }
    }

    // --- Cleanup ---
    // The loop exits when read() == 0, meaning the LCS program closed its
    // end of the pipe (which it should do at exit).
    std::cout << "Balloon: Pipe closed. Shutting down." << std::endl;
    
    // Unmap the final memory block
    if (dst != (int*)MAP_FAILED && MEMORY > 0) {
        munmap(dst, MEMORY);
    }
    
    close(fifo_fd);
    close(fdout);
    unlink(filename.c_str()); // Delete the /tmp/balloon_data file
    
    std::cout << "Balloon done executing." << std::endl;
    return 0;
}
