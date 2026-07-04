// --- Logging Macros with immediate flush ---
#ifdef DEBUG
    #define LOG_DEBUG(fmt, ...) do { fprintf(stdout, "[ENGINE DEBUG] " fmt "\n", ##__VA_ARGS__); fflush(stdout); } while(0)
    #define LOG_INFO(fmt, ...)  do { fprintf(stdout, "[ENGINE INFO] " fmt "\n", ##__VA_ARGS__); fflush(stdout); } while(0)
    #define LOG_ERROR(fmt, ...) do { fprintf(stderr, "[ENGINE ERROR] " fmt "\n", ##__VA_ARGS__); fflush(stderr); } while(0)
#else
    #define LOG_DEBUG(fmt, ...)
    #define LOG_INFO(fmt, ...)
    #define LOG_ERROR(fmt, ...)
#endif
