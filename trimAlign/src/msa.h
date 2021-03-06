#if !defined(MSA_H)
#define MSA_H

// Multiple Sequence Alignment record
//
// Its really a fasta record if not seqStart and seqEnd fields, which only make
// sense in the context of MSA.
//
// Each record consists of a header with sequence name/ID and annotation, and a sequence,
// possibly split into multiple lines.
//

#include <stdio.h>

typedef struct
{
  FILE *fp;       // file handle
  char *buf;      // buffer for line input
  int   buflen;	  // current length of buf
  int   bsize;    // initial buffer size. it is also used as an increment in the buffer size
  char *name;     // sequence name
//char *desc;     // sequence description/annotation
  char *seq;      // sequence
  int   seqlen;   // sequence length
  int   ngSeqLen; // sequence length of gap free version of the sequence
  int   seqStart; // index of the first non-gap position
  int   seqEnd;   // index of the last non-gap position

} MSArec_t;

// buf is the buffer for holding a content of a line of a sequence and seq is a
// char pointer that stores sequence data
//
// in the case of a fasta file buflen is the length of the longest line read so
// far and typically it stays constant within a single file if the file is
// formated so that sequences occupy no more than certain number of characters
// per line. If each sequence is written in one line, then in principle each one
// can have different length and then buflen is updated to accomodate the longest
// sequence read so far.

#endif
