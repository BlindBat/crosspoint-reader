#!/usr/bin/env python3
"""Regenerate the hand-written FB2 fixture catalog in test/fb2/.

Every fixture is deterministic. Run from the repo root:

    python3 scripts/generate_fb2_test_fixtures.py

The base64 payloads and the windows-1251 fixture are the reason this script
exists: their exact bytes are asserted by the fb2 test suites, so they must be
reproducible rather than hand-edited.
"""

import base64
import os

OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "test", "fb2")

FB2_OPEN = (
    '<?xml version="1.0" encoding="utf-8"?>\n'
    '<FictionBook xmlns="http://www.gribuser.ru/xml/fictionbook/2.0"'
    ' xmlns:l="http://www.w3.org/1999/xlink">\n'
)
FB2_CLOSE = "</FictionBook>\n"

BASIC_COVER_PAYLOAD = b"STUBJPEG:basic-cover-payload-0123456789"
WRAPPED_COVER_PAYLOAD = b"STUBPNG:wrapped-cover-payload-ABCDEFGHIJKLMNOPQRSTUVWXYZ"
DECOY_PAYLOAD = b"DECOY-BINARY-MUST-NOT-BE-EXTRACTED"
CORRUPT_BASE64_TEXT = "R09PR" + "!" + "ERE" + "@" + "FUQQ=" + "*" + "="  # 'GOODDATA' b64 with junk injected


def write(name, content):
    path = os.path.join(OUT_DIR, name)
    if isinstance(content, str):
        content = content.encode("utf-8")
    with open(path, "wb") as fh:
        fh.write(content)
    print("wrote", os.path.normpath(path), len(content), "bytes")


def wrap_b64(data, width=16, prefix="      "):
    text = base64.b64encode(data).decode()
    lines = [prefix + text[i : i + width] for i in range(0, len(text), width)]
    return "\n".join(lines)


def basic():
    filler = " ".join("filler%03d" % i for i in range(90))
    return (
        FB2_OPEN
        + """  <description>
    <title-info>
      <genre>antique</genre>
      <author>
        <first-name>John</first-name>
        <last-name>Doe</last-name>
      </author>
      <book-title>The Crosspoint Chronicle</book-title>
      <lang>en</lang>
      <coverpage>
        <image l:href="#cover.jpg"/>
      </coverpage>
    </title-info>
  </description>
  <body>
    <section>
      <title><p>Chapter One</p></title>
      <p>Alpha begins the story with a modest paragraph.</p>
      <p>%s</p>
    </section>
    <section>
      <title><p>Chapter Two</p></title>
      <p>Bravo continues where alpha left off.</p>
    </section>
  </body>
  <binary id="cover.jpg" content-type="image/jpeg">%s</binary>
"""
        % (filler, base64.b64encode(BASIC_COVER_PAYLOAD).decode())
        + FB2_CLOSE
    )


def multi_author():
    return (
        FB2_OPEN
        + """  <description>
    <title-info>
      <author>
        <first-name>John</first-name>
        <middle-name>Quincy</middle-name>
        <last-name>Smith</last-name>
      </author>
      <author>
        <first-name>Jane</first-name>
        <last-name>Roe</last-name>
      </author>
      <book-title>Two Hands, One Pen</book-title>
      <lang>en</lang>
      <coverpage>
        <image l:href="cover-nohash.png"/>
      </coverpage>
    </title-info>
  </description>
  <body>
    <section>
      <title><p>Only Chapter</p></title>
      <p>Collaboration paragraph.</p>
    </section>
  </body>
"""
        + FB2_CLOSE
    )


def no_cover():
    return (
        FB2_OPEN
        + """  <description>
    <title-info>
      <book-title>Untitled Fields</book-title>
    </title-info>
  </description>
  <body>
    <section>
      <p>A section without a title element.</p>
    </section>
  </body>
"""
        + FB2_CLOSE
    )


def nested_sections():
    return (
        FB2_OPEN
        + """  <description>
    <title-info>
      <book-title>Nesting Dolls</book-title>
      <lang>en</lang>
    </title-info>
  </description>
  <body>
    <section>
      <title><p>Part One</p><p>The Beginning</p></title>
      <p>Outer opening paragraph.</p>
      <section>
        <title><p>Inner Chapter</p></title>
        <p>Nested innerword paragraph.</p>
      </section>
      <p>Outer closing paragraph.</p>
    </section>
    <section>
      <title><p>Part Two</p></title>
      <p>Second top level paragraph.</p>
    </section>
  </body>
"""
        + FB2_CLOSE
    )


def notes_body():
    return (
        FB2_OPEN
        + """  <description>
    <title-info>
      <book-title>Annotated Tale</book-title>
      <lang>en</lang>
    </title-info>
  </description>
  <body>
    <section>
      <title><p>Story</p></title>
      <p>Main narrative referencing a note.</p>
    </section>
  </body>
  <body name="notes">
    <section id="n1">
      <title><p>Note 1</p></title>
      <p>First footnote text.</p>
    </section>
    <section id="n2">
      <title><p>Note 2</p></title>
      <p>Second footnote text.</p>
    </section>
  </body>
"""
        + FB2_CLOSE
    )


def malformed_truncated():
    full = (
        FB2_OPEN
        + """  <description>
    <title-info>
      <book-title>Interrupted</book-title>
    </title-info>
  </description>
  <body>
    <section>
      <title><p>Lost Chapter</p></title>
      <p>This document ends mid-ta"""
    )
    return full  # cut off inside character data, no closing tags


