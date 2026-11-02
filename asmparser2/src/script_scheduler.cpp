#include "script_scheduler.h"

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include <stdio.h>
#include <string.h>
#include "freertos/idf_additions.h"

ScriptScheduler scriptScheduler;

// The kernel's run-time statistics: each task's ulRunTimeCounter, and
// the clock it counts in (portGET_RUN_TIME_COUNTER_VALUE()).
#if defined(configGENERATE_RUN_TIME_STATS) && configGENERATE_RUN_TIME_STATS == 1
#define SCHEDULER_HAS_STATS 1
static uint32_t runTimeNow()
{
    return (uint32_t)portGET_RUN_TIME_COUNTER_VALUE();
}
static uint32_t taskRunTime(TaskHandle_t task)
{
    TaskStatus_t st;
    // eRunning: the state is not wanted, so not worked out
    vTaskGetInfo(task, &st, pdFALSE, eRunning);
    return (uint32_t)st.ulRunTimeCounter;
}
#else
#define SCHEDULER_HAS_STATS 0
#endif

static const char *stateName(ScriptState s)
{
    switch (s)
    {
    case ScriptState::running:
        return "running";
    case ScriptState::paused:
        return "paused";
    case ScriptState::stopped:
        return "stopped";
    default:
        return "ready";
    }
}

static bool validName(const char *name)
{
    return name != NULL && name[0] != 0 && strlen(name) < SCRIPT_NAME_MAX;
}

// The registry's mutex, created the first time it is needed (a global's
// constructor may run before FreeRTOS is ready to hand one out).
void ScriptScheduler::enter()
{
    if (lock == NULL)
    {
        SemaphoreHandle_t m = xSemaphoreCreateMutex();
        bool mine = false;
        portENTER_CRITICAL(&lockInit);
        if (lock == NULL)
        {
            lock = m;
            mine = true;
        }
        portEXIT_CRITICAL(&lockInit);
        if (!mine)
            vSemaphoreDelete(m);
    }
    xSemaphoreTake(lock, portMAX_DELAY);
}

void ScriptScheduler::leave()
{
    xSemaphoreGive(lock);
}

int ScriptScheduler::findSlot(const char *name)
{
    if (name == NULL)
        return -1;
    for (int i = 0; i < SCRIPT_SCHEDULER_MAX; i++)
        if (slots[i].used && strcmp(slots[i].name, name) == 0)
            return i;
    return -1;
}

bool ScriptScheduler::add(const char *name, const char *source)
{
    if (!validName(name) || source == NULL)
        return false;
    // a script of that name already: replace it
    remove(name);
    enter();
    int slot = -1;
    for (int i = 0; i < SCRIPT_SCHEDULER_MAX && slot < 0; i++)
        if (!slots[i].used)
            slot = i;
    if (slot < 0)
    {
        leave();
        printf("scriptScheduler: no free slot for %s (SCRIPT_SCHEDULER_MAX %d)\n", name, SCRIPT_SCHEDULER_MAX);
        return false;
    }
    // Compiling under the lock: the compiler itself is not reentrant, so
    // two tasks adding scripts at once must take turns anyway.
    ScriptExecutable *exec = new ScriptExecutable(parseScript(source));
    if (!exec->isExeExists())
    {
        delete exec;
        leave();
        return false;
    }
    Slot &s = slots[slot];
    memset(&s, 0, sizeof(s));
    s.used = true;
    strcpy(s.name, name);
    s.exec = exec;
    s.core = -1;
    s.cpuPercent = -1;
    leave();
    return true;
}

