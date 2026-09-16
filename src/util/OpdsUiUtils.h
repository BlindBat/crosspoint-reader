#pragma once

#include <OpdsParser.h>

#include <string>
#include <vector>

// Pure helpers behind the OPDS settings list and catalog browser: folder
// normalisation, OpenSearch query substitution and pagination rows. No UI or
// storage dependencies so they compile in the host test program.
namespace opds_ui {

// Normalizes a user-typed download folder: trims spaces/tabs, "" (or a bare
// "/") => SD root, otherwise a single leading '/' and no trailing '/'.
std::string normalizeDownloadFolder(std::string v);

// Percent-encodes a search query byte-wise; alnum and "-_.~" pass through.
std::string percentEncodeQuery(const std::string& query);

// Substitutes the encoded query for the first "{searchTerms}" in the OpenSearch
// template. A template without the placeholder is returned unchanged.
std::string buildSearchUrl(const std::string& searchTemplate, const std::string& query);

// Adds the "« Previous Page" row at the front and the "Next Page »" row at the
// back for the non-empty pagination URLs, reserving once for both.
void addPaginationRows(std::vector<OpdsEntry>& entries, const std::string& prevUrl, const std::string& nextUrl,
                       const char* prevLabel, const char* nextLabel);

}  // namespace opds_ui
