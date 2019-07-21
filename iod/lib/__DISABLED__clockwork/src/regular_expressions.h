/*
  Copyright (C) 2012 Martin Leadbeater, Michael O'Connor

  This file is part of Latproc

  Latproc is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  Latproc is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Latproc; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifndef __REGULAR_EXPRESSIONS_H__
#define __REGULAR_EXPRESSIONS_H__

#ifndef _WIN32
#include <regex.h>
#endif
#include <vector>
#include <string>

typedef struct rexp_info
{
  char *pattern;
  int compilation_result;
  char *compilation_error;
#ifndef _WIN32
  regex_t regex;
  regmatch_t *matches;
#endif
} rexp_info;

rexp_info *create_pattern(const char *pat);

size_t numSubexpressions(const rexp_info *info);

int execute_pattern(rexp_info *info, const char *string);

void release_pattern(rexp_info *info);

int matches(const char *string, const char *pattern);

int is_integer(const char *string);

/* tests the precompiled pattern against the text provided and return zero if the
    match succeeded.

    If the entire matched pattern is returned in variables[0] and
    if there are any subexpressions, the corresponding matches are returned in variables[1]..[n]
*/
int find_matches(rexp_info *info, std::vector<std::string> &variables, const char *string);

/* substitute the subt string into the text. Using the precompiled pattern.

    If the precompiled pattern contains subexpressions, these are first extracted
    into variables by the use of find_matches().
*/
char * substitute_pattern(rexp_info *info, std::vector<std::string> &variables, const char *text, const char *subst);

/* here is a way for users to iterate through each match */
typedef int (match_func)(const char *match, int index, void *user_data);

int each_match(rexp_info *info, const char *text, size_t *end_offset, match_func f, void *user_data);

#endif
