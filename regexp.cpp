/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *\
 * This file is part of Suneido - The Integrated Application Platform
 * see: http://www.suneido.com for more information.
 * 
 * Copyright (c) 2000 Suneido Software Corp. 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation - version 2. 
 *
 * This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License in the file COPYING
 * for more details. 
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA
\* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "regexp.h"
#include <ctype.h>
#include <limits.h>
#include "cachemap.h"
#include "gcstring.h"
#include "except.h"

enum
	{
	PATEND,
	CHAR,
	ANY,
	START, END, 
	START_LINE, END_LINE, 
	START_WORD, END_WORD,
	CCL, NCCL, CCLEND, RANGE,
	DIGIT, NDIGIT, WORD, NWORD, SPACE, NSPACE,
	ALPHA, ALNUM, BLANK, CNTRL, GRAPH, LOWER, PRINT, PUNCT, UPPER, XDIGIT,
	BRANCH, NGBRANCH,
	TRY, OR,
	LEFT, RIGHT,
	PIECE,
	IGNORE_CASE, CASE_SENSITIVE
	};

/* first the stuff to build a pattern from a string
 *
 * uses a recursive descent parser with the following grammar:
 *
 *	regexp	:	sequence				LEFT 0 ... RIGHT 0 ENDPAT
 *			|	sequence(|sequence)+	TRY n sequence (OR n sequence)+
 *
 *	sequence	:	element+
 *
 *	element		:	^					START_LINE
 *				|	$					END_LINE
 *				|	\A					START
 *				|	\Z					END
 *				|	(?i)				IGNORE_CASE
 *				|	(?-i)				CASE_SENSITIVE
 *				|	(?q)				start quoted (literal)
 *				|	(?-q)				end quoted (literal)
 *				|	\<					START_WORD
 *				|	\>					END_WORD
 *				|	\#					PIECE #
 *				|	simple
 *				|	simple ?			BRANCH-forward simple
 *				|	simple +			simple BRANCH-backward
 *				|	simple *			BRANCH-forward simple BRANCH-backward
 *				|	simple ??			NGBRANCH-forward simple
 *				|	simple +?			simple NGBRANCH-backward
 *				|	simple *?			NGSTAR simple NGBRANCH-backward
 *
 *	simple		:	.					ANY
 *				|	[...]				CCL ... CCLEND
 *				|	[^...]				NCCL ... CCLEND
 *				|	\d					DIGIT
 *				|	\D					NDIGIT
 *				|	\w					WORD
 *				|	\W					NWORD
 *				|	\s					SPACE
 *				|	\S					NSPACE
 *				|	( regexp )			LEFT i ... RIGHT i
 *				|	character			CHAR c
 *
 *	within a character class:
 *				a-z						RANGE a z
 */

class RxCompile
	{
public:
	RxCompile(const char* src, int len) :
		s(src), lim(s + len), 
		buflen(len * 3 + 8), buf(new char[buflen]), n(0), nl(1),
		literal(false)
		{ }
	char* compile();
private:
	void regexp();
	void sequence();
	void element();
	void simple();
	void cclass();
	void posix_cclass();
	bool match(char* token);
	void output(int c);
	void output(int c1, int c2)
		{
		output(c1);
		output(c2);
		}
	void insert(int c, int i);
	char next()
		{ return s + 1 < lim ? s[1] : 0; }

	const char* s;		// input
	const char* lim;	// input limit
	int buflen;
	char* buf;			// output
	int n;				// output length
	int nl;				// left parenthesis count
	bool literal;
	};

char* rx_compile(const gcstring& s)
	{
	static CacheMap<5,gcstring,char*> cache;
	if (char** p = cache.get(s))
		return *p;
	char* pat = RxCompile(s.buf(), s.size()).compile();
	cache.put(s, pat);
	return pat;
	}

char* RxCompile::compile()
	{
	output(LEFT, 0);
	regexp();
	output(RIGHT, 0);
	output(PATEND);
	verify(n <= buflen);
	return (char*) buf;
	}

bool RxCompile::match(char* token)
	// post:	if token was matched, s is advanced past it, and true is returned
	//			else s is unchanged, and false is returned
	{
	const char* t = s;
	for (; *token; ++t, ++token)
		if (t >= lim || *t != *token)
			return false;
	s = t;
	return true;
	}

