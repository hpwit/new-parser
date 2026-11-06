#pragma once
#ifndef __SCRIPT_SCHEDULER_H__
#define __SCRIPT_SCHEDULER_H__
// A registry of named scripts, each run on its own FreeRTOS task -- v1's
// scriptRuntime (add, execute, executeAsTask, kill, deleteExe, listExec)
// for v2, plus pause/restart and the CPU each script uses.
//
//   scriptScheduler.add("fire", fireSource);   // compile and register
//   scriptScheduler.run("fire");               // main() on its own task
//   scriptScheduler.pause("fire");             // vTaskSuspend()
//   scriptScheduler.restart("fire");           // vTaskResume()
//   scriptScheduler.kill("fire");              // vTaskDelete()
//   scriptScheduler.list();                    // table: state, CPU %, stack...
//   scriptScheduler.remove("fire");            // kill, free, forget
//
// Everything is FreeRTOS underneath: one task per running script
// (xTaskCreatePinnedToCore() through ScriptExecutable::executeAsTask()),
// vTaskSuspend()/vTaskResume()/vTaskDelete() for pause/restart/kill, a
// mutex around the registry, and the kernel's run-time statistics
// (vTaskGetInfo()'s ulRunTimeCounter) for the CPU figures. No STL: the
// registry is a fixed array of SCRIPT_SCHEDULER_MAX slots.
//
// CPU: each script's share of one core since the previous sample(), and
// each core's load (100% minus the time its idle task ran). list() takes
// a sample before printing, so calling it every second or so shows the
// last second. Needs FreeRTOS run-time statistics, which Arduino-ESP32
// enables (CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS); without them the
// CPU columns read -1.
//
// A script running as a task must give the CPU back now and then -- call
// a host function that blocks, such as a bound delay() -- or the lower
// priority tasks of its core never run: on core 0 that includes the idle
// task the task watchdog watches (5 s, then a reset by default).
//
// Only on ESP32 (FreeRTOS); the host test build leaves this out.
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include "script_executable.h"
#include "freertos/semphr.h"

#ifndef SCRIPT_SCHEDULER_MAX
#define SCRIPT_SCHEDULER_MAX 8
#endif
#define SCRIPT_NAME_MAX 24

// One script, as list() shows it (see info()).
struct ScriptInfo
{
    char name[SCRIPT_NAME_MAX];
    char function[SCRIPT_NAME_MAX]; // what run() started
    ScriptState state;
    float cpuPercent;     // of one core since the previous sample(); -1 unknown
    uint32_t stackFree;   // bytes of its task's stack never used; 0 if no task
    uint32_t codeSize;    // bytes of machine code
    uint32_t dataSize;    // bytes of globals
    int core;             // core it is pinned to, -1 if none
    UBaseType_t priority;
};

class ScriptScheduler
{
public:
    // Compiles `source` and registers it as `name` (replacing, after
    // killing it, any script already called that). False if it doesn't
    // compile (showCompileError() tells why), if `name` is empty or too
    // long, or if all SCRIPT_SCHEDULER_MAX slots are taken.
    bool add(const char *name, const char *source);

    // Runs `function` of script `name` on a task of its own and returns
    // at once (v1's executeAsTask). A task of that script still running
    // or paused is killed first. `core`: 0 or 1 to pin it, tskNO_AFFINITY
    // to let the scheduler choose.
    bool run(const char *name, const char *function = "main", uint32_t stackSize = 8192,
             UBaseType_t priority = 1, BaseType_t core = tskNO_AFFINITY);

    // Runs `function` of script `name` on the calling task and waits for
    // it to return (v1's execute).
    bool execute(const char *name, const char *function = "main");

    // pause: running -> paused, restart: paused -> running (where it
    // stopped), kill: running or paused -> stopped. False if `name` is
    // unknown or the script is not in a state the call applies to. A
    // script may pause or kill itself. See ScriptExecutable::pause() for
    // what stopping a task wherever it is implies.
    bool pause(const char *name);
    bool restart(const char *name);
    bool kill(const char *name);

    // Kills, frees and forgets script `name` (v1's deleteExe). A script
    // cannot remove itself (its code would be freed under it).
    bool remove(const char *name);

    // kill() / remove() for every script.
    void killAll();
    void removeAll();

    // Number of registered scripts, and the one at `index` (0 ..
    // count() - 1), for showing them your own way.
    int count();
    bool info(int index, ScriptInfo *out);
    bool info(const char *name, ScriptInfo *out);

    // The script itself, to bind it to an interrupt, call executeOnly()...
    // NULL if unknown. Valid until remove().
    ScriptExecutable *find(const char *name);

    // Takes a CPU sample: each script's share since the previous one,
    // and each core's load (coreLoad()).
    void sample();
    // Load of `core` (0 or 1) at the last sample(), in %; -1 unknown.
    float coreLoad(int core);

    // Prints the scripts (v1's listExec), after a sample():
    //  # name          state    CPU%  stack  code  data core prio function
    void list();

private:
    struct Slot
    {
        bool used;
        char name[SCRIPT_NAME_MAX];
        char function[SCRIPT_NAME_MAX];
        ScriptExecutable *exec;
        int core;
        UBaseType_t priority;
        uint32_t lastRunTime; // the task's run-time counter at the last sample
        float cpuPercent;
    };
    Slot slots[SCRIPT_SCHEDULER_MAX];
    SemaphoreHandle_t lock = NULL;
    portMUX_TYPE lockInit = portMUX_INITIALIZER_UNLOCKED;
    uint32_t lastSampleTime = 0;
    uint32_t lastIdleTime[2] = {0, 0};
    float load[2] = {-1, -1};

    void enter();
    void leave();
    int findSlot(const char *name);
    void fillInfo(int i, ScriptInfo *out);
};

extern ScriptScheduler scriptScheduler;

#endif
#endif