bool ScriptScheduler::run(const char *name, const char *function, uint32_t stackSize, UBaseType_t priority,
                          BaseType_t core)
{
    if (!validName(function))
        return false;
    enter();
    int i = findSlot(name);
    if (i < 0 || (slots[i].exec->task() != NULL && slots[i].exec->task() == xTaskGetCurrentTaskHandle()))
    {
        // unknown, or a script restarting itself (killing its own task
        // first would never return)
        leave();
        return false;
    }
    Slot &s = slots[i];
    // the task keeps pointing at these: copied into the slot, which
    // stays put until remove()
    strcpy(s.function, function);
    bool ok = s.exec->executeAsTask(s.function, stackSize, priority, core, s.name);
    s.core = core == tskNO_AFFINITY ? -1 : (int)core;
    s.priority = priority;
    s.lastRunTime = 0; // a new task's counter starts at 0
    s.cpuPercent = -1;
    leave();
    return ok;
}

bool ScriptScheduler::execute(const char *name, const char *function)
{
    enter();
    ScriptExecutable *exec = findSlot(name) >= 0 ? slots[findSlot(name)].exec : NULL;
    leave();
    // not under the lock: the script may take its time, or call back in
    return exec != NULL && exec->execute(function);
}

bool ScriptScheduler::pause(const char *name)
{
    enter();
    int i = findSlot(name);
    ScriptExecutable *exec = i >= 0 ? slots[i].exec : NULL;
    bool self = exec != NULL && exec->task() == xTaskGetCurrentTaskHandle();
    if (self)
    {
        // pausing the calling task: let go of the registry first
        leave();
        return exec->pause();
    }
    bool ok = exec != NULL && exec->pause();
    leave();
    return ok;
}

bool ScriptScheduler::restart(const char *name)
{
    enter();
    int i = findSlot(name);
    bool ok = i >= 0 && slots[i].exec->restart();
    leave();
    return ok;
}

bool ScriptScheduler::kill(const char *name)
{
    enter();
    int i = findSlot(name);
    ScriptExecutable *exec = i >= 0 ? slots[i].exec : NULL;
    if (exec != NULL && exec->task() == xTaskGetCurrentTaskHandle())
    {
        // killing the calling task never returns: let go first
        slots[i].cpuPercent = -1;
        leave();
        return exec->kill();
    }
    bool ok = exec != NULL && exec->kill();
    if (exec != NULL)
        slots[i].cpuPercent = -1;
    leave();
    return ok;
}

bool ScriptScheduler::remove(const char *name)
{
    enter();
    int i = findSlot(name);
    if (i < 0 || slots[i].exec->task() == xTaskGetCurrentTaskHandle())
    {
        leave();
        return false;
    }
    ScriptExecutable *exec = slots[i].exec;
    slots[i].used = false;
    slots[i].exec = NULL;
    exec->kill();
    delete exec; // frees the compiled script
    leave();
    return true;
}

void ScriptScheduler::killAll()
{
    for (int i = 0; i < SCRIPT_SCHEDULER_MAX; i++)
    {
        char name[SCRIPT_NAME_MAX];
        enter();
        bool used = slots[i].used;
        if (used)
            strcpy(name, slots[i].name);
        leave();
        if (used)
            kill(name);
    }
}

void ScriptScheduler::removeAll()
{
    for (int i = 0; i < SCRIPT_SCHEDULER_MAX; i++)
    {
        char name[SCRIPT_NAME_MAX];
        enter();
        bool used = slots[i].used;
        if (used)
            strcpy(name, slots[i].name);
        leave();
        if (used)
            remove(name);
    }
}

int ScriptScheduler::count()
{
    enter();
    int n = 0;
    for (int i = 0; i < SCRIPT_SCHEDULER_MAX; i++)
        if (slots[i].used)
            n++;
    leave();
    return n;
}

void ScriptScheduler::fillInfo(int i, ScriptInfo *out)
{
    Slot &s = slots[i];
    memset(out, 0, sizeof(*out));
    strcpy(out->name, s.name);
    strcpy(out->function, s.function);
    out->state = s.exec->state();
    // over the last sample interval: a script paused since still shows
    // what it used before; one that is not running has none
    out->cpuPercent = out->state == ScriptState::running || out->state == ScriptState::paused ? s.cpuPercent : -1;
    TaskHandle_t task = s.exec->task();
    // in bytes: ESP-IDF counts stacks in bytes
    out->stackFree = task != NULL ? (uint32_t)uxTaskGetStackHighWaterMark(task) : 0;
    out->codeSize = s.exec->exe.binary_size;
    out->dataSize = s.exec->exe.data_size;
    out->core = s.core;
    out->priority = s.priority;
}

