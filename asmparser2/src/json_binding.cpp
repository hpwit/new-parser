#include "json_binding.h"
#include "parser_enum.h"
#include <stdlib.h>
#include <string.h>

#ifdef __JSON_OPTION__
#include <ArduinoJson.h>

// Splits a dot-separated path ("wifi.ssid") and walks nested objects one
// key at a time -- same "." convention as upstream's getfromJson().
static JsonVariant getFromJsonPath(JsonDocument &doc, const char *path)
{
    JsonVariant v = doc.as<JsonVariant>();
    char *copy = strdup(path);
    char *saveptr = NULL;
    char *tok = strtok_r(copy, ".", &saveptr);
    while (tok != NULL)
    {
        v = v[tok];
        tok = strtok_r(NULL, ".", &saveptr);
    }
    free(copy);
    return v;
}

asm_error_message_struct updateJsonParameters(executable *ex, const char *json)
{
    asm_error_message_struct res;
    res.error = 0;
    res.error_message = NULL;
    if (json == NULL || json[0] == 0 || ex->jsonVars.size() == 0)
        return res;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err)
    {
        res.error = 1;
        res.error_message = (char *)"deserializeJson() failed";
        return res;
    }

    for (int i = 0; i < ex->jsonVars.size(); i++)
    {
        jsonVariable jv = ex->jsonVars.get(i);
        JsonVariant v = getFromJsonPath(doc, jv.json);
        switch ((varTypeEnum)jv.type)
        {
        case __uint8_t__:
        case __uint16_t__:
        case __uint32_t__:
        case __bool__:
        {
            uint32_t val = (uint32_t)(v.as<long>());
            memcpy(ex->data + jv.address, &val, 4);
            break;
        }
        case __int__:
        case __s_int__:
        {
            int val = v.as<int>();
            memcpy(ex->data + jv.address, &val, 4);
            break;
        }
        case __float__:
        {
            float val = v.as<float>();
            memcpy(ex->data + jv.address, &val, 4);
            break;
        }
        case __char__:
        {
            const char *val = v.as<const char *>();
            if (val != NULL)
                memcpy(ex->data + jv.address, val, strlen(val));
            break;
        }
        default:
            break;
        }
    }
    return res;
}

#else

asm_error_message_struct updateJsonParameters(executable *ex, const char *json)
{
    asm_error_message_struct res;
    res.error = 0;
    res.error_message = NULL;
    if (json == NULL || json[0] == 0 || ex->jsonVars.size() == 0)
        return res;
    res.error = 1;
    res.error_message = (char *)"updateJsonParameters: built without __JSON_OPTION__ (ArduinoJson support)";
    return res;
}

#endif
