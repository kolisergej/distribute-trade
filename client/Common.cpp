#include "Common.h"

mutex g_nextMachineCoreNumberMutex;
size_t g_nextMachineCoreNumber = -1;

bool setThreadAfinity(const unsigned machineCoreNumber) {
    bool result = true;
#ifdef __linux__
    cpu_set_t cpuset;
    const pthread_t threadId = pthread_self();
    CPU_ZERO(&cpuset);
    CPU_SET(machineCoreNumber, &cpuset);
    result = (pthread_setaffinity_np(threadId, sizeof(cpu_set_t), &cpuset) == 0);
#else
    (void)machineCoreNumber;
    cout << "set thread afinity not supported" << endl;
#endif

    return result;
}

bool setThreadAfinity() {
    std::lock_guard<std::mutex> lock(g_nextMachineCoreNumberMutex);
    g_nextMachineCoreNumber = (g_nextMachineCoreNumber + 1) % g_machineCoreCount;
    return setThreadAfinity(g_nextMachineCoreNumber);
}
