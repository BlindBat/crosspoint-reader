#pragma once

// Test hook exported by SectionLinkStubs.cpp: when true, ImageBlock::deserialize
// returns nullptr, standing in for the production nothrow allocation failing
// under memory pressure.
extern bool gImageBlockDeserializeFails;
