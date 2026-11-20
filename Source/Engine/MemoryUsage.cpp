// The process's resident memory, for the CPU / RAM module (and the memory each
// module takes when it is made). Kept apart from JUCE so the platform headers
// (windows.h above all) stay out of everything else.

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <psapi.h>
#elif defined (__APPLE__)
 #include <mach/mach.h>
#else
 #include <cstdio>
 #include <unistd.h>
#endif

namespace aquanode
{
    long long processResidentBytes()   // (juce::int64)
    {
       #if defined (_WIN32)
        PROCESS_MEMORY_COUNTERS pmc {};
        if (K32GetProcessMemoryInfo (GetCurrentProcess(), &pmc, sizeof (pmc)))
            return (long long) pmc.WorkingSetSize;
        return 0;
       #elif defined (__APPLE__)
        mach_task_basic_info info {};
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        if (task_info (mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t) &info, &count) == KERN_SUCCESS)
            return (long long) info.resident_size;
        return 0;
       #else
        // Linux and Android: /proc/self/statm, the second field in pages
        long long pages = 0, resident = 0;
        if (FILE* f = std::fopen ("/proc/self/statm", "r"))
        {
            if (std::fscanf (f, "%lld %lld", &pages, &resident) != 2) resident = 0;
            std::fclose (f);
        }
        return resident * (long long) sysconf (_SC_PAGESIZE);
       #endif
    }
}
