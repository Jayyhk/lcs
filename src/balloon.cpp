/* This balloon program allows one to change the memory of a program. */
#include<iostream>
#include<chrono>
#include<cstdlib>
#include<thread>
#include<sys/types.h>
#include<sys/stat.h>
#include<sys/mman.h>
#include<string>
#include<fcntl.h>
#include<fstream>
#include<vector>
#include<unistd.h>

unsigned long long TARGET_MEMORY = 0;
unsigned long long CGROUP_MEMORY = 0;
int NUM_BALLOONS = 0;
int BALLOON_ID = 0;
int memory_profile_type = 0;
unsigned long long MEMORY = 0ULL;
std::chrono::system_clock::time_point t_start;
std::vector<double> times = std::vector<double>(); //this is an array containing all the timestamps in a memory profile
std::vector<unsigned long long> memory_values = std::vector<unsigned long long>(); //this is an array containing all the memory values in a memory profile

/*this program accepts as input the total cgroup memory, the number of balloons being run, and the target memory we wish
to use in our main program. Everything is in bytes.
Note that at a minimum, the main algorithm will use at least C/(B+1) memory.
*/

unsigned long long set_memory_in_bytes(unsigned long long cgroup_memory, unsigned long long target_memory, int num_balloons){
  if (TARGET_MEMORY == 0 || CGROUP_MEMORY == 0 || NUM_BALLOONS == 0){
    std::cout << "Invalid value! Error!\n";
    exit(1);
  }
  std::cout << cgroup_memory << " " << target_memory << std::endl;
  if (cgroup_memory > target_memory){
    unsigned long long result = (cgroup_memory - target_memory)/num_balloons - 256*1024; //this is a magic number, don't question it
    return result >= 0ULL ? result : 0ULL;
  }
  else {
    return 50ULL; //another magic number, do not question
  }
}


void read_memory_profile(std::string mem_profile_filename) {
  bool timestamp = true;
  double value;
  std::ifstream input_mem_profile = std::ifstream(mem_profile_filename.c_str());
  if (!input_mem_profile.is_open()) {
    std::cout << "Error: Cannot open memory profile file " << mem_profile_filename << std::endl;
    exit(1);
  }
  while (input_mem_profile >> value){
    std::cout << "Value: " << value << std::endl;
    if (timestamp)
      times.push_back(value);
    else
      memory_values.push_back((unsigned long long)(value * 1048576));
    timestamp = !timestamp;
  }
  input_mem_profile.close();
  for (unsigned int i = 0; i < times.size(); i++)
    std::cout << "Time: " << times[i] << " Memory: " << memory_values[i] << std::endl; 
}