def wrong_root():
    return (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        "<recipe>\n"
        "  <ingredient>flour</ingredient>\n"
        "  <ingredient>water</ingredient>\n"
        "</recipe>\n"
    )


def cp1251_declared():
    body = (
        '<?xml version="1.0" encoding="windows-1251"?>\n'
        '<FictionBook xmlns="http://www.gribuser.ru/xml/fictionbook/2.0">\n'
        "  <description>\n"
        "    <title-info>\n"
        "      <book-title>{title}</book-title>\n"
        "      <lang>ru</lang>\n"
        "    </title-info>\n"
        "  </description>\n"
        "  <body>\n"
        "    <section>\n"
        "      <p>{text}</p>\n"
        "    </section>\n"
        "  </body>\n"
        "</FictionBook>\n"
    )
    return body.format(title="Тестовая книга", text="Привет, мир").encode("cp1251")


def base64_corrupt_cover():
    return (
        FB2_OPEN
        + """  <description>
    <title-info>
      <book-title>Damaged Cover</book-title>
      <coverpage>
        <image l:href="#bad.jpg"/>
      </coverpage>
    </title-info>
  </description>
  <body>
    <section>
      <p>Body text.</p>
    </section>
  </body>
  <binary id="bad.jpg" content-type="image/jpeg">%s</binary>
"""
        % CORRUPT_BASE64_TEXT
        + FB2_CLOSE
    )


def unicode_titles():
    return (
        FB2_OPEN
        + """  <description>
    <title-info>
      <author>
        <first-name>Лев</first-name>
        <last-name>Толстой</last-name>
      </author>
      <book-title>Война и мир</book-title>
      <lang>ru</lang>
    </title-info>
  </description>
  <body>
    <section>
      <title><p>Глава первая</p></title>
      <p>Первый абзац русского текста.</p>
    </section>
    <section>
      <title><p>Глава вторая</p></title>
      <p>Второй абзац русского текста.</p>
    </section>
  </body>
"""
        + FB2_CLOSE
    )


def no_sections():
    return (
        FB2_OPEN
        + """  <description>
    <title-info>
      <book-title>Plain Stream</book-title>
      <lang>en</lang>
    </title-info>
  </description>
  <body>
    <p>Paragraph one without any section wrapper.</p>
    <p>Paragraph two keeps flowing.</p>
  </body>
"""
        + FB2_CLOSE
    )


def cover_wrapped():
    return (
        FB2_OPEN
        + """  <description>
    <title-info>
      <book-title>Wrapped Cover</book-title>
      <coverpage>
        <image l:href="#target.png"/>
      </coverpage>
    </title-info>
  </description>
  <body>
    <section>
      <p>Body text.</p>
    </section>
  </body>
  <binary id="decoy.png" content-type="image/png">%s</binary>
  <binary id="target.png" content-type="image/png">
%s
  </binary>
"""
        % (base64.b64encode(DECOY_PAYLOAD).decode(), wrap_b64(WRAPPED_COVER_PAYLOAD))
        + FB2_CLOSE
    )


def styles():
    return (
        FB2_OPEN
        + """  <description>
    <title-info>
      <book-title>Styled Section</book-title>
      <lang>en</lang>
    </title-info>
  </description>
  <body>
    <section>
      <title><p>Styles</p></title>
      <epigraph><p>epigraphword</p></epigraph>
      <p>plainword <emphasis>italicword</emphasis> <strong>boldword</strong></p>
      <p><strong><emphasis>bolditalicword</emphasis></strong></p>
      <subtitle>subtitleword</subtitle>
      <empty-line/>
      <poem>
        <stanza>
          <v>verseline one</v>
          <v>verseline two</v>
        </stanza>
      </poem>
      <cite><p>citeword</p></cite>
      <unknown-tag><p>unknownword</p></unknown-tag>
      <image l:href="#pic.png"/>
      <table><tr><td>cellword</td></tr></table>
      <p>closingword</p>
    </section>
    <section>
      <title><p>Decoy</p></title>
      <p>decoyword must stay out of section zero.</p>
    </section>
  </body>
"""
        + FB2_CLOSE
    )


def long_book():
    paragraphs = "\n".join(
        "      <p>pageword%03d fills line number %d for pagination.</p>" % (i, i) for i in range(60)
    )
    return (
        FB2_OPEN
        + """  <description>
    <title-info>
      <book-title>Long Scroll</book-title>
      <lang>en</lang>
    </title-info>
  </description>
  <body>
    <section>
      <title><p>Only Chapter</p></title>
%s
    </section>
  </body>
"""
        % paragraphs
        + FB2_CLOSE
    )


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    write("basic.fb2", basic())
    write("multi-author.fb2", multi_author())
    write("no-cover.fb2", no_cover())
    write("nested-sections.fb2", nested_sections())
    write("notes-body.fb2", notes_body())
    write("malformed-truncated.fb2", malformed_truncated())
    write("wrong-root.fb2", wrong_root())
    write("cp1251-declared.fb2", cp1251_declared())
    write("base64-corrupt-cover.fb2", base64_corrupt_cover())
    write("unicode-titles.fb2", unicode_titles())
    write("no-sections.fb2", no_sections())
    write("cover-wrapped.fb2", cover_wrapped())
    write("styles.fb2", styles())
    write("long.fb2", long_book())


if __name__ == "__main__":
    main()
