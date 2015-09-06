#include "lib/mlrutil.h"
#include "input/peek_file_reader.h"

//typedef struct _peek_file_reader_t {
//      byte_reader_t* pbr;
//      int   peekbuflen;
//      char* peekbuf;
//      int   npeeked;
//} peek_file_reader_t;

peek_file_reader_t* pfr_alloc(byte_reader_t* pbr, int maxnpeek) {
	peek_file_reader_t* pfr = mlr_malloc_or_die(sizeof(peek_file_reader_t));
	pfr->pbr        =  pbr;
	pfr->peekbuflen =  maxnpeek + 1;
	pfr->peekbuf    =  mlr_malloc_or_die(pfr->peekbuflen);
	memset(pfr->peekbuf, 0, pfr->peekbuflen);
	pfr->npeeked    =  0;

	return pfr;
}

void pfr_free(peek_file_reader_t* pfr) {
	if (pfr == NULL)
		return;
	free(pfr->peekbuf);
	free(pfr);
}

char pfr_peek_char(peek_file_reader_t* pfr) {
	if (pfr->npeeked < 1) {
		pfr->peekbuf[pfr->npeeked++] = pfr->pbr->pread_func(pfr->pbr);
	}
	return pfr->peekbuf[0];
}

void pfr_buffer_by(peek_file_reader_t* pfr, int len) {
	while (pfr->npeeked < len) {
		pfr->peekbuf[pfr->npeeked++] = pfr->pbr->pread_func(pfr->pbr);
	}
}

void pfr_advance_by(peek_file_reader_t* pfr, int len) {
	// xxx stub
}

void pfr_dump(peek_file_reader_t* pfr) {
	// xxx stub
}
