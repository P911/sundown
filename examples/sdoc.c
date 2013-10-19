
#include "markdown.h"
#include "html.h"
/* use sundown utility for string handling */
#include "buffer.h"
#include "stack.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

/* bytes to read per chung */
#define READ_UNIT 1024
/* initial output string size */
#define OUTPUT_UNIT 64

/**
### slice struct ###

Represents a part of a buffer.
*/
struct slice {
	unsigned int pos;
	unsigned int len;
	struct buf *buf;
};

/**
### slice_print ###

Print contents of a slice.

#### parameters ####

+ s
  * slice to print

*/
void slice_print(struct slice *s) {
	fprintf(stderr, "slice:[[");
	fwrite(s->buf->data + s->pos, 1, s->len, stderr);
	fprintf(stderr, "]]\n");
}

/**
### slice_set ###

Initialize a slice as substring of a buffer

#### parameters ####

+ s
  * slice struct (typically on the stack)
+ buf
  * buffer object
+ p
  * start position (offset) of the slice in the buffer
+ l
  * length of the slice string

*/
static void slice_set(struct slice *s,
	struct buf *buf, unsigned int p, unsigned int l) {
	s->buf = buf;
	s->pos = p;
	s->len = l;
}

/**
### slice_substring ###

Reset slice to be a substring of the current slice string.

#### parameters ####

+ s
  * slice to be modified
+ from
  * start offset for the substring (0-based)
+ len
  * length of the substring; if 0, use the complete rest of the string
*/
static void
slice_substring(struct slice *s, unsigned int from, unsigned int len) {
	assert(from < s->len);
	s->len -= from;
	s->pos += from;
	if (len > 0) {
		s->len = len;
	}
}

/**
### slice_trim ###

Remove leading and trailing whitespace from a slice.

#### parameters ####

+ s
  * slice object
*/
static void slice_trim(struct slice *s) {
	unsigned int i;
	/* remove whitespace at the beginning of the slice */
	for (i = s->pos; i < s->pos + s->len; ++i) {
		if (!isspace(s->buf->data[i])) {
			break;
		}
	}
	s->len = s->len - (i - s->pos);
	s->pos = i;
	/* remove whitespace at the end of the slice */
	for (i = s->pos + s->len - 1; i >= s->pos; --i) {
		if (!isspace(s->buf->data[i])) {
			break;
		}
	}
	s->len = i - s->pos + 1;
}

/**
### slice_find ###

Find the first occurance of one of a set of characters in a slice string.

#### parameters ####

+ s
  * slice object
+ clist
  * character array (NULL-terminated), representing the list of
    characters to search for

#### returns ####

Position of the first occurance of one of the characters in the
slice string.
*/
static int slice_find(struct slice *s, const char *clist) {
	unsigned int i, j;
	for (i = s->pos; i < s->pos + s->len; ++i) {
		for (j = 0; clist[j] != 0; ++j) {
			if (s->buf->data[i] == clist[j]) {
				return i - s->pos;
			}
		}
	}
	return -1;
}

/**
### slice_starts_with_id ###

Determines the longest prefix of the slice representing an
identifier. That is the longest prefix of the slice consisting
of alphanumeric und unterscore characters.

#### parameters ####

+ s
  * slice object

#### returns ####

Number of characters of the id-prefix. (0 if there is no
such prefix.)
*/
static unsigned int slice_starts_with_id(struct slice *s) {
	unsigned int i;
	for (i = s->pos; i < s->pos + s->len; i++) {
		int ch = s->buf->data[i];
		if (!isdigit(ch) && !isalpha(ch) && ch != '_') {
			break;
		}

	}
	return i - s->pos;
}

/**
### add_attrib ###

Simple-Doc extension: add a header attributes via
the markdown attribut extention ("## ... {#<id>|.<class>}").

####parameters####

+ cb
  * copy buffer, where the attribute is added
+ ty
  * attribut type: '.' for class or '#' for id
+ slice
  * slice buffer for the class/id string

####returns####

new current position for the copy buffer
*/
void
add_attrib(struct buf *cb, const char *ty, struct slice *slice) {

	/* {#<str>} or {.<str>} */
	bufputs(cb, "{");
	bufputs(cb, ty);
	bufput(cb, slice->buf->data + slice->pos, slice->len);
	bufputs(cb, "}");
}

