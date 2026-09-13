#!/usr/bin/env python3
"""Regenerate the malformed-input corpus under test/corpus/.

Every file is defined as a literal byte string (or built from deterministic
constants) so a re-run always produces byte-identical output. No randomness,
no timestamps, no environment dependence.

Taxonomy (encoded in filename prefixes, one class per file):
  trunc_        input cut off at a structural boundary
  unterminated_ construct opened but never closed (string/comment/CDATA/block)
  deep_         deeply nested structure (>= 64 levels unless stated)
  huge_         absurdly large token / numeric value
  enc_          encoding pathologies: invalid UTF-8, NUL bytes, BOM variants
  empty_        empty or whitespace-only input
  mismatch_     structurally inconsistent input (wrong closers, dup attrs...)
  release_      (json only) GitHub-release-shaped payloads with defects

Usage: python3 scripts/generate_test_corpus.py
Writes into <repo>/test/corpus/{json,xml,css,html}/ next to this script.
"""

from pathlib import Path

# ---------------------------------------------------------------------------
# JSON — consumed by test/streaming_json_parser and test/release_json_parser
# ---------------------------------------------------------------------------

JSON_FILES = {
    "empty_zero_bytes.json": b"",
    "empty_whitespace.json": b" \t\r\n \n ",
    # Truncations at every structural boundary.
    "trunc_after_lbrace.json": b"{",
    "trunc_after_key.json": b'{"key"',
    "trunc_after_colon.json": b'{"key":',
    "trunc_after_comma.json": b'{"a":1,',
    "trunc_mid_array.json": b'{"items":[1,2',
    "trunc_mid_literal.json": b'{"flag":tru',
    "trunc_mid_number.json": b'{"n":12',
    "unterminated_string.json": b'{"key":"value never ends',
    "unterminated_string_escape.json": b'{"key":"ends with backslash\\',
    # Nesting: the streaming parser caps at 32 levels.
    "deep_nesting_32_arrays.json": b"[" * 32 + b"1" + b"]" * 32,
    "deep_nesting_64_arrays.json": b"[" * 64 + b"1" + b"]" * 64,
    "deep_nesting_64_objects.json": b'{"a":' * 64 + b"1" + b"}" * 64,
    # Token buffer is 512 bytes; these overflow it.
    "huge_number_600_digits.json": b'{"n":' + b"9" * 600 + b"}",
    "huge_number_exponent.json": b'{"n":1e309}',
    "huge_string_600_chars.json": b'{"s":"' + b"x" * 600 + b'"}',
    # Encoding pathologies.
    "enc_nul_in_string.json": b'{"key":"a\x00b"}',
    "enc_nul_between_tokens.json": b'{\x00"a"\x00:\x00 1}',
    "enc_invalid_utf8_string.json": b'{"key":"\xc3\x28\x80\xc0\x80"}',
    "enc_bom_utf8.json": b'\xef\xbb\xbf{"key":"value"}',
    "enc_bom_utf16le.json": b"\xff\xfe" + '{"a":1}'.encode("utf-16-le"),
    # Structural mismatches.
    "mismatch_extra_closers.json": b'{"a":1}}}]]',
    "mismatch_bracket_types.json": b'{"a":[1,2}}',
    "mismatch_bare_garbage.json": b"garbage!!! not json at all",
    # GitHub-release-shaped payloads for ReleaseJsonParser.
    "release_trunc_mid_assets.json": (
        b'{"tag_name":"v9.9.9","assets":[{"name":"firmware.bin",'
        b'"browser_download_url":"https://example.invalid/fw.bin","size":123'
    ),
    "release_huge_asset_size.json": (
        b'{"tag_name":"v1.0.0","assets":[{"name":"firmware.bin",'
        b'"browser_download_url":"https://example.invalid/fw.bin",'
        b'"size":99999999999999999999999999}]}'
    ),
    "release_wrong_types.json": b'{"tag_name":123,"assets":"not an array"}',
}

# ---------------------------------------------------------------------------
# XML — consumed by test/xml_parser_utils (vendored expat, XML_GE=0)
# ---------------------------------------------------------------------------


def nested_xml(depth: int, closed: bool) -> bytes:
    opens = b"".join(b"<d%d>" % i for i in range(depth))
    closes = b"".join(b"</d%d>" % i for i in reversed(range(depth))) if closed else b""
    return opens + b"x" + closes


