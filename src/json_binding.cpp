#include "json_binding.h"
#include "parser_enum.h"
#include <stdlib.h>
#include <string.h>

// ArduinoJson support compiles in by itself when ArduinoJson is visible
// to this file, which is the case as soon as the sketch includes
// <ArduinoJson.h> (the Arduino build then puts the library on every
// include path); __JSON_OPTION__ still forces it. Without it, a script
// using `json ... as ...;` still compiles and loads, and applying a JSON
// document reports what to add.
#if !defined(__JSON_OPTION__) && defined(__has_include)
#if __has_include(<ArduinoJson.h>)
#define __JSON_OPTION__
#endif
#endif

#ifdef __JSON_OPTION__
#include <ArduinoJson.h>

// "dp.value", "leds.3": each part is an object key, or an array index
// when the value there is an array.
static JsonVariant getFromJsonPath(JsonVariant v, const char *path)
{
    char *copy = strdup(path);
    char *saveptr = NULL;
    for (char *tok = strtok_r(copy, ".", &saveptr); tok != NULL; tok = strtok_r(NULL, ".", &saveptr))
    {
        bool digits = *tok != 0;
        for (const char *c = tok; *c; c++)
            if (*c < '0' || *c > '9')
                digits = false;
        if (digits && v.is<JsonArray>())
            v = v[atoi(tok)];
        else
            v = v[tok];
    }
    free(copy);
    return v;
}

static int elementSize(varTypeEnum type)
{
    switch (type)
    {
    case __uint8_t__:
    case __char__:
    case __bool__:
        return 1;
    case __uint16_t__:
    case __s_int__:
        return 2;
    default:
        return 4; // int, uint32_t, float, ffloat
    }
}

// One element, exactly its own size: a uint8_t used to get 4 bytes,
// overwriting the variables after it.
static void writeElement(uint8_t *dst, varTypeEnum type, JsonVariant v)
{
    switch (type)
    {
    case __uint8_t__:
    case __char__:
    case __bool__:
    {
        uint8_t val = type == __bool__ ? (uint8_t)(v.as<bool>() ? 1 : 0) : (uint8_t)v.as<long>();
        memcpy(dst, &val, 1);
        break;
    }
    case __uint16_t__:
    case __s_int__:
    {
        uint16_t val = (uint16_t)v.as<long>();
        memcpy(dst, &val, 2);
        break;
    }
    case __float__:
    {
        float val = v.as<float>();
        memcpy(dst, &val, 4);
        break;
    }
    case __ffloat__:
    {
        int32_t val = (int32_t)(v.as<float>() * 65536.0f);
        memcpy(dst, &val, 4);
        break;
    }
    case __uint32_t__:
    {
        uint32_t val = (uint32_t)v.as<unsigned long>();
        memcpy(dst, &val, 4);
        break;
    }
    default: // int
    {
        int32_t val = (int32_t)v.as<long>();
        memcpy(dst, &val, 4);
        break;
    }
    }
}

asm_error_message_struct updateJsonParameters(executable *ex, const char *json)
{
    asm_error_message_struct res;
    res.error = 0;
    res.error_message = NULL;
    if (json == NULL || json[0] == 0 || ex->jsonVars.size() == 0)
        return res;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(1024 + 2 * strlen(json));
#endif
    DeserializationError err = deserializeJson(doc, json);
    if (err)
    {
        res.error = 1;
        res.error_message = (char *)"the JSON can't be read (deserializeJson() failed)";
        return res;
    }

    for (int i = 0; i < ex->jsonVars.size(); i++)
    {
        jsonVariable jv = ex->jsonVars.get(i);
        JsonVariant v = getFromJsonPath(doc.as<JsonVariant>(), jv.json);
        if (v.isNull())
            continue; // not in this document: the variable keeps its value
        varTypeEnum type = (varTypeEnum)jv.type;
        uint8_t *dst = (uint8_t *)ex->data + jv.address;
        int esize = elementSize(type);
        int count = jv.size >= esize ? jv.size / esize : 1;

        if (type == __char__ && v.is<const char *>())
        {
            // a string, cut to the array's size and always terminated
            // (size 0: a binary saved before sizes were recorded)
            const char *val = v.as<const char *>();
            size_t n = strlen(val);
            if (jv.size > 0)
            {
                if (n > (size_t)jv.size - 1)
                    n = jv.size - 1;
                memcpy(dst, val, n);
                dst[n] = 0;
            }
            else
                memcpy(dst, val, n);
        }
        else if (v.is<JsonArray>())
        {
            // an array: as many elements as both have
            JsonArray a = v.as<JsonArray>();
            int n = (int)a.size() < count ? (int)a.size() : count;
            for (int k = 0; k < n; k++)
                writeElement(dst + k * esize, type, a[k]);
        }
        else
            writeElement(dst, type, v);
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
    res.error_message = (char *)"JSON support needs ArduinoJson: add #include <ArduinoJson.h> to your sketch";
    return res;
}

#endif