/**
### read_files ###

Read the contents of all files into the input buffer.

####parameters####

+ ib
  * "input buffer" (to be filled with the contents of then input file)
+ argc
  * number of input file names
+ argv
  * list of input file names

####returns####

0 on success, 1 on error
*/
static int read_files(struct buf *ib, int argc, char **argv) {
	int j;
	/* TODO: for each file (sort?) */
	// concatenate the contents of all given files to one
	// input string buffer
	for (j = 0; j < argc; j++) {
		FILE *in = fopen(argv[j], "r");
		if (!in) {
			fprintf(stderr,"Unable to open input file \"%s\": %s\n", argv[j], strerror(errno));
			return 1;
		}
		// read everything from the file and add it to
		// the buffer
		int ret;
		while ((ret = fread(ib->data + ib->size, 1, ib->asize - ib->size, in)) > 0) {
			ib->size += ret;
			bufgrow(ib, ib->size + READ_UNIT);
		}
		fclose(in);
	}
	return 0;
}

/**
### put_char ###

Add a character to a buffer. If it is an ansii charactor, just
add it. If not, guess if it is utf-8 or latin1-encoding (assuming
the language is german). It it is not already utf-8, convert
it to utf-8.

#### parameters ####

+ cb
  * output buffer to write to
+ ch
  * the character (ansii, utf-8, or latin1 encoding)
*/
static void put_char(struct buf *cb, int ch) {
	if (ch < 128) {
		/* ansii: just add the char */
		bufputc(cb, ch);
	}
	else {
		/* guess encodung (german) */
		/* if german umlauts -> ok, else: convert to utf-8 */
		switch (ch) {
		/*Aa*/case 196: case 228:
		/*Oo*/ case 214: case 246:
		/*Uu*/ case 220: case 252:
		/*sz*/ case 223:
			/* seems to by latin-1 */
			bufputc(cb, 0xc2 + (ch>0xbf));
			bufputc(cb, (ch&0x3f) + 0x80);
			break;
		default: /* hopefully already Utf-8 */
			bufputc(cb, ch);
			break;
		}
	}
}

/**
### copy_comments ###

Copy (API) comment blocks from the input buffer to the output buffer.

Only comment blocks starting with '**' are copied.
It is assued that the comment blocks are formated in the markdown
markup language.

Header-3 and Header-4 elements are treated special:

* For Header-3 texts, try to find the descriped element/function
  an use the identifier as the HTML-ID of the HTML header tag.
* For Header-4 texts, use the text as HTML-class of the HTML header tag. 

The idea is to use Header-3 with the function name and Header-4 for
standardized sections (like "parameters", "returns", etc.).

#### parameters ####

+ cb
  * output buffer
+ ib
  * input buffer
*/
static void copy_comments(struct buf *cb, struct buf *ib) {
	unsigned int i;
	int api_comment_block = 0, api_pound = 0, is_header = 0;

	/* read input char by char */
	for (i = 0; i < ib->size; i++) {

		/* read current character */
		int ch = ib->data[i];

		// check if API comment block starts
	  	if (ch == '/' && ib->size - 2 > i) {
	    		if (ib->data[i + 1] == '*' && ib->data[i + 2] == '*') {
	    			api_comment_block = 1;
				i += 3;
				continue;
	    		}
	  	}

		// check if API comment block ends
		if (ch == '*' && ib->size - 2 > i) {
			if ((ib->data[i + 1] == '*' && ib->data[i + 2] == '/')
				|| ib->data[i + 1] == '/') {
				if (api_comment_block) {
					// add newline after earch comment
					// block to force new paragraph
					// for markdown
					put_char(cb, '\n');
				}
				api_comment_block = 0;
				continue;
			}
		}

		/* if not within (api) comment block: read next char */
		if (!api_comment_block) continue;

		/* if api comment:
		   - check for (specially treater) headings
		   - write current char to output
		*/

		// if this is a api comment: check if this is
		// a level 3 heading
		if (ch == '#') ++api_pound;

		if (api_pound == 3 && ch != '#') {
			is_header = 3;
			/* state: in header line */
		}
		if (is_header == 3 && (ch == '#' || ch == '\n')) {
			struct slice slice;
			/* state: in header line and at end of header text */
			// 1. add {#<name>}
			unsigned int k;
			for (k = i - 1; k >= 0; k--) {
				if (ib->data[k] == '#') break;
			}
			slice_set(&slice, ib, k + 1, i - k - 1);

			slice_trim(&slice);

			if (slice_find(&slice, "&%") != -1) {
				// if the string contains '&'/'%':
				// use the identifier as ID
				// remove the '&'/'%'
				slice_substring(&slice,
					slice_find(&slice, "&%") + 1, 0);
			}

			slice_substring(&slice, 0,
				slice_starts_with_id(&slice));
			// 1c. add string to out buffer
			add_attrib(cb, "#", &slice);
			// 2. remove 3 '#'
			if (ch == '#') i += 2;
			// 3. reset header
			api_pound = 0;
			is_header = 0;
			continue;
		}

		if (api_pound == 4 && ch != '#') {
			is_header = 4;
		}
		if (is_header == 4 && ch == '#') {
			struct slice slice;
			// 1. add {.<header>}
			// 1a. skip back to '#'
			unsigned int k;
			for (k = i - 1; k >= 0; k--) {
				if (ib->data[k] == '#') break;
			}
			slice_set(&slice, ib, k + 1, i - k - 1);
			slice_trim(&slice);
			// 1c. add string to out buffer
			add_attrib(cb, ".", &slice);
			// 2. remove 4 '#'
			i += 3;
			// 3. reset header
			api_pound = 0;
			is_header = 0;
			continue;
		}

		if (api_pound && ch != '#') api_pound = 0;
		if (ch == '\n') is_header = 0;

		put_char(cb, ch);
	}
}

