# SD card contents

Copy what is inside this folder onto the root of the micro SD card that goes in
the CYD. The firmware reads it from there, so notes can be changed without
reflashing the board.

```
/                 SD card root
  /study/         one .txt file per subject
     analytical-chemistry.txt
     optics.txt
     sinhala.txt
```

The notes in `study/` are samples kept in the repository so the app can be
reviewed and tested without a card attached. Replace them with your own.

## Note format

A note is plain text. Markers work at the start of a line:

| Line                        | Meaning                                          |
| --------------------------- | ------------------------------------------------ |
| `# Subject title`           | Name shown in the app (otherwise the file name)  |
| `## Topic heading`          | Starts a topic, and becomes a flashcard front    |
| `Q: question` / `A: answer` | An extra flashcard of your own                   |
| `- bullet point`            | Bullet                                           |
| anything else               | Body text                                        |

Long lines are wrapped by the app, so write paragraphs normally. Blank lines
become small gaps. A note bigger than the reader's 16 KB pool is truncated on
screen with a visible notice — the converter splits large notes into parts that
fit (15 KB each by default), and long ones are best split by topic anyway so
each part stays a manageable revision session.

## Converting HTML notes

`tools/mkstudy.py` turns exported HTML revision notes into this format:

```
python3 tools/mkstudy.py notes.html --out sd-card/study            # one file
python3 tools/mkstudy.py *.html --out sd-card/study                # a whole set
python3 tools/mkstudy.py big.html --out sd-card/study --split 15000
```

Headings become `##` topics, lists become bullets, tables become bullet rows
with `|` between the cells, and long paragraphs are wrapped to the width the
reader uses. Files are written lowercase-with-dashes, and each one lands under
the size the app can hold.

### Notes written in Sinhala

The panel's fonts are Latin only, so Sinhala cannot be drawn — it would show as
garbage. The converter romanises it instead (plain SLS-1134 style, ASCII only):

```
python3 tools/mkstudy.py jaiwa.html --out sd-card/study                 # romanise (default)
python3 tools/mkstudy.py jaiwa.html --out sd-card/study --sinhala drop  # remove Sinhala
python3 tools/mkstudy.py jaiwa.html --out sd-card/study --sinhala keep  # leave it alone
```

So `ජෛව විද්‍යාව` becomes `jaiwa widhyaawa`, `න්‍යෂ්ටිය` becomes `nyashtiya`,
`ද්‍රව්‍ය` becomes `dhrawya`, and `සෛලය ජීවයේ ඒකකයයි` becomes
`sailaya jiiwayee eekakayayi`. English text in the same document is untouched,
and `sinhala.txt` in `study/` is a converted sample. The romaniser has its own
checks — `python3 tools/mkstudy.py --selftest` — which the host harness runs
with `tools/hostcheck/check.sh`.
