#pragma once

#include <expat.h>

// Registers an expat unknown-encoding handler that adds the single-byte
// codepages FB2 files commonly declare: windows-1251/cp1251 (the de facto
// standard for Russian FB2s) and windows-1252/cp1252. Without this, expat
// aborts such books with XML_ERROR_UNKNOWN_ENCODING before any content is
// seen. Call right after XML_ParserCreate on every FB2 parser instance.
void fb2RegisterExtraEncodings(XML_Parser parser);
