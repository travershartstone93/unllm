#!/usr/bin/env python3
"""Tests for the pack builder's text cleaning (tools/unllm_ingest.py).

Every input here is written for the test; no dataset text. Runs with the
standard library only: python3 tests/test_ingest.py
"""

import gzip
import importlib.util
import shutil
import subprocess
import json
import os
import sqlite3
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("unllm_ingest", os.path.join(HERE, "..", "tools", "unllm_ingest.py"))
ingest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ingest)


class Maths(unittest.TestCase):
    def test_formulas_read_as_text(self):
        t = ingest.latex_to_text
        self.assertEqual(t("E=mc^{2}"), "E=mc²")
        self.assertEqual(t(r"\frac{1}{2}mv^2"), "(1/2)mv²")
        self.assertEqual(t(r"\frac{RT}{nF}\ln Q"), "(RT/nF)ln Q")
        self.assertEqual(t(r"\sqrt{2}"), "√2")
        self.assertEqual(t(r"\sqrt{x+1}"), "√(x+1)")
        self.assertEqual(t(r"\ce{CO2}"), "CO₂")
        self.assertEqual(t(r"\alpha + \beta \leq \pi"), "α + β ≤ π")
        self.assertEqual(t(r"x_{i} + y_{max}"), "xᵢ + yₘₐₓ")
        self.assertEqual(t(r"\left( x \right)"), "(x)")
        self.assertEqual(t(r"\mathbb{R}^n"), "ℝⁿ")
        self.assertEqual(t("a \\"), "a")  # a backslash ending the formula

    def test_wikipedia_formulas(self):
        self.assertEqual(ingest.detex_display(r"His equation {\displaystyle E=mc^{2}} is famous."),
                         "His equation E=mc² is famous.")

    def test_dollar_maths_and_prices(self):
        self.assertEqual(ingest.detex_dollars(r"It costs \$5 and $x^2$ grows."), "It costs $5 and x² grows.")
        self.assertEqual(ingest.detex_dollars("$$\\frac{a}{b}$$"), "(a/b)")


class StackExchangeHtml(unittest.TestCase):
    def test_code_keeps_its_lines(self):
        body = ("<p>Run:</p><pre><code>sudo apt update\nsudo apt upgrade\n</code></pre>"
                "<p>Good luck!</p>")
        blocks = ingest.tidy_blocks(ingest.html_blocks(body))
        self.assertEqual(blocks, [("code", "Run:\nsudo apt update\nsudo apt upgrade")])

    def test_scraps_join_their_neighbours(self):
        body = ("<p>Heat the pan until it smokes, then sear the steak.</p><p>Works well.</p>"
                "<p>Source:</p><p>My grandmother, who cooked for fifty years.</p><p>edit</p>")
        blocks = ingest.tidy_blocks(ingest.html_blocks(body))
        self.assertEqual(blocks, [("text", "Heat the pan until it smokes, then sear the steak. Works well."),
                                  ("text", "Source: My grandmother, who cooked for fifty years.")])

    def test_short_items_make_one_list_steps_stay_apart(self):
        body = ("<p>You need:</p><ul><li>2 Tbsp oregano</li><li>1 tsp salt</li></ul>"
                "<p>Mix and rest for an hour before grilling.</p>"
                "<ol><li>Open a terminal window and type the first command below.</li>"
                "<li>Reboot the machine when the update has finished.</li></ol>")
        self.assertEqual(ingest.tidy_blocks(ingest.html_blocks(body)), [
            ("text", "You need: 2 Tbsp oregano; 1 tsp salt."),
            ("text", "Mix and rest for an hour before grilling."),
            ("text", "Open a terminal window and type the first command below."),
            ("text", "Reboot the machine when the update has finished.")])

    def test_heading_labels_the_paragraph_after_it(self):
        body = ("<p>The old way works on older releases of the system, see below.</p><h2>Ubuntu 18.04+</h2>"
                "<p>Use the systemd service instead of the old script.</p><h2>Notes</h2>")
        self.assertEqual(ingest.tidy_blocks(ingest.html_blocks(body)), [
            ("text", "The old way works on older releases of the system, see below."),
            ("text", "Ubuntu 18.04+: Use the systemd service instead of the old script.")])

    def test_introduction_stays_with_its_command_or_list(self):
        body = ("<p>Open a terminal window and type this:</p><pre><code>cat /proc/version\n</code></pre>"
                "<p>You can try one of these drivers:</p><ul><li>nouveau</li><li>nvidia-535</li></ul>")
        self.assertEqual(ingest.tidy_blocks(ingest.html_blocks(body)), [
            ("code", "Open a terminal window and type this:\ncat /proc/version"),
            ("text", "You can try one of these drivers: nouveau; nvidia-535.")])

    def test_maths_only_where_asked(self):
        body = "<p>The energy is $\\frac{1}{2}mv^2$ for a moving body.</p>"
        self.assertEqual(ingest.tidy_blocks(ingest.html_blocks(body), maths=True),
                         [("text", "The energy is (1/2)mv² for a moving body.")])
        shell = "<p>Add it to $PATH and then $HOME/bin is searched too.</p>"
        self.assertEqual(ingest.tidy_blocks(ingest.html_blocks(shell)),
                         [("text", "Add it to $PATH and then $HOME/bin is searched too.")])


