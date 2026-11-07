#pragma once
#ifndef __JSON_BINDING__
#define __JSON_BINDING__
// Applies a JSON document to a loaded script's `json "path" as <type>
// name;`-bound variables (see parser.cpp's jsonBindingNode handling and
// asm_types.h's jsonVariable). Ported from upstream ESPLiveScript's
// execute_asm.h updateParameters()/getfromJson().
//
// Real support (parsing `json` via ArduinoJson and writing the results
// into ex->data) compiles in when ArduinoJson is visible to the build
// (the sketch includes <ArduinoJson.h>) or __JSON_OPTION__ is defined,
// so declaring or even loading a script that uses `json ... as ...;`
// never pulls in ArduinoJson; only applying a JSON document does.
// Without it this reports an error if the script has variables to fill,
// and is callable either way, so callers don't need their own #ifdef.
#include "asm_types.h"

// Returns error.error == 0 on success. If `json` is NULL/empty, or the
// script has no `json ... as ...;` bindings, this is a no-op success
// either way (with or without __JSON_OPTION__).
asm_error_message_struct updateJsonParameters(executable *ex, const char *json);

#endif