bool ScriptScheduler::info(int index, ScriptInfo *out)
{
    enter();
    int n = 0;
    for (int i = 0; i < SCRIPT_SCHEDULER_MAX; i++)
    {
        if (!slots[i].used)
            continue;
        if (n++ == index)
        {
            fillInfo(i, out);
            leave();
            return true;
        }
    }
    leave();
    return false;
}

bool ScriptScheduler::info(const char *name, ScriptInfo *out)
{
    enter();
    int i = findSlot(name);
    if (i >= 0)
        fillInfo(i, out);
    leave();
    return i >= 0;
}

ScriptExecutable *ScriptScheduler::find(const char *name)
{
    enter();
    int i = findSlot(name);
    ScriptExecutable *exec = i >= 0 ? slots[i].exec : NULL;
    leave();
    return exec;
}

void ScriptScheduler::sample()
{
#if SCHEDULER_HAS_STATS
    enter();
    uint32_t now = runTimeNow();
    uint32_t elapsed = now - lastSampleTime;
    bool first = lastSampleTime == 0;
    for (int i = 0; i < SCRIPT_SCHEDULER_MAX; i++)
    {
        Slot &s = slots[i];
        if (!s.used)
            continue;
        TaskHandle_t task = s.exec->task();
        ScriptState st = s.exec->state();
        if (task == NULL || (st != ScriptState::running && st != ScriptState::paused))
        {
            s.cpuPercent = 0;
            continue;
        }
        uint32_t t = taskRunTime(task);
        s.cpuPercent = first || elapsed == 0 ? -1 : (float)(t - s.lastRunTime) * 100.0f / (float)elapsed;
        if (s.cpuPercent > 100)
            s.cpuPercent = 100; // the two counters are not read at the same instant
        s.lastRunTime = t;
    }
    for (int c = 0; c < 2; c++)
    {
        if (c >= configNUMBER_OF_CORES)
        {
            load[c] = -1;
            continue;
        }
        uint32_t idle = taskRunTime(xTaskGetIdleTaskHandleForCore(c));
        float idlePercent = (float)(idle - lastIdleTime[c]) * 100.0f / (float)(elapsed ? elapsed : 1);
        load[c] = first ? -1 : (idlePercent > 100 ? 0 : 100 - idlePercent);
        lastIdleTime[c] = idle;
    }
    lastSampleTime = now;
    leave();
#endif
}

float ScriptScheduler::coreLoad(int core)
{
    return core >= 0 && core < 2 ? load[core] : -1;
}

void ScriptScheduler::list()
{
    sample();
    printf(" #  %-16s %-8s %6s %7s %6s %6s %4s %4s  %s\n", "name", "state", "CPU%", "stack", "code", "data", "core",
           "prio", "function");
    ScriptInfo inf;
    int n = 0;
    while (info(n, &inf))
    {
        char cpu[12], core[8];
        if (inf.cpuPercent < 0)
            strcpy(cpu, "-");
        else
            snprintf(cpu, sizeof(cpu), "%.1f", inf.cpuPercent);
        if (inf.core < 0)
            strcpy(core, "any");
        else
            snprintf(core, sizeof(core), "%d", inf.core);
        printf("%2d  %-16s %-8s %6s %7u %6u %6u %4s %4u  %s\n", n + 1, inf.name, stateName(inf.state), cpu,
               (unsigned)inf.stackFree, (unsigned)inf.codeSize, (unsigned)inf.dataSize, core, (unsigned)inf.priority,
               inf.function[0] ? inf.function : "-");
        n++;
    }
    if (n == 0)
        printf("    (no script)\n");
    if (coreLoad(0) >= 0)
        printf("    core 0 load %.1f%%   core 1 load %.1f%%\n", coreLoad(0), coreLoad(1));
}
#endif
