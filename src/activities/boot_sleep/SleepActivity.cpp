#include "SleepActivity.h"

#include <Epub.h>
#include <Fb2.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <Txt.h>
#include <Xtc.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/Logo120.h"
#include "util/StringUtils.h"

void SleepActivity::onEnter() {
  Activity::onEnter();
  GUI.drawPopup(renderer, "Entering Sleep...");

  // If cover sleep screen toggle is enabled, try cover first before falling back to selected mode
  if (SETTINGS.coverSleepScreen) {
    renderCoverSleepScreen();
    return;
  }

  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::BLANK):
      return renderBlankSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM):
      return renderCustomSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER):
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      return renderCoverSleepScreen();
    default:
      return renderDefaultSleepScreen();
  }
}

void SleepActivity::renderCustomSleepScreen() const {
  // Check if we have a /sleep directory
  auto dir = SdMan.open("/sleep");
  if (dir && dir.isDirectory()) {
    std::vector<std::string> files;
    char name[500];
    // collect all valid BMP files
    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      if (file.isDirectory()) {
        file.close();
        continue;
      }
      file.getName(name, sizeof(name));
      auto filename = std::string(name);
      if (filename[0] == '.') {
        file.close();
        continue;
      }

      if (filename.substr(filename.length() - 4) != ".bmp") {
        Serial.printf("[%lu] [SLP] Skipping non-.bmp file name: %s\n", millis(), name);
        file.close();
        continue;
      }
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() != BmpReaderError::Ok) {
        Serial.printf("[%lu] [SLP] Skipping invalid BMP file: %s\n", millis(), name);
        file.close();
        continue;
      }
      files.emplace_back(filename);
      file.close();
    }
    const auto numFiles = files.size();
    if (numFiles > 0) {
      // Generate a random number between 1 and numFiles
      auto randomFileIndex = random(numFiles);
      // If we picked the same image as last time, reroll
      while (numFiles > 1 && randomFileIndex == APP_STATE.lastSleepImage) {
        randomFileIndex = random(numFiles);
      }
      APP_STATE.lastSleepImage = randomFileIndex;
      APP_STATE.saveToFile();
      const auto filename = "/sleep/" + files[randomFileIndex];
      FsFile file;
      if (SdMan.openFileForRead("SLP", filename, file)) {
        Serial.printf("[%lu] [SLP] Randomly loading: /sleep/%s\n", millis(), files[randomFileIndex].c_str());
        delay(100);
        Bitmap bitmap(file, true);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderBitmapSleepScreen(bitmap);
          dir.close();
          return;
        }
      }
    }
  }
  if (dir) dir.close();

  // Look for sleep.bmp on the root of the sd card to determine if we should
  // render a custom sleep screen instead of the default.
  FsFile file;
  if (SdMan.openFileForRead("SLP", "/sleep.bmp", file)) {
    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      Serial.printf("[%lu] [SLP] Loading: /sleep.bmp\n", millis());
      renderBitmapSleepScreen(bitmap);
      return;
    }
  }

  renderDefaultSleepScreen();
}

