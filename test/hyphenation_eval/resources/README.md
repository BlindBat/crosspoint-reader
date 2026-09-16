# Hyphenation evaluation fixtures

Each `<language>_hyphenation_tests.txt` file holds up to 5000 words drawn from one
book, with the hyphenation pyphen produces for that language as ground truth.
`HyphenationEvaluationTest` scores the firmware's Liang hyphenator against them and
fails when accuracy drops below the thresholds pinned in the suite.

**The committed files are the fixtures.** They are inputs to the test, not build
artifacts: nothing regenerates them during a build or a CI run, and a regeneration
would change the scores, so only regenerate when adding a language or when
deliberately re-baselining one.

## What each file was generated from

| File | Source book | pyphen language | Min prefix / suffix |
|------|-------------|-----------------|---------------------|
| `english_hyphenation_tests.txt` | where_the_axe_is_buried.epub | `en_US` | 3 / 3 |
| `french_hyphenation_tests.txt` | le_rouge_et_le_noir.epub | `fr_FR` | 2 / 2 |
| `german_hyphenation_tests.txt` | also_sprach_zarathustra.epub | `de_DE` | 2 / 2 |
| `italian_hyphenation_tests.txt` | manzoni_i_promessi_sposi.epub | `it_IT` | 2 / 2 |
| `polish_hyphenation_tests.txt` | Władysław Stanisław Reymont, Chlopi.epub | `pl_PL` | 2 / 2 |
| `russian_hyphenation_tests.txt` | bog_kak_illyuziya.epub | `ru_RU` | 2 / 2 |
| `spanish_hyphenation_tests.txt` | quijote.epub | `es_ES` | 2 / 2 |
| `swedish_hyphenation_tests.txt` | Uppdrag Hail Mary - Andy Weir.epub | `sv_SE` | 2 / 2 |

The header of every file records its own source book, language and prefix/suffix
settings, so a fixture always carries its provenance. The books themselves are NOT
in this repository: they are ordinary EPUBs that happened to be on hand, most of
them copyrighted, and the generator accepts any EPUB or plain text file in the
target language. Reproducing a file byte-for-byte therefore needs the same book;
regenerating from a different book of the same language is fine, but it re-baselines
that language's scores and the thresholds in `HyphenationEvaluationTest.cpp` must be
reviewed in the same change.

## External requirements for regeneration

- Python 3 and `pyphen` (`pip install pyphen`), which supplies the hyphenation
  dictionaries. pyphen bundles the Hunspell/LibreOffice dictionaries, so its version
  determines the ground truth: record the version you used in the commit message.
- A source book in the target language (EPUB or plain text; the script strips tags
  when given an EPUB).

```bash
python test/hyphenation_eval/resources/generate_hyphenation_test_data.py \
  <book.epub> test/hyphenation_eval/resources/<language>_hyphenation_tests.txt \
  --language <xx_XX> --max-words 5000 --min-prefix 2 --min-suffix 2
```

A new language also needs its pattern table registered in
`lib/Epub/Epub/hyphenation/LanguageRegistry.cpp` and a threshold entry in the suite.