ARTICLE = """# Mount Example
Mount Example is a mountain in Testland. It is 1,234 metres (4,049 ft) high.

## History
People first climbed it in 1901, using ropes made of grass.

## Peaks
- North Peak
- South Peak

| Year | Climbers |
| ---- | -------- |
| 1901 | 2 |

### Formula
The slope follows {\\displaystyle h=\\frac{1}{2}gt^{2}} here.

## Other websites
- An example site
- Another example site
"""


class FineWiki(unittest.TestCase):
    def test_article_becomes_clean_passages(self):
        ps = ingest.finewiki_passages(ARTICLE)
        self.assertEqual(ps[0], ("", "lead", "Mount Example is a mountain in Testland. It is 1,234 metres (4,049 ft) high."))
        self.assertIn(("History", "body", "People first climbed it in 1901, using ropes made of grass."), ps)
        self.assertIn(("Peaks", "list", "Peaks: North Peak; South Peak."), ps)
        self.assertIn(("Peaks", "table", "Year, Climbers\n1901, 2"), ps)
        self.assertIn(("Formula", "body", "The slope follows h=(1/2)gt² here."), ps)
        self.assertFalse(any("example site" in t for _, _, t in ps), "link sections are dropped")


class FineWikiLabels(unittest.TestCase):
    def test_bare_label_heads_its_list(self):
        ps = ingest.finewiki_passages("# X\nX is a football club in Testland with a long history.\n\n"
                                      "## Club\nBoardroom\n- Ann Smith\n- Bob Jones\n")
        self.assertIn(("Club", "list", "Boardroom: Ann Smith; Bob Jones."), ps)
        self.assertFalse(any(t == "Boardroom" for _, _, t in ps))

    def test_bare_label_before_prose_is_dropped(self):
        ps = ingest.finewiki_passages("# X\n## Clubs\nVfB Stuttgart\nHe played there for three seasons and scored often.\n")
        self.assertEqual(ps, [("Clubs", "lead", "He played there for three seasons and scored often.")])

    def test_lines_without_words_are_dropped(self):
        ps = ingest.finewiki_passages("# X\n## Episodes\n8.\nThe eighth episode was filmed in Paris during the spring.\n12.\n")
        self.assertEqual(ps, [("Episodes", "lead", "The eighth episode was filmed in Paris during the spring.")])

    def test_colon_line_introduces_what_follows(self):
        ps = ingest.finewiki_passages("# X\n## Kinds\nThe main kinds are:\n- Red\n- Blue\n\n"
                                      "The reaction is:\nA + B → C\n")
        self.assertIn(("Kinds", "list", "The main kinds are: Red; Blue."), ps)
        self.assertIn(("Kinds", "lead", "The reaction is: A + B → C"), ps)

    def test_nested_items_stay_with_their_parent(self):
        ps = ingest.finewiki_passages("# X\n## Deaths\n- July 2:\n  - Ann Smith\n  - Bob Jones\n- July 5: Carl Doe\n")
        self.assertIn(("Deaths", "list", "Deaths: July 2: Ann Smith, Bob Jones; July 5: Carl Doe."), ps)


class Wiktionary(unittest.TestCase):
    def test_word_forms_are_marked_with_their_base_word(self):
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, "kaikki.jsonl")
            with open(src, "w") as f:
                f.write(json.dumps({"word": "abridgement", "pos": "noun", "lang_code": "en", "senses": [
                    {"glosses": ["Alternative form of abridgment."], "tags": ["alt-of", "alternative"],
                     "alt_of": [{"word": "abridgment"}]}]}) + "\n")
                f.write(json.dumps({"word": "abridgment", "pos": "noun", "lang_code": "en", "senses": [
                    {"glosses": ["A shortened version of a written work."]}]}) + "\n")
            shard = ingest.Shard(os.path.join(d, "s.sqlite"), {"name": "t"})
            self.assertEqual(ingest.read_wiktionary(src, shard), 2)
            rows = shard.db.execute("SELECT d.title, p.role, p.text, d.extra FROM passages p "
                                    "JOIN docs d USING(doc_id) ORDER BY d.doc_id").fetchall()
            shard.db.close()
        self.assertEqual(rows[0][:3], ("abridgement", "form", "abridgement (noun): Alternative form of abridgment."))
        self.assertEqual(json.loads(rows[0][3])["form_of"], "abridgment")
        self.assertEqual(rows[1][:2], ("abridgment", "definition"))