void SleepActivity::renderDefaultSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, "CrossPoint", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, "SLEEPING");

  // Make sleep screen dark unless light is selected in settings
  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap) const {
  int x, y;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  float cropX = 0, cropY = 0;

  Serial.printf("[%lu] [SLP] bitmap %d x %d, screen %d x %d\n", millis(), bitmap.getWidth(), bitmap.getHeight(),
                pageWidth, pageHeight);
  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    // image will scale, make sure placement is right
    float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    Serial.printf("[%lu] [SLP] bitmap ratio: %f, screen ratio: %f\n", millis(), ratio, screenRatio);
    if (ratio > screenRatio) {
      // image wider than viewport ratio, scaled down image needs to be centered vertically
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropX = 1.0f - (screenRatio / ratio);
        Serial.printf("[%lu] [SLP] Cropping bitmap x: %f\n", millis(), cropX);
        ratio = (1.0f - cropX) * static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
      }
      x = 0;
      y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
      Serial.printf("[%lu] [SLP] Centering with ratio %f to y=%d\n", millis(), ratio, y);
    } else {
      // image taller than viewport ratio, scaled down image needs to be centered horizontally
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropY = 1.0f - (ratio / screenRatio);
        Serial.printf("[%lu] [SLP] Cropping bitmap y: %f\n", millis(), cropY);
        ratio = static_cast<float>(bitmap.getWidth()) / ((1.0f - cropY) * static_cast<float>(bitmap.getHeight()));
      }
      x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      y = 0;
      Serial.printf("[%lu] [SLP] Centering with ratio %f to x=%d\n", millis(), ratio, x);
    }
  } else {
    // center the image
    x = (pageWidth - bitmap.getWidth()) / 2;
    y = (pageHeight - bitmap.getHeight()) / 2;
  }

  Serial.printf("[%lu] [SLP] drawing to %d x %d\n", millis(), x, y);
  renderer.clearScreen();

  const bool hasGreyscale = bitmap.hasGreyscale() &&
                            SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;

  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);

  if (SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);

  if (hasGreyscale) {
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleLsbBuffers();

    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }
}

void SleepActivity::renderCoverSleepScreen() const {
  // Determine fallback screen when no cover is available
  void (SleepActivity::*renderNoCoverSleepScreen)() const;
  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM):
      renderNoCoverSleepScreen = &SleepActivity::renderCustomSleepScreen;
      break;
    case (CrossPointSettings::SLEEP_SCREEN_MODE::BLANK):
      renderNoCoverSleepScreen = &SleepActivity::renderBlankSleepScreen;
      break;
    default:
      renderNoCoverSleepScreen = &SleepActivity::renderDefaultSleepScreen;
      break;
  }

  if (APP_STATE.openEpubPath.empty()) {
    return (this->*renderNoCoverSleepScreen)();
  }

  // Fast path: try to open the cached cover BMP directly using the deterministic cache path,
  // avoiding the expensive book load entirely.
  const auto& bookPath = APP_STATE.openEpubPath;
  const auto pathHash = std::to_string(std::hash<std::string>{}(bookPath));
  std::string prefix;
  if (StringUtils::checkFileExtension(bookPath, ".epub")) {
    prefix = "epub_";
  } else if (StringUtils::checkFileExtension(bookPath, ".fb2")) {
    prefix = "fb2_";
  } else if (StringUtils::checkFileExtension(bookPath, ".xtc") || StringUtils::checkFileExtension(bookPath, ".xtch")) {
    prefix = "xtc_";
  } else if (StringUtils::checkFileExtension(bookPath, ".txt")) {
    prefix = "txt_";
  } else {
    return (this->*renderNoCoverSleepScreen)();
  }

  const auto cacheDir = "/.crosspoint/" + prefix + pathHash;
  bool cropped = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;

  // EPUB has separate cropped/fit cover paths; other formats use a single cover.bmp
  std::string coverBmpPath;
  if (prefix == "epub_") {
    coverBmpPath = cacheDir + (cropped ? "/cover_crop.bmp" : "/cover.bmp");
  } else {
    coverBmpPath = cacheDir + "/cover.bmp";
  }

  FsFile file;
  if (SdMan.openFileForRead("SLP", coverBmpPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      Serial.printf("[SLP] Rendering cached sleep cover: %s\n", coverBmpPath.c_str());
      renderBitmapSleepScreen(bitmap);
      return;
    }
  }

  // No cached cover — use stored title/author for text stub (no book loading needed)
  if (!APP_STATE.openBookTitle.empty()) {
    Serial.println("[SLP] No cached cover, rendering text stub from stored metadata");
    return renderCoverStubSleepScreen(APP_STATE.openBookTitle, APP_STATE.openBookAuthor);
  }

  // Fallback: title/author not stored yet (first run after upgrade), load book to get metadata
  Serial.println("[SLP] No stored metadata, loading book for cover sleep screen");
  std::string bookTitle;
  std::string bookAuthor;

  if (StringUtils::checkFileExtension(bookPath, ".xtc") || StringUtils::checkFileExtension(bookPath, ".xtch")) {
    Xtc lastXtc(bookPath, "/.crosspoint");
    if (lastXtc.load()) {
      bookTitle = lastXtc.getTitle();
      bookAuthor = lastXtc.getAuthor();
      if (lastXtc.generateCoverBmp()) {
        FsFile f;
        if (SdMan.openFileForRead("SLP", lastXtc.getCoverBmpPath(), f)) {
          Bitmap bmp(f);
          if (bmp.parseHeaders() == BmpReaderError::Ok) {
            renderBitmapSleepScreen(bmp);
            return;
          }
        }
      }
    }
  } else if (StringUtils::checkFileExtension(bookPath, ".fb2")) {
    Fb2 lastFb2(bookPath, "/.crosspoint");
    if (lastFb2.load(true)) {
      bookTitle = lastFb2.getTitle();
      bookAuthor = lastFb2.getAuthor();
      if (lastFb2.generateCoverBmp()) {
        FsFile f;
        if (SdMan.openFileForRead("SLP", lastFb2.getCoverBmpPath(), f)) {
          Bitmap bmp(f);
          if (bmp.parseHeaders() == BmpReaderError::Ok) {
            renderBitmapSleepScreen(bmp);
            return;
          }
        }
      }
    }
  } else if (StringUtils::checkFileExtension(bookPath, ".txt")) {
    Txt lastTxt(bookPath, "/.crosspoint");
    if (lastTxt.load()) {
      bookTitle = lastTxt.getTitle();
      if (lastTxt.generateCoverBmp()) {
        FsFile f;
        if (SdMan.openFileForRead("SLP", lastTxt.getCoverBmpPath(), f)) {
          Bitmap bmp(f);
          if (bmp.parseHeaders() == BmpReaderError::Ok) {
            renderBitmapSleepScreen(bmp);
            return;
          }
        }
      }
    }
  } else if (StringUtils::checkFileExtension(bookPath, ".epub")) {
    Epub lastEpub(bookPath, "/.crosspoint");
    if (lastEpub.load(true, true)) {
      bookTitle = lastEpub.getTitle();
      bookAuthor = lastEpub.getAuthor();
      if (lastEpub.generateCoverBmp(cropped)) {
        FsFile f;
        if (SdMan.openFileForRead("SLP", lastEpub.getCoverBmpPath(cropped), f)) {
          Bitmap bmp(f);
          if (bmp.parseHeaders() == BmpReaderError::Ok) {
            renderBitmapSleepScreen(bmp);
            return;
          }
        }
      }
    }
  }

  if (!bookTitle.empty()) {
    return renderCoverStubSleepScreen(bookTitle, bookAuthor);
  }

  return (this->*renderNoCoverSleepScreen)();
}