/**
### doc_api ###

Process API-comments from an source files and produce HTML
from the Markdown in the comments.

#### parameters ####

+ ib
  * input buffer
+ out
  * output file (where the HTML should be written)

#### returns ####

0 on success, -1 on error
*/
static int
doc_api(struct buf *ib, FILE *out)
{
	struct buf *ob;
	struct buf *cb;
	int ret;

	struct sd_callbacks callbacks;
	struct html_renderopt options;
	struct sd_markdown *markdown;

	// copy API comments
	cb = bufnew(ib->size);
	bufgrow(cb, ib->size);
	copy_comments(cb, ib);

	/* performing markdown parsing */
	ob = bufnew(OUTPUT_UNIT);

	/* HTML-header */
	fprintf(out, "<!DOCTYPE html>\n");
	fprintf(out, "<html><meta charset='utf-8'>\n");
	fprintf(out, "<head><link href=\"apidoc.css\" "
	                "rel=\"stylesheet\" type=\"text/css\">");
	fprintf(out, "</head><body>");

	/* contents */
	sdhtml_renderer(&callbacks, &options, HTML_HARD_WRAP|HTML_H_ATTRIBUTES|HTML_TOC);
	markdown = sd_markdown_new(
		MKDEXT_TABLES /* support for tables */
		| MKDEXT_FENCED_CODE /* allow ~~~ instead of ``` */
		| MKDEXT_NO_INTRA_EMPHASIS /* no emphasis in identifier with '_' */
		, 16, &callbacks, &options);

	sd_markdown_render(ob, cb->data, cb->size, markdown);
	sd_markdown_free(markdown);

	fprintf(out, "<div id=\"Doc\">\n");
	ret = fwrite(ob->data, 1, ob->size, out);
	fprintf(out, "</div>\n");

	/* TOC */
	bufreset(ob);
	sdhtml_toc_h_renderer(&callbacks, &options, HTML_H_ATTRIBUTES);
	markdown = sd_markdown_new(
		MKDEXT_TABLES /* support for tables */
		| MKDEXT_FENCED_CODE /* allow ~~~ instead of ``` */
		| MKDEXT_NO_INTRA_EMPHASIS /* no emphasis in identifier with '_' */
		, 16, &callbacks, &options);

	sd_markdown_render(ob, cb->data, cb->size, markdown);
	sd_markdown_free(markdown);

	fprintf(out, "<div id=\"Nav\">\n");
	ret = fwrite(ob->data, 1, ob->size, out);
	fprintf(out, "</div>\n");

	/* cleanup */
	bufrelease(ob);
	bufrelease(cb);

	return (ret < 0) ? -1 : 0;
}