void RxCompile::regexp()
	{
	int start = n; 
	sequence(); 
	if (s < lim && *s == '|')
		{
		int len = n - start;
		insert(TRY, start); insert(len, start + 1);
		while (s < lim && *s == '|')
			{
			++s;
			start = n; sequence(); len = n - start;
			insert(OR, start); insert(len, start + 1);
			}
		}
	}

void RxCompile::sequence()
	{
	if (s >= lim)
		return ;
	element();
	while (s < lim && (literal || (*s != '|' && *s != ')')))
		{
		element();
		}
	}
		
void RxCompile::element()
	{
	if (literal)
		{
		if (match("(?-q)"))
			literal = false;
		else
			output(CHAR, *s), ++s;
		}
	else if (*s == '^')
		output(START_LINE), ++s;
	else if (*s == '$')
		output(END_LINE), ++s;
	else if (s[0] == '\\' && next() == 'A')
		output(START), s += 2;
	else if (s[0] == '\\' && next() == 'Z')
		output(END), s += 2;
	else if (s[0] == '\\' && next() == '<')
		output(START_WORD), s += 2;
	else if (s[0] == '\\' && next() == '>')
		output(END_WORD), s += 2;
	else if (match("(?i)"))
		output(IGNORE_CASE);
	else if (match("(?-i)"))
		output(CASE_SENSITIVE);
	else if (match("(?q)"))
		literal = true;
	else
		{
		int start = n;
		simple();
		int len = n - start;
		if (*s == '?' && next() == '?')
			{
			insert(NGBRANCH, start); insert(len, start + 1);
			s += 2;
			}
		if (*s == '?')
			{
			insert(BRANCH, start); insert(len, start + 1);
			++s;
			}
		else if (*s == '*' && next() == '?')
			{
			output(NGBRANCH, -len);
			insert(BRANCH, start); insert(len + 2, start + 1);
			s += 2;
			}
		else if (*s == '*')
			{
			output(BRANCH, -len);
			insert(BRANCH, start); insert(len + 2, start + 1);
			++s;
			}
		else if (*s == '+' && next() == '?')
			{
			output(NGBRANCH, -len);
			s += 2;
			}
		else if (*s == '+')
			{
			output(BRANCH, -len);
			++s;
			}
		}
	}

void RxCompile::simple()
	{
	if (*s == '.')
		output(ANY), ++s;
	else if (s[0] == '\\' && next() == 'd')
		++s, output(DIGIT), ++s;
	else if (s[0] == '\\' && next() == 'D')
		++s, output(NDIGIT), ++s;
	else if (s[0] == '\\' && next() == 'w')
		++s, output(WORD), ++s;
	else if (s[0] == '\\' && next() == 'W')
		++s, output(NWORD), ++s;
	else if (s[0] == '\\' && next() == 's')
		++s, output(SPACE), ++s;
	else if (s[0] == '\\' && next() == 'S')
		++s, output(NSPACE), ++s;
	else if (s[0] == '\\' && '1' <= next() && next() <= '9')
		output(PIECE, *++s - '0'), ++s;
	else if (*s == '\\' && s+1 < lim)
		++s, output(CHAR, *s), ++s;
	else if (*s == '[')
		cclass();
	else if (*s == '(')
		{
		++s;
		int tmp = nl++;
		output(LEFT, tmp);
		regexp();
		if (*s == ')')
			++s;
		output(RIGHT, tmp);
		}
	else
		{
		output(CHAR, *s);
		++s;
		}
	}

void RxCompile::cclass()
	{
	if (*s == '[')
		++s;
	if (*s != '^' && next() == ']')
		{
		output(CHAR, *s); // e.g. [.]
		s += 2;
		return ;
		}
	if (*s == '^')
		output(NCCL), ++s;
	else
		output(CCL);
	if (*s == ']')
		output(CHAR, ']'), ++s;
	for (; s < lim && *s != ']'; )
		{
		if (next() == '-' && s+2 < lim && s[2] != ']')
			output(RANGE), output(*s), output(s[2]), s += 3;
		else if (s[0] == '\\' && next() == 'd')
			output(DIGIT), s += 2;
		else if (s[0] == '\\' && next() == 'D')
			output(NDIGIT), s += 2;
		else if (s[0] == '\\' && next() == 'w')
			output(WORD), s += 2;
		else if (s[0] == '\\' && next() == 'W')
			output(NWORD), s += 2;
		else if (s[0] == '\\' && next() == 's')
			output(SPACE), s += 2;
		else if (s[0] == '\\' && next() == 'S')
			output(NSPACE), s += 2;
		else if (s[0] == '\\' && s + 1 < lim)
			output(CHAR, s[1]), s += 2;
		else if (s[0] == '[' && next() == ':')
			posix_cclass();
		else
			output(CHAR, *s), ++s;
		}
	if (*s == ']')
		++s;
	output(CCLEND);
	}