void SleepActivity::renderCoverStubSleepScreen(const std::string& title, const std::string& author) const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  // Draw double-line border frame
  constexpr int outerMargin = 30;
  constexpr int innerMargin = 36;
  renderer.drawRect(outerMargin, outerMargin, pageWidth - outerMargin * 2, pageHeight - outerMargin * 2);
  renderer.drawRect(innerMargin, innerMargin, pageWidth - innerMargin * 2, pageHeight - innerMargin * 2);

  // Calculate text area
  const int textMargin = innerMargin + 20;
  const int maxTextWidth = pageWidth - textMargin * 2;

  // Draw title centered
  const int titleY = pageHeight / 3;
  const auto truncTitle = renderer.truncatedText(UI_12_FONT_ID, title.c_str(), maxTextWidth, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_12_FONT_ID, titleY, truncTitle.c_str(), true, EpdFontFamily::BOLD);

  // Draw author below title
  if (!author.empty()) {
    const int authorY = titleY + renderer.getLineHeight(UI_12_FONT_ID) + 12;
    const auto truncAuthor = renderer.truncatedText(SMALL_FONT_ID, author.c_str(), maxTextWidth);
    renderer.drawCenteredText(SMALL_FONT_ID, authorY, truncAuthor.c_str());
  }

  // Apply cover filter
  if (SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderBlankSleepScreen() const {
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
