#include <iostream>
#include <nvapi.h>
#include <Windows.h>
#include <map>
#include <mutex>

typedef void* (*QueryPtr)(uint32_t);

HMODULE hNvAPI;
std::string NVAPI_DLL = "nvapi64.dll";

int initNvAPI() {
    if (NvAPI_Initialize() != 0) {
        std::cerr << "error: failed to initialize nvapi\n";
        return 1;
    }

    hNvAPI = LoadLibraryA(NVAPI_DLL.c_str());

    if (!hNvAPI) {
        std::cerr << "error: failed loading nvapi64 dll\n";
        return 1;
    }

    return 0;
}

void* NvAPI_Query(uint32_t id) {
    return reinterpret_cast<QueryPtr>(GetProcAddress(hNvAPI, "nvapi_QueryInterface"))(id);
}

int NvAPI_GPU_SetPstateClientLimits(NvPhysicalGpuHandle hPhysicalGpu, unsigned int pstateType, unsigned int pStateLimit) {
    static NvAPI_Status(*pointer)(NvPhysicalGpuHandle, unsigned int, unsigned int) = NULL;

    if (!pointer) {
        pointer = (NvAPI_Status(*)(NvPhysicalGpuHandle, unsigned int, unsigned int))NvAPI_Query(0xFDFC7D49);
    }

    if (!pointer) {
        return 1;
    }

    return (*pointer)(hPhysicalGpu, pstateType, pStateLimit);
}

int setPState(NvPhysicalGpuHandle hPhysicalGpu, unsigned int pStateLimit) {
    // can be called from the UI thread and the process polling thread
    static std::mutex mutex;
    // last applied limit per GPU (0 = unlimited)
    static std::map<NvPhysicalGpuHandle, unsigned int> appliedLimits;

    std::lock_guard<std::mutex> lock(mutex);

    unsigned int current = 0;
    auto it = appliedLimits.find(hPhysicalGpu);
    if (it != appliedLimits.end()) {
        current = it->second;
    }

    if (current == pStateLimit) {
        return 0;
    }

    if (NvAPI_GPU_SetPstateClientLimits(hPhysicalGpu, 3, pStateLimit) != 0) {
        std::cerr << "error: NvAPI_SetPstateClientLimits failed\n";
        return 1;
    }

    if (pStateLimit == 0) {
        std::cout << "info: set P0 (unlimited)\n";
    } else {
        std::cout << "info: set P" << pStateLimit << "\n";
    }

    appliedLimits[hPhysicalGpu] = pStateLimit;
    return 0;
}
