#pragma once
#include <cstddef>

// See alloc_tracker.cpp.
size_t allocLiveBytes();
// The same heap as the ESP32 would count it (see alloc_tracker.cpp).
size_t allocLiveEsp32();
size_t allocPeakEsp32();
size_t allocPeakBytes();
size_t allocLiveBlocks();
size_t allocPeakBlocks();
size_t allocTotal();
void allocResetPeak();
void allocPrintLiveBySite(int top);
// Keeps a by-site breakdown of the live heap each time the peak grows by
// more than 8 KB, so the one closest to the peak can be printed.
void allocTrackPeakSites(bool on);
void allocPrintPeakBySite(int top);
void allocPrintLargestBlocks(int top);
