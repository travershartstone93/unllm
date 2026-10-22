# Clean question-answering exam

400 plain factual questions for measuring how often unllm answers correctly
from its local knowledge packs. Every question has one stable answer, and that
answer was found in the packs before the question was kept.

## Files

- `dev.tsv`: 200 questions (ids c001 to c200). The developer may read these.
- `sealed.tsv`: 200 questions (ids s001 to s200). Held out.

## The sealed rule

Never read `sealed.tsv` while developing. Do not open it, print it, grep it,
or look at individual failures from it. Run it only to get totals (how many
right out of 200), and report only those totals. If the sealed score is far
below the dev score, the system has been tuned to the dev questions.

## Columns

Tab separated, with a header line.

| column | meaning |
| --- | --- |
| id | c001..c200 in dev, s001..s200 in sealed |
| question | the question as a person would type it: mostly lowercase, often no question mark, reworded so it does not copy the article's sentence |
| answer_pattern | a Python regular expression, used case-insensitively with `re.search`, that matches every acceptable form of the answer as whole words |
| answer_type | person, place, date, year, number, organization, title or description |
| source_title | the Wikipedia article that holds the answer. Almost all were verified in the English Wikipedia pack; a handful were verified in the Simple Wikipedia article of the same title |

Pattern shapes:

- Most answers: `\b(?:Gabriel Garc[ií]a M[aá]rquez|Garc[ií]a M[aá]rquez|M[aá]rquez)\b`.
  Alternates cover full names, surnames, accents, and numbers written as
  digits or words.
- Dates (day and month): both orders are accepted, for example
  `15 April`, `April 15`, `April 15th`.
- Descriptions: several required parts, all of which must appear somewhere in
  the answer, written as lookaheads:
  `(?s)^(?=.*\b(?:light|sunlight)\b)(?=.*\b(?:glucose|sugars?|...)\b)`.
  These are kept strict enough that a wrong definition fails.

Scoring: an answer is right when
`re.search(answer_pattern, answer_text, re.IGNORECASE)` finds a match.

## Mix

Each file: 50 person, 30 place, 40 date or year, 30 number, 20 organization
or title of a work, 30 description. Topics cover geography, history, science,
biology, technology, arts, music, film, literature, sport and everyday
knowledge.

## How the questions were made

1. About 460 candidate questions were written by hand, avoiding anything that
   changes over time (officeholders, records, populations), opinion, trick
   questions, answers that appear in the question, and answers that are lists.
   A few candidates were removed before checking for being ambiguous (for
   example how many teaspoons are in a tablespoon, which differs by country).
2. Each candidate was checked directly against the pack files with SQLite:
   the source article was looked up by exact title in the shard files, and the
   answer pattern had to match a passage of that article. For candidates where
   a bare match could be a coincidence (short numbers, names that appear in a
   cast list, and so on) a second context word also had to appear in the same
   passage (for example "first president" for George Washington, "18-hole
   round" for golf). The matching passage of every kept question was read by
   eye to confirm it states the answer to that question.
3. Candidates that could not be confirmed this way were dropped.
4. Every pattern was compiled with Python and tested against the passage text
   it was verified on.
5. From the verified pool, 400 were chosen at random to fit the mix above and
   assigned at random (split evenly within each answer type) to dev or sealed,
   so both files come from the same distribution.

The system under test (`build/unllm`) was never run while making this exam, so
no question was chosen or reworded based on its behaviour.
