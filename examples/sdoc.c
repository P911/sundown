
/* Sundown library, for the Markdown processing */
#include "markdown.h"
#include "html.h"
/* use sundown utility for string handling */
#include "buffer.h"
#include "stack.h"

/* usual C std library */
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
  Simple-Doc extension: add a (HTML) reference for
  level 3 headers that contain a '%' or a '&'.

  **parameters**

  + cb
    * copy buffer, where the HTML-tag is added
  + idx
    * index of the current position in the copy buffer
*/
unsigned int
add_attrib(struct buf *cb, unsigned int idx, const char *ty,
	struct buf *in, unsigned int i_start, unsigned int i_len) {

	memcpy(cb->data + idx, "{", 1); idx++;
	memcpy(cb->data + idx, ty, strlen(ty)); idx += strlen(ty);
	memcpy(cb->data + idx, in->data + i_start, i_len); idx += i_len;
	memcpy(cb->data + idx, "}", 1); idx++;
	return idx;
}

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

static int put_char(struct buf *cb, int idx_out, int ch) {
	if (ch < 128) {
		assert((unsigned int) idx_out < cb->asize);
		cb->data[idx_out++] = ch;
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
			cb->data[idx_out++] = 0xc2 + (ch>0xbf);
			cb->data[idx_out++] = (ch&0x3f) + 0x80;
			break;
		default: /* hopefully already Utf-8 */
			cb->data[idx_out++] = ch;
			break;
		}
	}
	return idx_out;
}

// API comments: starting with /** and ending with */ or **/
// copy parts between comments
static void copy_comments(struct buf *cb, struct buf *ib) {
	unsigned int i, idx_out = 0;
	int api_comment_block = 0, api_pound = 0, is_header = 0;
	for (i = 0; i < ib->size; i++) {
		int ch = ib->data[i];
		// API comment block starts
	  	if (ch == '/' && ib->size - 2 > i) {
	    		if (ib->data[i + 1] == '*' && ib->data[i + 2] == '*') {
	    			api_comment_block = 1;
				i += 3;
				continue;
	    		}
	  	}
		// API comment block ends
		if (ch == '*' && ib->size - 2 > i) {
			if ((ib->data[i + 1] == '*' && ib->data[i + 2] == '/')
				|| ib->data[i + 1] == '/') {
				api_comment_block = 0;
				continue;
			}
		}
		if (api_comment_block) {
			// if this is a api comment: check if this is
			// a level 3 heading
			if (ch == '#') ++api_pound;

			if (api_pound == 3 && ch != '#') {
				is_header = 3;
			}
			if (is_header == 3 && ch == '#') {
				// 1. remove 3 '#'
				i += 2;
				// 2. add {#<name>}
				bufgrow(cb, cb->asize + 100);
				unsigned int k, l;
				// 2a. skip back to '&' or '%'
				for (k = i; k >= 0; k--) {
					if (ib->data[k] == '&') break;
					if (ib->data[k] == '%') break;
					if (ib->data[k] == '\n') break;
				}
				// 2b. find end of identifier
				for (l = k + 1; l < i; l++) {
					int ch = ib->data[l];
					if (!isdigit(ch) && !isalpha(ch) && ch != '_') {
						break;
					}
				}
				// 2c. add string to out buffer
				if (ib->data[k] == '&' || ib->data[k] == '%') {
					idx_out = add_attrib(cb, idx_out, "#",
						ib, k+1, l-k);
				}
			}

			if (api_pound == 4 && ch != '#') {
				is_header = 4;
			}
			if (is_header == 4 && ch == '#') {
				// 1. add {.<header>}
				bufgrow(cb, cb->asize + 100);
				unsigned int k;
				// 1a. skip back to '#'
				for (k = i - 1; k >= 0; k--) {
					if (ib->data[k] == '#') break;
					if (ib->data[k] == '\n') break;
				}
				// 1c. add string to out buffer
				if (ib->data[k] == '#') {
					idx_out = add_attrib(cb, idx_out, ".",
						ib, k+1, i-k-1);
				}
				// 2. remove 4 '#'
				i += 3;
			}

			if (api_pound && ch != '#') api_pound = 0;
			if (ch == '\n') is_header = 0;

			idx_out = put_char(cb, idx_out, ch);

		}
	}
	cb->size = idx_out;
}

static int
doc_api(struct buf *ib)
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

	sdhtml_renderer(&callbacks, &options, HTML_HARD_WRAP|HTML_H_ATTRIBUTES);
	markdown = sd_markdown_new(
		MKDEXT_TABLES /* support for tables */
		| MKDEXT_FENCED_CODE /* allow ~~~ instead of ``` */
		| MKDEXT_NO_INTRA_EMPHASIS /* no emphasis in identifier with '_' */
		, 16, &callbacks, &options);

	sd_markdown_render(ob, cb->data, cb->size, markdown);
	sd_markdown_free(markdown);

	/* HTML-header */
	fprintf(stdout, "<html><meta charset='utf-8'>\n");
	fprintf(stdout, "<head><link href=\"apidoc.css\" "
	                "rel=\"stylesheet\" type=\"text/css\">");
	fprintf(stdout, "</head><body>");
	fprintf(stdout, "<div id=\"Content\">");
	ret = fwrite(ob->data, 1, ob->size, stdout);
	fprintf(stdout, "</div>");

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
		doc_api(ib);
	}
	else {
		docco(ib);
	}
	bufrelease(ib);
	return 0;
}