XML_FILES = {
    "empty_zero_bytes.xml": b"",
    "empty_whitespace.xml": b" \n\t ",
    "trunc_mid_decl.xml": b'<?xml version="1.0',
    "trunc_mid_open_tag.xml": b"<root><chi",
    "trunc_mid_attr_name.xml": b"<root at",
    "trunc_mid_attr_value.xml": b'<root attr="val',
    "trunc_mid_close_tag.xml": b"<root>x</ro",
    "trunc_mid_entity.xml": b"<r>&am",
    "trunc_no_close.xml": b"<root><child>text</child>",
    "unterminated_comment.xml": b"<root><!-- comment never closes",
    "unterminated_cdata.xml": b"<root><![CDATA[ data never closes",
    "deep_nesting_64_wellformed.xml": nested_xml(64, closed=True),
    "deep_nesting_256_unclosed.xml": nested_xml(256, closed=False),
    "mismatch_tags.xml": b"<a><b></a></b>",
    "mismatch_duplicate_attr.xml": b'<root a="1" a="2"/>',
    "mismatch_two_roots.xml": b"<a/><b/>",
    "mismatch_bare_ampersand.xml": b"<r>fish & chips</r>",
    "mismatch_undefined_entity.xml": b"<r>&nope;</r>",
    "enc_invalid_utf8_text.xml": b"<r>\xc3\x28</r>",
    "enc_nul_in_text.xml": b"<r>a\x00b</r>",
    "enc_bom_utf8.xml": b"\xef\xbb\xbf<r>ok</r>",
    "enc_bom_utf16le.xml": b"\xff\xfe" + "<r>ok</r>".encode("utf-16-le"),
    "huge_attr_value_64k.xml": b'<r a="' + b"x" * 65536 + b'"/>',
    # Three levels of tenfold entity expansion (1000 copies max: bounded, and
    # inert on the device build where XML_GE=0 disables general entities).
    "huge_entity_expansion.xml": (
        b"<!DOCTYPE r [\n"
        b'<!ENTITY a "aaaaaaaaaa">\n'
        b'<!ENTITY b "&a;&a;&a;&a;&a;&a;&a;&a;&a;&a;">\n'
        b'<!ENTITY c "&b;&b;&b;&b;&b;&b;&b;&b;&b;&b;">\n'
        b"]>\n"
        b"<r>&c;</r>"
    ),
}

# ---------------------------------------------------------------------------
# CSS — consumed by test/css_parser
# ---------------------------------------------------------------------------

CSS_FILES = {
    "empty_zero_bytes.css": b"",
    "empty_whitespace.css": b" \n\t\r\n ",
    "trunc_mid_selector.css": b"p, div",
    "trunc_after_lbrace.css": b"p {",
    "trunc_mid_decl.css": b"p { text-align: cen",
    "unterminated_block.css": b"p { text-align: center;",
    "unterminated_comment.css": b"p { text-align: center } /* never closes",
    "unterminated_atrule.css": b"@media screen { p { color: red } ",
    "unterminated_string.css": b'p { content: "abc } .x { font-weight: bold }',
    "deep_nesting_64_blocks.css": b"p " + b"{" * 64 + b"}" * 64,
    "deep_nesting_64_unbalanced.css": b"{" * 64,
    "huge_selector_4k.css": b"." + b"s" * 4096 + b" { font-weight: bold }",
    "huge_decl_value_8k.css": (b"p { font-family: " + b"A" * 8192 + b"; text-align: center }"),
    "huge_numeric_value.css": (b"p { margin-top: 999999999999999999999999999999em; text-indent: 1e4999em }"),
    "enc_nul_in_value.css": b"p { text-align: cen\x00ter }",
    "enc_nul_in_selector.css": b".a\x00b { font-weight: bold }",
    "enc_invalid_utf8.css": b".\xc3\x28\x80 { font-weight: bold }",
    "enc_bom_utf8.css": b"\xef\xbb\xbfp { text-align: center }",
    "mismatch_extra_closers.css": b"}}} p { text-align: center }",
}

# ---------------------------------------------------------------------------
# HTML (XHTML chapters) — consumed by test/chapter_html_slim_parser
# ---------------------------------------------------------------------------


def chapter(body: bytes) -> bytes:
    return b"<html><body>" + body + b"</body></html>"


HTML_FILES = {
    "empty_zero_bytes.html": b"",
    "empty_whitespace.html": b"  \n\t ",
    "trunc_mid_tag.html": b"<html><body><p>Hello</p><di",
    "trunc_mid_attr.html": b'<html><body><p class="x',
    "trunc_after_text.html": b"<html><body><p>Hello world",
    "trunc_trailing_garbage.html": chapter(b"<p>Hi</p>") + b"<p>trailing garbage",
    "unterminated_comment.html": b"<html><body><!-- never closes",
    "unterminated_cdata.html": b"<html><body><![CDATA[ never closes",
    "deep_nesting_64_divs.html": chapter(b"<div>" * 64 + b"<p>deep text</p>" + b"</div>" * 64),
    "mismatch_tags.html": b"<html><body><p>text</div></body></html>",
    "mismatch_duplicate_attr.html": chapter(b'<p class="a" class="b">text</p>'),
    "enc_invalid_utf8.html": chapter(b"<p>\xc3\x28</p>"),
    "enc_nul_in_text.html": chapter(b"<p>a\x00b</p>"),
    "enc_bom_utf8.html": b"\xef\xbb\xbf" + chapter(b"<p>ok</p>"),
    "huge_word_1k.html": chapter(b"<p>" + b"w" * 1024 + b"</p>"),
    "mismatch_undefined_entity.html": chapter(b"<p>a&nbsp;b&bogus;c</p>"),
}

CORPUS = {
    "json": JSON_FILES,
    "xml": XML_FILES,
    "css": CSS_FILES,
    "html": HTML_FILES,
}


def main() -> None:
    root = Path(__file__).resolve().parent.parent / "test" / "corpus"
    total = 0
    for kind, files in CORPUS.items():
        directory = root / kind
        directory.mkdir(parents=True, exist_ok=True)
        for name, payload in sorted(files.items()):
            (directory / name).write_bytes(payload)
            total += 1
        print(f"{kind}: {len(files)} files")
    print(f"wrote {total} corpus files under {root}")


if __name__ == "__main__":
    main()
