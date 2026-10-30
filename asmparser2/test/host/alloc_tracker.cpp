// Heap accounting for test_large_script: the compiler sources are built
// for that target with -Dmalloc=trk_malloc (calloc, realloc, free, strdup,
// strndup likewise -- see the Makefile), so every block the compiler
// allocates passes through here, and operator new/delete are replaced
// below. This file itself is built without those defines and calls the
// real allocator.
//
// Sizes are the requested ones, as the compiler sees them -- on the host,
// with 8-byte pointers, so structures holding pointers are larger than on
// the ESP32. Each block also remembers its caller, so the live heap can be
// broken down by allocation site.
#include "alloc_tracker.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <new>

namespace
{
const size_t kTable = 1 << 20; // must stay a power of two
struct Entry
{
    void *ptr;    // NULL: empty, kTombstone: removed
    size_t size;
    void *caller;
    void *caller2; // one frame further up: who called p_realloc etc.
};
Entry table[kTable];
void *const kTombstone = (void *)1;

size_t liveBytes = 0, peakBytes = 0, liveBlocks = 0, peakBlocks = 0, totalAllocs = 0;
// What the same blocks would cost on the ESP32's heap: measured on a board,
// each block takes its size rounded up to 4 (at least 4) plus 16 bytes.
size_t liveEsp = 0, peakEsp = 0;
size_t espCost(size_t n) { return (n < 4 ? 4 : (n + 3) & ~(size_t)3) + 16; }
size_t snapshotAt = 0; // peak at the last breakdown taken
bool snapshotting = false;
void takePeakSnapshot();

size_t slot(void *p) { return ((uintptr_t)p >> 4) & (kTable - 1); }

void record(void *p, size_t n, void *caller, void *caller2 = NULL)
{
    if (p == NULL)
        return;
    size_t h = slot(p);
    while (table[h].ptr != NULL && table[h].ptr != kTombstone && table[h].ptr != p)
        h = (h + 1) & (kTable - 1);
    table[h].ptr = p;
    table[h].size = n;
    table[h].caller = caller;
    table[h].caller2 = caller2;
    liveBytes += n;
    liveEsp += espCost(n);
    if (liveEsp > peakEsp)
        peakEsp = liveEsp;
    liveBlocks++;
    totalAllocs++;
    if (liveBytes > peakBytes)
    {
        peakBytes = liveBytes;
        if (snapshotting && peakBytes > snapshotAt + 8192)
        {
            snapshotAt = peakBytes;
            takePeakSnapshot();
        }
    }
    if (liveBlocks > peakBlocks)
        peakBlocks = liveBlocks;
}

// Forgets p; false if it was never recorded (allocated elsewhere).
bool forget(void *p)
{
    if (p == NULL)
        return false;
    size_t h = slot(p);
    while (table[h].ptr != NULL)
    {
        if (table[h].ptr == p)
        {
            liveBytes -= table[h].size;
            liveEsp -= espCost(table[h].size);
            liveBlocks--;
            table[h].ptr = kTombstone;
            return true;
        }
        h = (h + 1) & (kTable - 1);
    }
    return false;
}
} // namespace

extern "C" void *trk_malloc(size_t n)
{
    void *p = malloc(n);
    record(p, n, __builtin_return_address(0));
    return p;
}

extern "C" void *trk_calloc(size_t c, size_t n)
{
    void *p = calloc(c, n);
    record(p, c * n, __builtin_return_address(0));
    return p;
}

extern "C" void *trk_realloc(void *q, size_t n)
{
    void *p = realloc(q, n);
    if (p != NULL || n == 0)
        forget(q);
    record(p, n, __builtin_return_address(0), __builtin_frame_address(1) ? __builtin_return_address(1) : NULL);
    return p;
}

extern "C" void trk_free(void *p)
{
    forget(p);
    free(p);
}

extern "C" char *trk_strdup(const char *s)
{
    char *p = strdup(s);
    record(p, strlen(s) + 1, __builtin_return_address(0));
    return p;
}

extern "C" char *trk_strndup(const char *s, size_t n)
{
    char *p = strndup(s, n);
    if (p != NULL)
        record(p, strlen(p) + 1, __builtin_return_address(0));
    return p;
}

