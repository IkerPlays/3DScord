/* Builds stb_vorbis (public domain, https://github.com/nothings/stb) without the file API: the app decodes from memory. */
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "impl/stb_vorbis.c"
