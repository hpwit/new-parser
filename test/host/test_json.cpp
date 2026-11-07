// Verifies updateJsonParameters() (src/json_binding.cpp) actually parses
// a JSON document and writes the right values into a loaded script's
// data segment, at the addresses/types recorded by its `json "path" as
// <type> name;` bindings. The structural half of this feature (parsing,
// assembling, loading, and populating executable.jsonVars) is already
// covered by test/host/test_parser.cpp's default suite, which needs no
// ArduinoJson dependency at all -- this file is separate, and requires
// __JSON_OPTION__ plus a real ArduinoJson checkout, because it's the only
// thing in this project that actually needs ArduinoJson.
//
// Build/run (from this directory), pointing ARDUINOJSON_DIR at a
// checkout's src/ folder (e.g. a sibling ArduinoJson Arduino library):
//   make run-json ARDUINOJSON_DIR=/path/to/ArduinoJson/src
#include <cstdio>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

#include "parser.h"
#include "compiler_error.h"
#include "binding.h"
#include "asm_parser.h"
#include "asm_execute.h"
#include "json_binding.h"
#include "asm_serialize.h"

static int passed = 0, failed = 0;

static void runUpdateJsonParametersTest()
{
    const char *name = "updateJsonParameters() writes int/float JSON values into the right data-segment addresses";
    printf("RUNNING: %s\n", name);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0)
    {
        Parser p;
        p.clean();
        Script s;
        char *buf = strdup(
            "json \"wifi.ssid\" as int myInt;\n"
            "json \"wifi.count\" as float myFloat;\n"
            "void main(){myInt=myInt+1;}\n");
        s.addContent(buf);
        s.init();
        p.parse(&s, &__allTokens);

        bool ok = true;
        if (Error.error)
        {
            printf("       parse error=%d\n", Error.error);
            ok = false;
        }
        else
        {
            Binary bin = createBinary(&footer, &header, &content, false);
            if (bin.error.error)
            {
                printf("       assembler error: %s\n", bin.error.error_message ? bin.error.error_message : "?");
                ok = false;
            }
            else
            {
                executable exe = createExecutableFromBinary(&bin);
                if (exe.error.error)
                {
                    printf("       loader error: %s\n", exe.error.error_message ? exe.error.error_message : "?");
                    ok = false;
                }
                else
                {
                    asm_error_message_struct res = updateJsonParameters(&exe, "{\"wifi\":{\"ssid\":42,\"count\":3.5}}");
                    if (res.error != 0)
                    {
                        printf("       updateJsonParameters error: %s\n", res.error_message ? res.error_message : "?");
                        ok = false;
                    }
                    else
                    {
                        int32_t intVal = 0;
                        memcpy(&intVal, exe.data + exe.jsonVars.get(0).address, 4);
                        if (intVal != 42)
                        {
                            printf("       myInt = %d, expected 42\n", intVal);
                            ok = false;
                        }

                        float floatVal = 0;
                        memcpy(&floatVal, exe.data + exe.jsonVars.get(1).address, 4);
                        if (floatVal < 3.49f || floatVal > 3.51f)
                        {
                            printf("       myFloat = %f, expected 3.5\n", floatVal);
                            ok = false;
                        }
                    }
                    freeExecutable(&exe);
                }
            }
        }

        fflush(stdout);
        _exit(ok ? 0 : 1);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status))
    {
        failed++;
        printf("[CRASH] %s (signal %d)\n", name, WTERMSIG(status));
    }
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        passed++;
        printf("[PASS] %s\n", name);
    }
    else
    {
        failed++;
        printf("[FAIL] %s\n", name);
    }
}

static jsonVariable *jsonVar(executable *exe, const char *path)
{
    for (int i = 0; i < exe->jsonVars.size(); i++)
        if (strcmp(exe->jsonVars.get(i).json, path) == 0)
            return exe->jsonVars.getptr(i);
    return NULL;
}