int main(int argc, char *argv[]){
  std::cout << "Starting balloon program" << std::endl;
  if (argc < 7){
    std::cout << "Insufficient arguments! Usage: balloon.cpp <memory_profile_type>"
    "<cgroup_memory> <desired_memory> <num_balloons> <balloon_id> <memory_profile_file>\n";
    exit(1);
  }
  memory_profile_type = atoi(argv[1]);
  CGROUP_MEMORY = 1048576ULL * atol(argv[2]);
  TARGET_MEMORY = 1048576ULL * atol(argv[3]);
  NUM_BALLOONS = atoi(argv[4]);
  BALLOON_ID = atoi(argv[5]);
  std::string mem_profile_filename = argv[6];
  
  // Extract directory from profile filename
  std::string balloon_dir = ".";
  size_t last_slash = mem_profile_filename.find_last_of('/');
  if (last_slash != std::string::npos) {
    balloon_dir = mem_profile_filename.substr(0, last_slash);
  }
  
  // Create balloon directory if it doesn't exist
  if (mkdir(balloon_dir.c_str(), 0777) != 0 && errno != EEXIST) {
    std::cout << "Failed to create " << balloon_dir << " directory" << std::endl;
    return 1;
  }
  
  int fdout, ipcfd = -1;  // Initialize ipcfd to -1
  unsigned long long* dst2 = nullptr;  // Initialize dst2 to nullptr
  std::string filename = balloon_dir + "/balloon_data" + std::to_string(BALLOON_ID);
  std::cout << "Balloon data filename: " << filename << std::endl;
  if ((fdout = open(filename.c_str(), O_RDWR | O_CREAT, 0x0777 )) < 0){
    printf ("can't create/open balloon data files for writing\n");
    return 0;
  }
  
  // Only create IPCTEST for mode 3 (dynamic memory control)
  if (memory_profile_type == 3) {
    std::string ipctest_file = balloon_dir + "/IPCTEST";
    if ((ipcfd = open(ipctest_file.c_str(), O_RDWR | O_CREAT, 0x0777 )) < 0) {
      printf ("can't create/open file for writing\n");
      return 0;
    }
    if (ftruncate(ipcfd, sizeof(unsigned long long)*1) != 0) {
      printf ("ftruncate error for IPCTEST\n");
      return 0;
    }
    if (((dst2 = (unsigned long long*) mmap(0, sizeof(unsigned long long)*1, PROT_READ | PROT_WRITE, MAP_SHARED, ipcfd, 0)) == (unsigned long long*)MAP_FAILED)) {
      printf ("mmap error for output with code 1");
      return 0;
    }
    dst2[0] = TARGET_MEMORY;
  }
  
  // Worst-case memory profile is given
  if (memory_profile_type == 1)
    read_memory_profile(mem_profile_filename);

  MEMORY = set_memory_in_bytes(CGROUP_MEMORY, TARGET_MEMORY, NUM_BALLOONS);
  std::cout << "Each balloon is mmapping " << MEMORY << std::endl;
  std::cout << "cgroup memory " << CGROUP_MEMORY << ", target memory " << TARGET_MEMORY << std::endl;
  std::cout << "num balloons " <<   NUM_BALLOONS  << std::endl;

  // Set file size for mmap
  if (ftruncate(fdout, MEMORY) != 0) {
    printf ("ftruncate error\n");
    return 0;
  }
  
  int* dst;
  if (((dst = (int*) mmap(0, MEMORY, PROT_READ | PROT_WRITE, MAP_SHARED, fdout, 0)) == (int*)MAP_FAILED)){
    printf ("mmap error for output with code 2");
    return 0;
  }
  
  t_start = std::chrono::system_clock::now();
  std::chrono::system_clock::time_point current = std::chrono::system_clock::now();
  unsigned long index = 0;
  unsigned int counter = 0;
  while(true){
    if (MEMORY > 0) {
      index %= (MEMORY/4);
      //std::cout << index << "here" << MEMORY << std::endl;
      //dst[rand()%MEMORY] = 1;
      *(dst + rand()%(MEMORY/4)) = 1;
      *(dst + index) = 1;
      //dst[index] = 1;
      index += 1000;
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::chrono::system_clock::time_point t_end = std::chrono::system_clock::now();
    auto wall_time_ms = std::chrono::duration<double, std::milli>(t_end-t_start).count();
    double wall_time = wall_time_ms / 1000.0;
    if (memory_profile_type == 1){
      if (counter < times.size() && wall_time >= times[counter]){
        std::cout << "Time " << wall_time << "s reached. Changing memory." << std::endl;
        std::cout << "value stored in times array: " << times[counter] << std::endl;
        munmap(dst,MEMORY);
        MEMORY = set_memory_in_bytes(CGROUP_MEMORY, memory_values[counter], NUM_BALLOONS);
        std::cout << "Changing memory to (bytes): " << memory_values[counter] << " (Balloon size: " << MEMORY << ")" << std::endl;
        if (ftruncate(fdout, MEMORY) != 0) {
          printf ("ftruncate error\n");
          return 0;
        }
        if (((dst = (int*) mmap(0, MEMORY, PROT_READ | PROT_WRITE, MAP_SHARED , fdout, 0)) == (int*)MAP_FAILED)){
          printf ("mmap error for output with code");
          return 0;
        }
        std::cout << "Done changing memory " << MEMORY << std::endl;
        counter++;
      }
    }
    else if (memory_profile_type == 2){
      if (std::chrono::duration<double, std::milli>(std::chrono::system_clock::now()-current).count() > 15000){
        current = std::chrono::system_clock::now();
        munmap(dst,MEMORY);
        if (MEMORY == 50){
          MEMORY = set_memory_in_bytes(CGROUP_MEMORY, TARGET_MEMORY, NUM_BALLOONS);
        }else{
          MEMORY = 50;
        }
        std::cout << "Changing memory to " << MEMORY << std::endl;
        if (ftruncate(fdout, MEMORY) != 0) {
          printf ("ftruncate error\n");
          return 0;
        }
        if (((dst = (int*) mmap(0, MEMORY, PROT_READ | PROT_WRITE, MAP_SHARED , fdout, 0)) == (int*)MAP_FAILED)){
          printf ("mmap error for output with code");
          return 0;
        }
        std::cout << "Done changing memory " << MEMORY << std::endl;
      }
    }
    else if (memory_profile_type == 3){
      if (dst2 != nullptr) {
        std::cout << (int)*dst2 << std::endl;
        if ((int)dst2[0] != 0) { //} && MEMORY != set_memory_in_bytes(CGROUP_MEMORY, (int)dst2[0], NUM_BALLOONS)){
          munmap(dst,MEMORY);
          MEMORY = set_memory_in_bytes(CGROUP_MEMORY, (int)dst2[0], NUM_BALLOONS);
          std::cout << "Changing memory to " << MEMORY << std::endl;
          if (ftruncate(fdout, MEMORY) != 0) {
            printf ("ftruncate error\n");
            return 0;
          }
          if (((dst = (int*) mmap(0, MEMORY, PROT_READ | PROT_WRITE, MAP_SHARED , fdout, 0)) == (int*)MAP_FAILED)){
            printf ("mmap error for output with code");
            return 0;
          }
          std::cout << "Done changing memory " << MEMORY << std::endl;
        }
      }
    }
  }
  std::cout << "Balloon done executing." << std::endl;
}