#pragma once
#include <Arduino.h>

// ==========================================
// STUDY APP
// ==========================================
// Revision notes live on the SD card as plain text, one file per subject, in
// the /study folder.  Nothing is embedded in the firmware, so notes can be
// added, edited or swapped without reflashing.
//
// File format (all markers are at the start of a line):
//
//   # Subject title        the first one wins; otherwise the file name is used
//   ## Topic heading       starts a topic AND becomes a flashcard front
//   Q: question            an extra flashcard (paired with the A: line)
//   A: answer              the back of that flashcard
//   - bullet point
//   anything else          body text
//
// Blank lines are kept as small gaps.  Long lines are wrapped by the app, so
// the files stay readable on a PC.  A note above ~16 KB is truncated on screen
// with a visible notice; tools/mkstudy.py converts HTML notes into this format
// and splits large ones into parts that fit.

enum StudyView {
  STUDY_VIEW_SUBJECTS,
  STUDY_VIEW_TOPICS,
  STUDY_VIEW_READER,
  STUDY_VIEW_CARDS
};

// Counts topics and flashcards in one note file by streaming it (used by the
// host tests and tools).  Returns false when the file cannot be opened.
bool studyScanFile(const char* path, int* topics, int* cards);
// Bytes of note text currently held from the heap (0 when the app is closed).
int studyPoolBytes();
// Re-lists the notes folder (falling back to the card root) and rebuilds the
// subject list.  Called on entry and by the RELOAD action; it only stats
// directory entries, so it is fast even with large notes.
void studyRefreshSubjects();
int studySubjectCount();

// Entry point used by the home screen: paints the app, reads the card and
// draws the subject list.  Never blocks for more than a bounded time.
void studyEnterApp();
void drawStudyScreen(bool fullWipe);
void handleStudyTouch(bool touched, int sx, int sy);
// Frees the text pool; called when the app is left so the RAM is not held for
// the rest of the session.
void studyRelease();