class Poetry(unittest.TestCase):
    def test_titles_and_languages(self):
        english = ["The sea rolls in beneath the moon,", "The gulls go quiet and the tide comes soon,",
                   "And every wave that finds the sand", "Carries a word from a far-off land.",
                   "O thou who walkest by the shore,", "Hear what the waters tell once more:",
                   "That all the world is in the deep,", "And all the deep is in our sleep."]
        spanish = ["Mas si osare un extraño enemigo", "profanar con su planta tu suelo",
                   "piensa oh patria querida que el cielo", "un soldado en cada hijo te dio,",
                   "Que la patria os contempla orgullosa", "No temais una muerte gloriosa,",
                   "Que morir por la patria es vivir.", "Que morir por la patria es vivir."]
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "pg_catalog.csv"), "w", newline="") as f:
                f.write('Text#,Type,Issued,Title,Language,Authors,Subjects,LoCC,Bookshelves\n'
                        '1,Text,2000-01-01,"Songs of the Shore\r\nAnd Other Verses",en,"Poet, A., 1800-1870",,,\n')
            src = os.path.join(d, "gutenberg-poetry-v001.ndjson.gz")
            with gzip.open(src, "wt") as f:
                for line in english + spanish:
                    f.write(json.dumps({"s": line, "gid": "1"}) + "\n")
            shard = ingest.Shard(os.path.join(d, "s.sqlite"), {"name": "t"})
            self.assertEqual(ingest.read_poetry(src, shard), 1)
            title = shard.db.execute("SELECT title, author FROM docs").fetchone()
            verses = [t for (t,) in shard.db.execute("SELECT text FROM passages ORDER BY pid")]
            shard.db.close()
        self.assertEqual(title, ("Songs of the Shore", "Poet, A."))
        self.assertEqual(verses, ["\n".join(english)])


