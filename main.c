#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mach-o/dyld.h>
#include "logging.h"

extern int run_engine(int pipe_write_fd);
extern int run_hud(int pipe_read_fd);

int main(int argc, char **argv) {
    // // 1. Child process mode (Engine)
    // if (argc == 3 && strcmp(argv[1], "--engine") == 0) {
    //     int pipe_fd = atoi(argv[2]);
    //     return run_engine(pipe_fd);
    // }

    // 2. Parent process mode (UI / Router)
    printf("[MAIN] Starting WhisperKey Router...\n");
    fflush(stdout);

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("[MAIN ERROR] Pipe creation failed");
        return 1;
    }

    // Get absolute path for self-execution
    char path[1024];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) {
        fprintf(stderr, "[MAIN ERROR] Executable path buffer too small\n");
        return 1;
    }

    printf("[MAIN] Forking process for Engine...\n");
    fflush(stdout);

    pid_t pid = fork();
    if (pid < 0) {
        perror("[MAIN ERROR] Fork failed");
        return 1;
    }

    if (pid == 0) {
        // CHILD PROCESS
        close(pipefd[0]); // Close read end
        
        // char fd_str[16];
        // snprintf(fd_str, sizeof(fd_str), "%d", pipefd[1]);
        
        // // Execute self as engine
        // LOG_INFO("Executing engine process: %s --engine %s", path, fd_str);
        // execl(path, "WhisperKey", "--engine", fd_str, NULL);
        
        run_engine(pipefd[1]);

        // // If execl fails:
        // perror("[MAIN ERROR] execl failed");
        return 1;
    }

    // PARENT PROCESS
    close(pipefd[1]); // Close write end
    printf("[MAIN] Router successfully spawned Engine (PID: %d). Starting HUD...\n", pid);
    fflush(stdout);
    
    return run_hud(pipefd[0]);
}