#pragma once

// Shared gtest fixture: a scratch directory holding both the EPUB file and the
// cache root Epub derives its cachePath from, with the platform, storage and
// image-converter stubs reset before every test.

#include <Epub.h>
#include <HalStorage.h>
#include <ImageConverterStubs.h>
#include <PlatformHost.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "EpubTestSupport.h"

class EpubFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    imgconv_host::resetAll();
    platform_host::setHeap(0, 0);
    Storage.resetForTest();
    cacheRoot = tmp.at("crosspoint");
    Storage.mkdir(cacheRoot.c_str());
  }

  void TearDown() override {
    imgconv_host::resetAll();
    platform_host::setHeap(0, 0);
    Storage.resetForTest();
  }

  // Writes archive bytes to <tmp>/<name> and returns the absolute path.
  std::string writeEpub(const std::string& name, const std::string& bytes) const {
    const std::string path = tmp.at(name);
    epubtest::writeBytes(path, bytes);
    return path;
  }

  static std::unique_ptr<Epub> make(const std::string& epubPath, const std::string& cacheDir) {
    return std::make_unique<Epub>(epubPath, cacheDir);
  }

  std::unique_ptr<Epub> make(const std::string& epubPath) const { return make(epubPath, cacheRoot); }

  // Canonical well-formed book: EPUB 3 nav + NCX fallback, one stylesheet, a
  // cover image declared through properties="cover-image", three spine items
  // whose uncompressed sizes are 100/200/300 bytes exactly.
  struct StandardBook {
    std::string bytes;
    std::string chapter1;
    std::string chapter2;
    std::string chapter3;
    std::string coverJpg = "JPEG-COVER-BYTES";
    std::string css = "p { text-indent: 1em; }\n";
  };

  static std::string padTo(const std::string& prefix, const size_t total) {
    std::string out = prefix;
    out.resize(total, 'x');
    return out;
  }

  static StandardBook standardBook() {
    StandardBook book;
    book.chapter1 = padTo("<html><body><p>one</p></body></html>", 100);
    book.chapter2 = padTo("<html><body><p>two</p></body></html>", 200);
    book.chapter3 = padTo("<html><body><p>three</p></body></html>", 300);

    epubtest::OpfSpec opf;
    opf.manifest = {
        {"nav", "nav.xhtml", "application/xhtml+xml", "nav"},
        {"ncx", "toc.ncx", "application/x-dtbncx+xml", ""},
        {"css", "style.css", "text/css", ""},
        {"cover-img", "cover.jpg", "image/jpeg", "cover-image"},
        {"c1", "chap1.xhtml", "application/xhtml+xml", ""},
        {"c2", "chap2.xhtml", "application/xhtml+xml", ""},
        {"c3", "chap3.xhtml", "application/xhtml+xml", ""},
    };
    opf.spine = {"c1", "c2", "c3"};

    epubtest::ZipBuilder zip;
    zip.add("mimetype", "application/epub+zip")
        .add("META-INF/container.xml", epubtest::containerXml("OEBPS/content.opf"))
        .add("OEBPS/content.opf", epubtest::opfXml(opf))
        .add("OEBPS/nav.xhtml", epubtest::navXhtml({{"One", "chap1.xhtml"}, {"Three", "chap3.xhtml"}}))
        .add("OEBPS/toc.ncx", epubtest::ncxXml({{"One", "chap1.xhtml"}}))
        .add("OEBPS/style.css", book.css)
        .add("OEBPS/cover.jpg", book.coverJpg)
        .add("OEBPS/chap1.xhtml", book.chapter1)
        .add("OEBPS/chap2.xhtml", book.chapter2)
        .add("OEBPS/chap3.xhtml", book.chapter3);
    book.bytes = zip.build();
    return book;
  }

  epubtest::TempDir tmp;
  std::string cacheRoot;
};
