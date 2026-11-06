#pragma once
#ifndef __JSON_BINDING__
#define __JSON_BINDING__
// Applies a JSON document to a loaded script's `json "path" as <type>
// name;`-bound variables (see parser.cpp's jsonBindingNode handling and
// asm_types.h's jsonVariable). Ported from upstream ESPLiveScript's
// execute_asm.h updateParameters()/getfromJson().
//
// Real support (parsing `json` via ArduinoJson and writing the results
// into ex->data) only compiles in when __JSON_OPTION__ is defined --
// matching upstream's own opt-in guard, so declaring or even loading a
// script that happens to use `json ... as ...;` never pulls in
// ArduinoJson as a dependency; only actually wanting to *apply* a JSON
// document does. Without __JSON_OPTION__ this is a harmless no-op that
// reports an error if the script actually has any jsonVars to populate,
// matching asm_execute.cpp's callXtensaDirect's own "no-op off-target"
// pattern -- callable either way, so callers don't need their own
// #ifdef.
#include "asm_types.h"

// Returns error.error == 0 on success. If `json` is NULL/empty, or the
// script has no `json ... as ...;` bindings, this is a no-op success
// either way (with or without __JSON_OPTION__).
asm_error_message_struct updateJsonParameters(executable *ex, const char *json);

#endif
