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

int main()
{
    runUpdateJsonParametersTest();
    printf("\n%d passed, %d failed (of %d)\n", passed, failed, passed + failed);
    return failed ? 1 : 0;
}