void RxCompile::posix_cclass()
	{
	static struct { char* name; int output; } classes[] = {
		{ "[:alnum:]", ALNUM },
		{ "[:alpha:]", ALPHA },
		{ "[:blank:]", BLANK },
		{ "[:cntrl:]", CNTRL },
		{ "[:digit:]", DIGIT },
		{ "[:graph:]", GRAPH },
		{ "[:lower:]", LOWER },
		{ "[:print:]", PRINT },
		{ "[:punct:]", PUNCT },
		{ "[:space:]", SPACE },
		{ "[:upper:]", UPPER },
		{ "[:xdigit:]", XDIGIT },
		{ NULL, 0 }
		};
	for (int i = 0; ; ++i)
		if (s + 5 >= lim || ! classes[i].name)
			except("bad posix character class");
		else if (has_prefix(s, classes[i].name))
			{
			output(classes[i].output);
			s += strlen(classes[i].name);
			break ;
			}
	}
			
void RxCompile::output(int c)
	{
	verify(SCHAR_MIN <= c && c <= SCHAR_MAX);
	buf[n++] = c;
	}

void RxCompile::insert(int c, int i)
	{
	verify(SCHAR_MIN <= c && c <= SCHAR_MAX);
	memmove(buf + i + 1, buf + i, n - i);
	buf[i] = c;
	++n;
	}

/* next the stuff to match a pattern and a string
 *
 * uses iteration where possible
 * recurses for BRANCH (? * +) and alternatives
 * amatch finds longest match
 * match finds first successful amatch
 */

class RxMatch
	{
public:
	RxMatch(char* str, int len, Rxpart* pts)
		: s(str), n(len), part(pts ? pts : parts)
		{ }
	int amatch(int pos, char* pat);
private:
	bool domatch();
	bool omatch();
	bool cclass();

	char* s;	// string
	int i;		// current position in string
	int n;		// length of string
	char* p;	// current position in pattern
	Rxpart* part;
	Rxpart parts[MAXPARTS];	// used if none passed in
	bool ignore_case;
	enum { MAXNEST = 500 };
	int domatch_nest;
	};

bool rx_match(char* s, int n, char* pat, Rxpart* psubs)
	{
	int i = 0;
	int inc = 1;
	if (n < 0)
		{
		// searches backwards if n < 0
		n = -n;
		inc = -1;
		i = n;
		}
	RxMatch match(s, n, psubs);
	do
		{
		if (0 <= match.amatch(i, pat))
			return true;
		i += inc;
		}
		while (i <= n && i >= 0);
	return false;
	}

int rx_amatch(char* s, int i, int n, char* pat, Rxpart* psubs)
	{
	return RxMatch(s, n, psubs).amatch(i, pat);
	}

int RxMatch::amatch(int pos, char* pat)
	{
	for (int j = 0; j < MAXPARTS; ++j)
		part[j].n = -1;
	i = pos;
	p = pat;
	domatch_nest = 0;
	ignore_case = false;
	if (! domatch())
		return -1;
	return (i);
	}

bool RxMatch::domatch()
	{
	char* save_p;
	int save_i;
	int offset;

	if (++domatch_nest > MAXNEST)
		return false;
	while (*p != PATEND)
		{
		switch (*p)
			{
		case CHAR :
			++i;
			p += 2;
			if (i > n || 
				(ignore_case ? tolower(p[-1]) != tolower(s[i-1]) : p[-1] != s[i-1]))
				return false;
			break ;
		case BRANCH :
			p += 2;
			save_p = p; save_i = i;
			offset = *((signed char *) p - 1);
			if (offset < 0)						// backward branch '+' or '*'
				{
				// try to go back
				p += offset - 2;
				if (domatch())
					return true;
				// else go on, don't repeat
				p = save_p; i = save_i;
				}
			else								// forward branch '?'
				{
				// try to not skip
				if (domatch())
					return true;
				// else skip and continue
				p = save_p; i = save_i;
				p += offset;
				}
			break ;
		case NGBRANCH : // non-greedy
			p += 2;
			save_p = p; save_i = i;
			offset = *((signed char *) p - 1);
			if (offset < 0)						// backward branch '+' or '*'
				{
				// try to match rest without repeating
				if (domatch())
					return true;
				// else go back & repeat
				p = save_p; i = save_i;
				p += offset - 2;
				}
			else								// forward branch '?'
				{
				// try to skip
				p += offset;
				if (domatch())
					return true;
				// else don't skip
				p = save_p; i = save_i;
				}
			break ;
		case TRY :
			save_i = i;
			while (TRY == *p || OR == *p)
				{
				save_p = ++p; ++p;
				if (domatch())
					return true; // matched everything
				p = save_p, i = save_i;
				p += *((signed char *) p) + 1;
				}
			return false; // none of alternatives matched
		case OR :
			++p;
			p += *((signed char *) p) + 1; // skip
			break ;
		default :
			if (! omatch())
				return false;
			}
		}
	return true;
	}

