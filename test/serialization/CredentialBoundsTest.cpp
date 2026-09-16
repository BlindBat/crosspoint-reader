// PersistableStoreBase::extractPassword, both overloads.
//
// opds.json / wifi.json / kosync.json are SD-card JSON the user can edit, so the
// bounded overload must reject an over-long credential (obfuscated or legacy
// plaintext) instead of handing a derived store a string that will not fit its
// fixed char[] field (FR-161).

#include <ObfuscationUtils.h>
#include <PersistableStore.h>
#include <gtest/gtest.h>

#include <limits>
#include <string>

namespace {

// extractPassword is protected; a derived type is the sanctioned way in.
struct PasswordProbe : PersistableStoreBase {
  using PersistableStoreBase::extractPassword;
};

// Values are assigned as std::string so ArduinoJson copies them into the
// document instead of linking a pointer into a dead temporary.
JsonDocument obfuscatedDoc(const std::string& plaintext) {
  JsonDocument doc;
  const String encoded = obfuscation::obfuscateToBase64(plaintext);
  doc["password_obf"] = std::string(encoded.c_str());
  return doc;
}

JsonDocument legacyDoc(const std::string& plaintext) {
  JsonDocument doc;
  doc["password"] = plaintext;
  return doc;
}

std::string extract(const JsonDocument& doc, bool& needsResave, const size_t maxLength, bool& valid) {
  return PasswordProbe::extractPassword(doc.as<JsonVariantConst>(), needsResave, maxLength, valid);
}

TEST(ExtractPasswordTest, DecodesAnObfuscatedPasswordWithoutRequestingAResave) {
  const JsonDocument doc = obfuscatedDoc("hunter2");
  bool needsResave = false;
  bool valid = false;
  EXPECT_EQ(extract(doc, needsResave, 63, valid), "hunter2");
  EXPECT_TRUE(valid);
  EXPECT_FALSE(needsResave);
}

TEST(ExtractPasswordTest, RoundTripsBytesThatAreNotPrintableAscii) {
  const std::string secret = "p\xC3\xA4ss w\xE2\x82\xAC rd!";
  const JsonDocument doc = obfuscatedDoc(secret);
  bool needsResave = false;
  bool valid = false;
  EXPECT_EQ(extract(doc, needsResave, 63, valid), secret);
  EXPECT_TRUE(valid);
}

TEST(ExtractPasswordTest, AcceptsAPasswordExactlyAtTheLimit) {
  const std::string secret(63, 'z');
  const JsonDocument doc = obfuscatedDoc(secret);
  bool needsResave = false;
  bool valid = false;
  EXPECT_EQ(extract(doc, needsResave, 63, valid), secret);
  EXPECT_TRUE(valid);
}

TEST(ExtractPasswordTest, RejectsAnObfuscatedPasswordOneByteOverTheLimit) {
  const std::string secret(64, 'z');
  const JsonDocument doc = obfuscatedDoc(secret);
  bool needsResave = false;
  bool valid = true;
  EXPECT_EQ(extract(doc, needsResave, 63, valid), "");
  EXPECT_FALSE(valid);
  EXPECT_FALSE(needsResave);
}

TEST(ExtractPasswordTest, RejectsAWildlyOversizedObfuscatedPassword) {
  const JsonDocument doc = obfuscatedDoc(std::string(4096, 'x'));
  bool needsResave = false;
  bool valid = true;
  EXPECT_EQ(extract(doc, needsResave, 63, valid), "");
  EXPECT_FALSE(valid);
}

TEST(ExtractPasswordTest, LegacyPlaintextIsAcceptedAndRequestsAResave) {
  const JsonDocument doc = legacyDoc("plaintext-pass");
  bool needsResave = false;
  bool valid = false;
  EXPECT_EQ(extract(doc, needsResave, 63, valid), "plaintext-pass");
  EXPECT_TRUE(valid);
  EXPECT_TRUE(needsResave);
}

TEST(ExtractPasswordTest, LegacyPlaintextOverTheLimitIsRejected) {
  const JsonDocument doc = legacyDoc(std::string(200, 'p'));
  bool needsResave = false;
  bool valid = true;
  EXPECT_EQ(extract(doc, needsResave, 63, valid), "");
  EXPECT_FALSE(valid);
  EXPECT_FALSE(needsResave);
}

TEST(ExtractPasswordTest, LegacyPlaintextExactlyAtTheLimitIsAccepted) {
  const std::string secret(63, 'p');
  const JsonDocument doc = legacyDoc(secret);
  bool needsResave = false;
  bool valid = false;
  EXPECT_EQ(extract(doc, needsResave, 63, valid), secret);
  EXPECT_TRUE(valid);
  EXPECT_TRUE(needsResave);
}

TEST(ExtractPasswordTest, MissingBothFieldsYieldsAnEmptyPasswordWithoutAResave) {
  JsonDocument doc;
  doc["name"] = "Catalog";
  bool needsResave = false;
  bool valid = false;
  EXPECT_EQ(extract(doc, needsResave, 63, valid), "");
  EXPECT_TRUE(valid);
  EXPECT_FALSE(needsResave);  // an absent password is not a legacy upgrade
}

TEST(ExtractPasswordTest, EmptyStringsInBothFieldsYieldAnEmptyPassword) {
  JsonDocument doc;
  doc["password_obf"] = "";
  doc["password"] = "";
  bool needsResave = false;
  bool valid = false;
  EXPECT_EQ(extract(doc, needsResave, 63, valid), "");
  EXPECT_TRUE(valid);
  EXPECT_FALSE(needsResave);
}

TEST(ExtractPasswordTest, CorruptBase64FallsBackToTheLegacyPlaintextField) {
  JsonDocument doc;
  doc["password_obf"] = "!!!not base64!!!";
  doc["password"] = "fallback";
  bool needsResave = false;
  bool valid = false;
  EXPECT_EQ(extract(doc, needsResave, 63, valid), "fallback");
  EXPECT_TRUE(valid);
  EXPECT_TRUE(needsResave);
}

TEST(ExtractPasswordTest, CorruptBase64WithNoLegacyFieldYieldsEmpty) {
  JsonDocument doc;
  doc["password_obf"] = "????";
  bool needsResave = false;
  bool valid = false;
  EXPECT_EQ(extract(doc, needsResave, 63, valid), "");
  EXPECT_TRUE(valid);
  EXPECT_FALSE(needsResave);
}

TEST(ExtractPasswordTest, TruncatedBase64FallsBackInsteadOfDecodingGarbage) {
  std::string encoded = obfuscation::obfuscateToBase64("hunter2").c_str();
  ASSERT_GT(encoded.size(), 2u);
  encoded.pop_back();  // breaks the final 4-character group

  JsonDocument doc;
  doc["password_obf"] = encoded;
  doc["password"] = "legacy";
  bool needsResave = false;
  bool valid = false;
  EXPECT_EQ(extract(doc, needsResave, 63, valid), "legacy");
  EXPECT_TRUE(valid);
  EXPECT_TRUE(needsResave);
}

TEST(ExtractPasswordTest, WrongTypedFieldsAreTreatedAsAbsent) {
  JsonDocument doc;
  doc["password_obf"] = 42;
  doc["password"] = true;
  bool needsResave = false;
  bool valid = false;
  EXPECT_EQ(extract(doc, needsResave, 63, valid), "");
  EXPECT_TRUE(valid);
  EXPECT_FALSE(needsResave);
}

TEST(ExtractPasswordTest, NestedObjectElementIsAccepted) {
  // Callers pass an array element straight out of the opds.json server list.
  JsonDocument doc;
  const JsonArray servers = doc["servers"].to<JsonArray>();
  const JsonObject entry = servers.add<JsonObject>();
  entry["password_obf"] = std::string(obfuscation::obfuscateToBase64("per-server").c_str());

  bool needsResave = false;
  bool valid = false;
  EXPECT_EQ(PasswordProbe::extractPassword(doc["servers"][0].as<JsonVariantConst>(), needsResave, 63, valid),
            "per-server");
  EXPECT_TRUE(valid);
  EXPECT_FALSE(needsResave);
}

TEST(ExtractPasswordTest, UnboundedOverloadAcceptsAnOverlongPassword) {
  const std::string secret(4096, 'y');
  const JsonDocument doc = obfuscatedDoc(secret);
  bool needsResave = false;
  EXPECT_EQ(PasswordProbe::extractPassword(doc.as<JsonVariantConst>(), needsResave), secret);
  EXPECT_FALSE(needsResave);
}

TEST(ExtractPasswordTest, UnboundedOverloadStillUpgradesLegacyPlaintext) {
  const JsonDocument doc = legacyDoc("old-style");
  bool needsResave = false;
  EXPECT_EQ(PasswordProbe::extractPassword(doc.as<JsonVariantConst>(), needsResave), "old-style");
  EXPECT_TRUE(needsResave);
}

TEST(ExtractPasswordTest, MaxSizeLimitBehavesLikeTheUnboundedOverload) {
  const std::string secret(1000, 'w');
  const JsonDocument doc = obfuscatedDoc(secret);
  bool needsResave = false;
  bool valid = false;
  EXPECT_EQ(extract(doc, needsResave, std::numeric_limits<size_t>::max(), valid), secret);
  EXPECT_TRUE(valid);
}

TEST(ExtractPasswordTest, ZeroLimitRejectsAnyNonEmptyPassword) {
  const JsonDocument doc = obfuscatedDoc("a");
  bool needsResave = false;
  bool valid = true;
  EXPECT_EQ(extract(doc, needsResave, 0, valid), "");
  EXPECT_FALSE(valid);
}

TEST(ExtractPasswordTest, ZeroLimitAcceptsAnAbsentPassword) {
  JsonDocument doc;
  doc["name"] = "Catalog";
  bool needsResave = false;
  bool valid = false;
  EXPECT_EQ(extract(doc, needsResave, 0, valid), "");
  EXPECT_TRUE(valid);
}

}  // namespace