static void runJsonTypesTest()
{
    const char *name = "json: v1's unquoted paths, each type at its own size, strings and arrays bounded, missing keys kept";
    printf("RUNNING: %s\n", name);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0)
    {
        Parser p;
        p.clean();
        Script s;
        char *buf = strdup(
            "json dp.value as int g2;\n"
            "json dp2 as float x;\n"
            "json \"name\" as char label[8];\n"
            "json \"list\" as int cells[3];\n"
            "json \"list.1\" as int second;\n"
            "json \"neg\" as s_int small;\n"
            "json \"ff\" as ffloat f;\n"
            "json \"nope\" as int kept;\n"
            "json \"byte\" as uint8_t u8;\n"
            "uint8_t tail;\n"
            "void main(){ tail = u8 + label[0] + cells[0] + small; }\n");
        s.addContent(buf);
        s.init();
        p.parse(&s, &__allTokens);
        bool ok = !Error.error;
        if (!ok)
            printf("       parse error=%d\n", Error.error);
        Binary bin;
        if (ok)
        {
            bin = createBinary(&footer, &header, &content, false);
            ok = !bin.error.error;
            if (!ok)
                printf("       assembler error: %s\n", bin.error.error_message ? bin.error.error_message : "?");
        }
        if (ok)
        {
            // and through a save and a load: the record keeps the sizes
            uint32_t size = 0;
            uint8_t *saved = serializeBinary(&bin, &size);
            Binary loaded = deserializeBinary(saved, size);
            executable exe = createExecutableFromBinary(&loaded);
            ok = !exe.error.error;
            if (!ok)
                printf("       loader error: %s\n", exe.error.error_message ? exe.error.error_message : "?");
            if (ok)
            {
                memset(exe.data, 0xAA, exe.data_size); // to see what gets written
                asm_error_message_struct res = updateJsonParameters(&exe,
                    "{\"dp\":{\"value\":6},\"dp2\":4.5,\"name\":\"a long label\",\"list\":[10,20,30,40],"
                    "\"neg\":-5,\"ff\":1.5,\"byte\":200}");
                if (res.error)
                {
                    printf("       updateJsonParameters: %s\n", res.error_message);
                    ok = false;
                }
                struct { const char *path; int bytes; uint8_t expect[16]; } checks[] = {
                    {"dp.value", 4, {6, 0, 0, 0}},
                    {"name", 9, {'a', ' ', 'l', 'o', 'n', 'g', ' ', 0, 0xAA}},     // cut, ended, nothing after
                    {"list", 13, {10, 0, 0, 0, 20, 0, 0, 0, 30, 0, 0, 0, 0xAA}},     // 3 cells, not 4
                    {"list.1", 4, {20, 0, 0, 0}},
                    {"neg", 3, {0xFB, 0xFF, 0xAA}},                                 // 2 bytes only
                    {"ff", 4, {0x00, 0x80, 0x01, 0x00}},                            // 1.5 in 16.16
                    {"nope", 4, {0xAA, 0xAA, 0xAA, 0xAA}},                          // not in the JSON
                    {"byte", 2, {200, 0xAA}},                                       // 1 byte only
                };
                for (auto &c : checks)
                {
                    jsonVariable *jv = jsonVar(&exe, c.path);
                    if (jv == NULL)
                    {
                        printf("       no json variable for %s\n", c.path);
                        ok = false;
                        continue;
                    }
                    if (memcmp(exe.data + jv->address, c.expect, c.bytes) != 0)
                    {
                        printf("       %s (size %d):", c.path, jv->size);
                        for (int k = 0; k < c.bytes; k++)
                            printf(" %02x", ((uint8_t *)exe.data)[jv->address + k]);
                        printf("\n");
                        ok = false;
                    }
                }
                jsonVariable *xv = jsonVar(&exe, "dp2");
                float x = 0;
                if (xv != NULL)
                    memcpy(&x, exe.data + xv->address, 4);
                if (x != 4.5f)
                {
                    printf("       x = %f, expected 4.5\n", x);
                    ok = false;
                }
                freeExecutable(&exe);
            }
        }
        fflush(stdout);
        _exit(ok ? 0 : 1);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status))
    {
        failed++;
        printf("[CRASH] %s (signal %d)\n", name, WTERMSIG(status));
    }
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        passed++;
        printf("[PASS] %s\n", name);
    }
    else
    {
        failed++;
        printf("[FAIL] %s\n", name);
    }
}

int main()
{
    runUpdateJsonParametersTest();
    runJsonTypesTest();
    printf("\n%d passed, %d failed (of %d)\n", passed, failed, passed + failed);
    return failed ? 1 : 0;
}