#define ISSPACE(c)	isspace((unsigned char) (c))
#define ISALPHA(c)	isalpha((unsigned char) (c))
#define ISDIGIT(c)	isdigit((unsigned char) (c))
#define ISWORD(c)	(isalnum((unsigned char) (c)) || c == '_')
#define TOLOWER(c)	tolower((unsigned char) (c))
#define TOUPPER(c)	toupper((unsigned char) (c))

bool RxMatch::omatch()
	{
	switch (*p++)
		{
		// positions
	case START :
		return i == 0;
	case END :
		return i == n || 
			(i == n - 1 && s[i] == '\n') ||
			(i == n - 2 && s[i] == '\r' && s[i+1] == '\n');
	case START_LINE :
		return i == 0 || s[i-1] == '\n';
	case END_LINE :
		return i == n || s[i] == '\n' || s[i] == '\r';
	case START_WORD :
		return i == 0 || ! ISWORD(s[i-1]);
	case END_WORD :
		return i == n || ! ISWORD(s[i]);

		// modes
	case IGNORE_CASE :
		ignore_case = true;
		return true;
	case CASE_SENSITIVE :
		ignore_case = false;
		return true;

		// single characters
	case ANY :
		return i < n && (s[i] != '\n' && s[i] != '\r' && s[i] != 0) ? (++i, true) : false;
	case CCL :
		return i < n ? cclass() : false;
	case NCCL :
		return i < n ? ! cclass() : false;
	case DIGIT :
		return i < n && ISDIGIT(s[i]) ? (++i, true) : false;
	case NDIGIT :
		return i < n && ! ISDIGIT(s[i]) ? (++i, true) : false;
	case WORD :
		return i < n && ISWORD(s[i]) ? (++i, true) : false;
	case NWORD :
		return i < n && ! ISWORD(s[i]) ? (++i, true) : false;
	case SPACE :
		return i < n && ISSPACE(s[i]) ? (++i, true) : false;
	case NSPACE :
		return i < n && ! ISSPACE(s[i]) ? (++i, true) : false;

		// grouping
	case LEFT :
		if (*p < MAXPARTS)
			part[*p].tmp = s + i;
		++p;
		return true;
	case RIGHT :
		if (*p < MAXPARTS)
			{
			part[*p].s = part[*p].tmp;
			part[*p].n = (s + i) - part[*p].s;
			}
		++p;
		return true;

		// backreference
	case PIECE :
		{
		char* t = part[*p].s;
		int tn = part[*p].n;
		++p;
		for (int j = 0; j < tn && i < n; ++j, ++i)
			if (ignore_case ? tolower(t[j]) != tolower(s[i]) : t[j] != s[i])
				return false;
		return true;
		}
	default :
		unreachable();	
		}
	}