class Progress(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.saved = (ingest.PACKS, ingest.PROGRESS, ingest.QUEUE, ingest.LOG)
        ingest.PACKS = os.path.join(self.tmp.name, "packs")
        ingest.PROGRESS = os.path.join(self.tmp.name, "progress")
        ingest.QUEUE = os.path.join(ingest.PROGRESS, "queue.json")
        ingest.LOG = os.path.join(self.tmp.name, "ingest.log")

    def tearDown(self):
        ingest.PACKS, ingest.PROGRESS, ingest.QUEUE, ingest.LOG = self.saved
        self.tmp.cleanup()

    def test_phases_map_onto_one_bar(self):
        t = ingest.Ticker("demo", 0, 1)
        t.tick(0.5, "half read", force=True)
        self.assertEqual(ingest.load_json(t.path)["fraction"], 0.425)
        t.tick(0, "indexing", phase="indexing", force=True)
        self.assertEqual(ingest.load_json(t.path)["fraction"], 0.85)
        t.done()
        self.assertFalse(os.path.exists(t.path))

    def test_states_and_the_overall_bar(self):
        os.makedirs(os.path.join(ingest.PACKS, "alpha"))
        ingest.write_manifest(os.path.join(ingest.PACKS, "alpha"),
                              {"version": "1", "ingest_version": ingest.INGEST_VERSION, "built": "2026-10-16T13:00:00"})
        ingest.save_json(os.path.join(ingest.PROGRESS, "beta.json"),
                         {"pack": "beta", "shards": 2, "shards_done": 1, "phase": "building", "pid": os.getpid(),
                          "started": 0})
        ingest.save_json(os.path.join(ingest.PROGRESS, "beta.shard-001.json"),
                         {"pack": "beta", "shard": 1, "fraction": 0.5, "detail": "100 of 200 articles", "pid": os.getpid()})
        items = [{"name": "alpha", "version": "1", "bytes": 100}, {"name": "beta", "version": "1", "bytes": 200},
                 {"name": "gamma", "version": "1", "bytes": 700}]
        states, overall, left = ingest.collect(items)
        self.assertEqual([s["state"] for s in states], ["done", "building", "waiting"])
        self.assertEqual(states[1]["fraction"], 0.75)
        self.assertIn("1 of 2 parts done; part 2: 100 of 200 articles", states[1]["detail"])
        self.assertAlmostEqual(overall, 0.25)
        text = ingest.render(items, states, overall, left, width=120)
        self.assertIn("NOW     beta", text)
        self.assertIn(" 25%  1 of 3 tasks done", text)


POSTS = """<?xml version="1.0" encoding="utf-8"?>
<posts>
  <row Id="1" PostTypeId="1" AcceptedAnswerId="3" CreationDate="2020-01-01T00:00:00.000" Score="5" Body="&lt;p&gt;The software center is broken.&lt;/p&gt;" OwnerUserId="10" Title="How do I update my packages?" Tags="&lt;apt&gt;&lt;upgrade&gt;" ContentLicense="CC BY-SA 4.0" />
  <row Id="2" PostTypeId="2" ParentId="1" CreationDate="2020-01-02T00:00:00.000" Score="9" Body="&lt;p&gt;Use the graphical updater from the menu.&lt;/p&gt;" OwnerUserId="11" ContentLicense="CC BY-SA 4.0" />
  <row Id="3" PostTypeId="2" ParentId="1" CreationDate="2020-01-03T00:00:00.000" Score="4" Body="&lt;h2&gt;Ubuntu 18.04+&lt;/h2&gt;&lt;p&gt;Open a terminal and type this:&lt;/p&gt;&lt;pre&gt;&lt;code&gt;sudo apt update&#xA;sudo apt upgrade&#xA;&lt;/code&gt;&lt;/pre&gt;&lt;p&gt;Reboot when the upgrade has finished.&lt;/p&gt;" OwnerUserId="12" ContentLicense="CC BY-SA 4.0" />
  <row Id="4" PostTypeId="1" CreationDate="2020-01-04T00:00:00.000" Score="0" Body="&lt;p&gt;Nobody liked this one.&lt;/p&gt;" OwnerUserId="10" Title="An unloved question" ContentLicense="CC BY-SA 4.0" />
</posts>
"""
USERS = """<?xml version="1.0" encoding="utf-8"?>
<users>
  <row Id="10" DisplayName="Sam Asker" />
  <row Id="12" DisplayName="Pat Admin" />
</users>
"""


class StackExchangeArchive(unittest.TestCase):
    def test_a_site_dump_becomes_clean_threads(self):
        if not shutil.which("7z"):
            self.skipTest("7z is not installed")
        with tempfile.TemporaryDirectory() as d:
            for name, text in (("Posts.xml", POSTS), ("Users.xml", USERS)):
                with open(os.path.join(d, name), "w") as f:
                    f.write(text)
            archive = os.path.join(d, "askubuntu.com.7z")
            subprocess.run(["7z", "a", archive, "Posts.xml", "Users.xml"], cwd=d, capture_output=True, check=True)
            shard = ingest.Shard(os.path.join(d, "s.sqlite"), {"name": "t"})
            self.assertEqual(ingest.read_stackexchange(archive, shard, "askubuntu.com"), 1)
            doc = shard.db.execute("SELECT title, url, author, license, tags, extra FROM docs").fetchone()
            ps = shard.db.execute("SELECT ord, role, text FROM passages ORDER BY ord").fetchall()
            shard.db.close()
        self.assertEqual(doc[:5], ("How do I update my packages?", "https://askubuntu.com/q/1", "Pat Admin",
                                   "CC BY-SA 4.0", "apt upgrade"))
        self.assertEqual(json.loads(doc[5]), {"asker": "Sam Asker", "accepted": True})
        self.assertEqual(ps, [
            (0, "question", "How do I update my packages?. The software center is broken."),
            (1, "code", "Ubuntu 18.04+: Open a terminal and type this:\nsudo apt update\nsudo apt upgrade"),
            (2, "accepted", "Reboot when the upgrade has finished.")])


class Checks(unittest.TestCase):
    def test_damaged_text_is_measured(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "shard-000.sqlite")
            db = sqlite3.connect(path)
            db.executescript(ingest.SCHEMA)
            rows = ["It is about  high and very cold."] * 10 + ["A clean sentence about mountains."] * 90
            rows += ["print(foo())"] * 20  # code is never counted
            for i, t in enumerate(rows, 1):
                db.execute("INSERT INTO passages VALUES(?,?,?,?,?,?)",
                           (i, 1, i, "", "code" if t.startswith("print") else "body", t))
            db.commit()
            db.close()
            n, rates, examples = ingest.quality([path], sample=1000)
            self.assertEqual(n, 100)
            self.assertEqual(rates["number holes"], 10.0)
            self.assertEqual(rates["empty brackets"], 0.0)
            self.assertGreater(rates["number holes"], ingest.LIMITS["number holes"])


if __name__ == "__main__":
    unittest.main()