void *operator new(size_t n)
{
    void *p = malloc(n);
    if (p == NULL)
        throw std::bad_alloc();
    record(p, n, __builtin_return_address(0));
    return p;
}
void *operator new[](size_t n)
{
    void *p = malloc(n);
    if (p == NULL)
        throw std::bad_alloc();
    record(p, n, __builtin_return_address(0));
    return p;
}
void operator delete(void *p) noexcept { trk_free(p); }
void operator delete[](void *p) noexcept { trk_free(p); }
void operator delete(void *p, size_t) noexcept { trk_free(p); }
void operator delete[](void *p, size_t) noexcept { trk_free(p); }

size_t allocLiveBytes() { return liveBytes; }
size_t allocLiveEsp32() { return liveEsp; }
size_t allocPeakEsp32() { return peakEsp; }
size_t allocPeakBytes() { return peakBytes; }
size_t allocLiveBlocks() { return liveBlocks; }
size_t allocPeakBlocks() { return peakBlocks; }
size_t allocTotal() { return totalAllocs; }
void allocResetPeak()
{
    peakBytes = liveBytes;
    peakBlocks = liveBlocks;
    peakEsp = liveEsp;
}

namespace
{
struct Site
{
    const char *name;
    size_t bytes, blocks, esp;
};

// The live heap grouped by the function that allocated it, largest first.
int liveBySite(Site *sites, int max)
{
    int n = 0;
    for (size_t i = 0; i < kTable; i++)
    {
        if (table[i].ptr == NULL || table[i].ptr == kTombstone)
            continue;
        Dl_info info;
        const char *name = dladdr(table[i].caller, &info) && info.dli_sname ? info.dli_sname : "?";
        int k = 0;
        while (k < n && strcmp(sites[k].name, name) != 0)
            k++;
        if (k == n)
        {
            if (n == max)
                continue;
            sites[n++] = {name, 0, 0, 0};
        }
        sites[k].bytes += table[i].size;
        sites[k].esp += espCost(table[i].size);
        sites[k].blocks++;
    }
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (sites[j].esp > sites[i].esp)
            {
                Site t = sites[i];
                sites[i] = sites[j];
                sites[j] = t;
            }
    return n;
}

Site peakSites[512];
int peakSiteCount = 0;

void takePeakSnapshot()
{
    peakSiteCount = liveBySite(peakSites, 512);
}
} // namespace

void allocPrintLiveBySite(int top)
{
    static Site sites[512];
    int n = liveBySite(sites, 512);
    for (int i = 0; i < n && i < top; i++)
        printf("      %8zu B %6zu blocks ~%7zu B ESP32  %s\n", sites[i].bytes, sites[i].blocks, sites[i].esp, sites[i].name);
}

void allocTrackPeakSites(bool on)
{
    snapshotting = on;
    snapshotAt = peakBytes;
}

void allocPrintPeakBySite(int top)
{
    for (int i = 0; i < peakSiteCount && i < top; i++)
        printf("      %8zu B %6zu blocks ~%7zu B ESP32  %s\n", peakSites[i].bytes, peakSites[i].blocks, peakSites[i].esp,
               peakSites[i].name);
}

// The largest live blocks, with their allocation site and the one above it.
void allocPrintLargestBlocks(int top)
{
    static int idx[64];
    int n = 0;
    for (size_t i = 0; i < kTable; i++)
    {
        if (table[i].ptr == NULL || table[i].ptr == kTombstone)
            continue;
        if (n < top)
            idx[n++] = (int)i;
        else
        {
            int m = 0;
            for (int k = 1; k < n; k++)
                if (table[idx[k]].size < table[idx[m]].size)
                    m = k;
            if (table[i].size > table[idx[m]].size)
                idx[m] = (int)i;
        }
    }
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (table[idx[j]].size > table[idx[i]].size)
            {
                int t = idx[i];
                idx[i] = idx[j];
                idx[j] = t;
            }
    for (int i = 0; i < n; i++)
    {
        Dl_info a, b;
        const char *n1 = dladdr(table[idx[i]].caller, &a) && a.dli_sname ? a.dli_sname : "?";
        const char *n2 = table[idx[i]].caller2 && dladdr(table[idx[i]].caller2, &b) && b.dli_sname ? b.dli_sname : "";
        printf("      %8zu B  %s <- %s\n", table[idx[i]].size, n1, n2);
    }
}