bool RxMatch::cclass()
	{
	bool in;

	int c = (unsigned char) s[i++];
	if (ignore_case)
		c = tolower(c);
	for (in = false; *p != CCLEND; )
		{
		switch (*p)
			{
		case CHAR :
			if (c == (ignore_case ? tolower(p[1]) : p[1]))
				in = true;
			p += 2;
			break ;
		case RANGE :
			{
			unsigned p1 = (ignore_case ? tolower(p[1]) : p[1]);
			unsigned p2 = (ignore_case ? tolower(p[2]) : p[2]);
			if (p1 <= (unsigned) c && (unsigned) c <= p2)
				in = true;
			p += 3;
			break ;
			}
		case DIGIT :
			if (isdigit(c))
				in = true;
			++p;
			break ;
		case NDIGIT :
			if (! isdigit(c))
				in = true;
			++p;
			break ;
		case WORD :
			if (ISWORD(c))
				in = true;
			++p;
			break ;
		case NWORD :
			if (! ISWORD(c))
				in = true;
			++p;
			break ;
		case SPACE :
			if (isspace(c))
				in = true;
			++p;
			break ;
		case NSPACE :
			if (! isspace(c))
				in = true;
			++p;
			break ;
		case ALPHA :
			if (isalpha(c))
				in = true;
			++p;
			break ;
		case ALNUM :
			if (isalnum(c))
				in = true;
			++p;
			break ;
		case BLANK :
			if (c == ' ' || c == '\t')
				in = true;
			++p;
			break ;
		case CNTRL :
			if (iscntrl(c))
				in = true;
			++p;
			break ;
		case GRAPH :
			if (isgraph(c))
				in = true;
			++p;
			break ;
		case LOWER :
			if (islower(c))
				in = true;
			++p;
			break ;
		case UPPER :
			if (isupper(c))
				in = true;
			++p;
			break ;
		case PRINT :
			if (isprint(c))
				in = true;
			++p;
			break ;
		case PUNCT :
			if (ispunct(c))
				in = true;
			++p;
			break ;
		case XDIGIT :
			if (isxdigit(c))
				in = true;
			++p;
			break ;
		default :
			unreachable();
			}
		}
	++p;
	return in;
	}

// replace ==========================================================

int rx_replen(const char* rep, Rxpart* subs)
	{
	if (rep[0] == '\\' && rep[1] == '=')
		return strlen(rep) - 2;

	int len = 0;
	for (; *rep; ++rep)
		{
		if (*rep == '&')
			{
			if (subs->n > 0)
				len += subs[0].n;
			}
		else if ('\\' == *rep && rep[1])
			{
			++rep;
			if (ISDIGIT(*rep))
				{
				if (subs[*rep - '0'].n > 0)
					len += subs[*rep - '0'].n;
				}
			else if (*rep != 'u' && *rep != 'l' &&
				*rep != 'U' && *rep != 'L' && *rep != 'E')
				++len;
			}
		else
			++len;
		}
	return len;
	}

inline char trcase(char& tr, char c)
	{
	c = tr == 'E' ? c :
		(tr == 'L' || tr == 'l' ? tolower(c) : toupper(c));
	if (tr == 'u' || tr == 'l')
		tr = 'E';
	return c;
	}

inline char* insert(char* dst, Rxpart& part, char& tr)
	{
	int n = part.n;
	if (n <= 0)
		return dst;
	char* s = part.s;
	for (; n--; ++s)
		*dst++ = trcase(tr, *s);
	return dst;
	}

char* rx_mkrep(char* buf, const char* rep, Rxpart* subs)
	{
	if (rep[0] == '\\' && rep[1] == '=')
		return strcpy(buf, rep + 2);

	char tr = 'E';
	char *dst = buf;
	for (; *rep; ++rep)
		{
		if (*rep == '&')
			dst = insert(dst, subs[0], tr);
		else if ('\\' == *rep && rep[1])
			{
			++rep;
			if (ISDIGIT(*rep))
				dst = insert(dst, subs[*rep - '0'], tr);
			else if (*rep == 'n')
				*dst++ = '\n';
			else if (*rep == 't')
				*dst++ = '\t';
			else if (*rep == '\\')
				*dst++ = '\\';
			else if (*rep == '&')
				*dst++ = '&';
			else if (*rep == 'u' || *rep == 'l' ||
				*rep == 'U' || *rep == 'L' || *rep == 'E')
				tr = *rep;
			else
				*dst++ = *rep;
			}
		else
			{
			*dst++ = trcase(tr, *rep);
			}
		}
	*dst = 0;
	return buf;
	}

#include "testing.h"
#include <stdio.h>

struct Rxtest
	{
	char* string;
	char* pattern;
	bool result;
	};	
