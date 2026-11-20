#pragma once

#include <iostream>
#include <nvapi.h>

int initNvAPI();

// pStateLimit: 0 = unlimited (P0), N = limit the GPU to P<N>
// the last applied limit is cached per GPU so redundant driver calls are skipped
int setPState(NvPhysicalGpuHandle hPhysicalGpu, unsigned int pStateLimit);