static int is_comment_line(struct buf *ib, unsigned int i) {
	unsigned int j = i;
	// skip whitespace
	for (j = i; j < ib->size; j++) {
		if (ib->data[j] == '\n') break;
		if (!isspace(ib->data[j])) break;
	}
	// SAS: two kinds of allowed comments '%*' and '*'
	if (j >= ib->size || ib->data[j] == '\n') return 0; /* empty line */
	if (ib->data[j] == '*') return (j-i) + 1;
	if (j < ib->size - 1) {
		if (ib->data[j] == '%' && ib->data[j+1] == '*') return (j-i) + 2;
	}
	return 0;
}

// search for the next newline
// if there are only empty/space characters before the newline the
// line is called 'empty'
// returns number of characters in the line
static int is_empty_line(struct buf *ib, unsigned int i) {
	unsigned int j;
	for (j = i; j < ib->size; j++) {
		if (ib->data[j] == '\n') {
			return (j-i) + 1;
		}
		if (!isspace(ib->data[j])) break;
	}
	return 0;
}

static unsigned int fill_line(struct buf *ib, unsigned int i, struct buf *ob) {
	unsigned int j;
	for (j = i; j < ib->size; j++) {
		bufputc(ob, ib->data[j]);
		if (ib->data[j] == '\n') {
			return (j-i) + 1;
		}
	}
	return (j-i) + 1;
}

// add a new section to the doc-stack and the code-stack
static void add_section(struct stack *doc, struct stack *code) {
	struct buf *ndoc = bufnew(128);
	struct buf *ncode = bufnew(128);
	stack_push(doc, ndoc);
	stack_push(code, ncode);
}

static void docco_parse(struct buf *ib, struct stack *doc, struct stack *code) {
	int prev_line_empty = 0, have_doc = 0, have_code = 0;
	unsigned int i = 0;
	add_section(doc, code);
	while (i < ib->size) {
		/* invariant: ib->data[i] is alway beginning of a line */
		int empty = is_empty_line(ib, i);
		int cmt = is_comment_line(ib, i);
		if (cmt) {
			if (prev_line_empty && have_doc && !have_code) {
				/* if doc <empty> doc, treat first doc as a section without code */
				add_section(doc, code);
				have_doc = 0;
				i += empty;
			}
			if (have_code) {
				add_section(doc, code);
				have_code = 0;
			}
			have_doc = 1;
			i += cmt;
			unsigned int s = fill_line(ib, i, stack_top(doc));
			i += s;
		}
		else {
			if (!empty) have_code = 1;
			unsigned int s = fill_line(ib, i, stack_top(code));
			i += s;
		}
		prev_line_empty = empty;
	}
	printf("found %d/%d sections\n", code->size, doc->size);
	for (i = 0; i < doc->size; i++) {
		struct buf *b = doc->item[i];
		bufputc(b, 0);
		printf("section %d:\n%s", i, b->data);
	}
}

static void docco(struct buf *ib) {
	struct stack sections_doc;
	struct stack sections_code;
	stack_init(&sections_doc, 10);
	stack_init(&sections_code, 10);

	docco_parse(ib, &sections_doc, &sections_code);
	// docco_html(&sections_doc, &section_code);

	unsigned int i;
	for (i = 0; i < sections_doc.size; i++) {
		bufrelease(sections_doc.item[i]);
		bufrelease(sections_code.item[i]);
	}
	stack_free(&sections_code);
	stack_free(&sections_doc);
}

int
main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Usage: <prog> -docco|-api {input-file}.\n");
		return 1;
	}

	/* arguments are the input (code) files */
	if (argc < 3) {
		fprintf(stderr, "Missing input files.\n");
		return 1;
	}

	struct buf *ib;
	ib = bufnew(READ_UNIT);
	bufgrow(ib, READ_UNIT);

	int r;
	r = read_files(ib, argc - 2, argv + 2);
	if (r) {
		bufrelease(ib);
		return r;
	}

	if (strcmp(argv[1], "-api") == 0) {
		doc_api(ib, stdout);
	}
	else {
		docco(ib);
	}
	bufrelease(ib);
	return 0;
}


