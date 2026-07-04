#ifndef WHISPER_WRAPPER_H
#define WHISPER_WRAPPER_H

#include "/opt/homebrew/include/whisper.h"
#include "/opt/homebrew/include/ggml-backend.h"

// Swift imports opaque C pointer types more reliably when the C type is also a
// typedef alias with a complete struct declaration.
// We do not use the struct fields directly, only pointers to these opaque types.

typedef struct whisper_context whisper_context;
struct whisper_context { char _opaque; };

typedef struct whisper_state whisper_state;
struct whisper_state { char _opaque; };

#endif // WHISPER_WRAPPER_H