static Rxtest rxtests[] =
	{
	{ "hello", "hello", true },
	{ "hello", "^hello$", true },

	{ "hello\nworld", "^hello$", true },
	{ "hello\nworld", "\\Ahello", true },
	{ "hello\nworld", "\\Aworld", false },
	{ "hello\nworld", "world\\Z", true },
	{ "hello\nworld", "hello\\Z", false },

	{ "hello\r\nworld", "^hello$", true },
	{ "hello\r\nworld", "\\Ahello", true },
	{ "hello\r\nworld", "\\Aworld", false },
	{ "hello\r\nworld", "world\\Z", true },
	{ "hello\r\nworld", "hello\\Z", false },

	{ "one_1 two_2\nthree_3", "\\<one_1\\>", true },
	{ "one_1 two_2\nthree_3", "\\<two_2\\>", true },
	{ "one_1 two_2\nthree_3", "\\<three_3\\>", true },
	{ "one_1 two_2\r\nthree_3", "\\<two_2\\>", true },
	{ "one_1 two_2\r\nthree_3", "\\<three_3\\>", true },
	{ "one_1 two_2\nthree_3", "\\<one\\>", false },
	{ "one_1 two_2\nthree_3", "\\<two\\>", false },

	{ "hello", "fred", false },
	{ "hello", "h.*o", true },
	{ "hello", "[a-z]ello", true },
	{ "hello", "[^0-9]ello", true },
	{ "hello", "ell", true },
	{ "hello", "^ell", false },
	{ "hello", "ell$", false },
	{ "heeeeeeeello", "^he+llo$", true },
	{ "heeeeeeeello", "^he*llo*", true },
	{ "hllo", "^he*llo$", true },
	{ "hllo", "^he?llo$", true },
	{ "hello", "^he?llo$", true },
	{ "heello", "^he?llo$", false },
	{ "+123.456", "^[+-][0-9]+[.][0123456789]*$", true },

	{ "0123456789", "^\\d+$", true },
	{ "0123456789", "\\D", false },
	{ "hello_123", "^\\w+$", true },
	{ "hello_123", "\\W", false },
	{ "hello \t\r\nworld", "^\\w+\\s+\\w+$", true },
	{ "!@#@!# \r\t{},()[];", "^\\W+$", true },
	{ "123adjf!@#", "^\\S+$", true },
	{ "123adjf!@#", "\\s", false },

	{ "()[]", "^\\(\\)\\[\\]$", true },
	{ "hello world", "^(hello|howdy) (\\w+)$", true },
	{ "ab", "(a|ab)b", true }, // alternation backtrack
	{ "abc", "x*c", true },
	{ "abc", "x*$", true },
	{ "abc", "x?$", true },
	{ "abc", "^x?", true },
	{ "abc", "^x*", true },
	{ "aBcD", "abcd", false },
	{ "aBcD", "(?i)abcd", true },
	{ "aBCd", "a(?i)bc(?-i)d", true },
	{ "aBCD", "a(?i)bc(?-i)D", true },
	{ "ABCD", "a(?i)bc(?-i)d", false },
	{ "abc", "a.c", true },
	{ "a.c", "(?q)a.c", true },
	{ "abc", "(?q)a.c", false },
	{ "a.cd", "(?q)a.c(?-q).", true },
	{ "abcd", "(?q)a.c(?-q).", false },
	{ "abc", "(?q)(", false },
	{ "ABC", "(?i)[A-Z]", true },
	{ "ABC", "(?i)[a-z]", true },
	{ "abc", "(?i)[A-Z]", true },
	{ "abc", "(?i)[a-z]", true }
	};

class test_regexp : public Tests
	{
	TEST(0, match)
		{
		for (int i = 0; i < sizeof rxtests / sizeof (Rxtest); ++i)
			{
			char* pattern = rxtests[i].pattern;
			char* pat = rx_compile(pattern);
			char* string = rxtests[i].string;
			except_if(rxtests[i].result != rx_match(string, strlen(string), pat, NULL),
				string << " =~ " << pattern << " should be " << (rxtests[i].result ? "true" : "false"));
			}
		}
	TEST(1, replace)
		{
		char* pat = rx_compile("(\\w+) (\\w+)");
		Rxpart parts[MAXPARTS];
		char* s = "hello WORLD";
		rx_amatch(s, 0, strlen(s), pat, parts);
		char* rep = "\\u\\1 \\l\\2 \\U\\1 \\L\\2 \\E\\1 \\2";
		size_t replen = rx_replen(rep, parts);
		char buf[200];
		rx_mkrep(buf, rep, parts);
		asserteq(strlen(buf), replen);
		verify(0 == strcmp(buf, "Hello wORLD HELLO world hello WORLD"));
		}
	TEST(2, group_repetition_bug)
		{
		char* pat = rx_compile("(\\w+ )+");
		Rxpart parts[MAXPARTS];
		char* s = "hello world ";
		rx_match(s, strlen(s), pat, parts);
		asserteq(parts[0].s, s);
		asserteq(parts[0].n, 12);
		asserteq(parts[1].s, s + 6);
		asserteq(parts[1].n, 6);
		}
	TEST(3, escape_nul_bug)
		{
		asserteq(rx_replen("\\", NULL), 1);
		}
	};
REGISTER(test_regexp);
