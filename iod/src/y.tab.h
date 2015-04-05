/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton interface for Bison's Yacc-like parsers in C

   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     MODULES = 258,
     MODULE = 259,
     DEFAULTS = 260,
     DEFAULT = 261,
     STATE = 262,
     ENTER = 263,
     NUMBER = 264,
     SEPARATOR = 265,
     OEXPR = 266,
     EEXPR = 267,
     OBRACE = 268,
     EBRACE = 269,
     QUOTE = 270,
     LE = 271,
     LT = 272,
     GE = 273,
     GT = 274,
     NE = 275,
     EQ = 276,
     NOT = 277,
     SET = 278,
     AND = 279,
     OR = 280,
     SQUOTE = 281,
     LOG = 282,
     FLAG = 283,
     PROPERTY = 284,
     DEFINE = 285,
     COLLECT = 286,
     FROM = 287,
     TEST = 288,
     EXECUTE = 289,
     SPAWN = 290,
     RUN = 291,
     CALL = 292,
     TRIM = 293,
     LINE = 294,
     OF = 295,
     USING = 296,
     MATCH = 297,
     IN = 298,
     REPLACE = 299,
     WITH = 300,
     INTERPRET = 301,
     COMMA = 302,
     BEGINPROP = 303,
     ENDPROP = 304,
     PROPSEP = 305,
     STATEMACHINE = 306,
     WHEN = 307,
     TO = 308,
     RECEIVE = 309,
     DURING = 310,
     WAIT = 311,
     WAITFOR = 312,
     TRANSITION = 313,
     TAG = 314,
     INC = 315,
     DEC = 316,
     BY = 317,
     CONCAT = 318,
     SEND = 319,
     CONDITION = 320,
     INITSTATE = 321,
     IF_ = 322,
     ELSE = 323,
     ENABLE = 324,
     DISABLE = 325,
     BECOMES = 326,
     GLOBAL = 327,
     GROUP = 328,
     OPTION = 329,
     LOCK = 330,
     UNLOCK = 331,
     ON = 332,
     RESUME = 333,
     AT = 334,
     SHUTDOWN = 335,
     COMMAND = 336,
     EXPORT = 337,
     READONLY = 338,
     READWRITE = 339,
     WORD = 340,
     DOUBLEWORD = 341,
     STRINGTYPE = 342,
     STATES = 343,
     MATCHES = 344,
     COMMANDS = 345,
     COPY = 346,
     EXTRACT = 347,
     ALL = 348,
     REQUIRES = 349,
     WHERE = 350,
     ROUTE = 351,
     ANY = 352,
     ARE = 353,
     COUNT = 354,
     SELECT = 355,
     TAKE = 356,
     LENGTH = 357,
     INCLUDES = 358,
     INCLUDE = 359,
     CREATE = 360,
     BITSET = 361,
     PROPERTIES = 362,
     ENTRIES = 363,
     SORT = 364,
     ENABLED = 365,
     DISABLED = 366,
     SIZE = 367,
     ITEM = 368,
     FIRST = 369,
     LAST = 370,
     COMMON = 371,
     BETWEEN = 372,
     DIFFERENCE = 373,
     COMBINATION = 374,
     CLEAR = 375,
     LEAVE = 376,
     ASSIGN = 377,
     WITHIN = 378,
     PUSH = 379,
     MOVE = 380,
     ITEMS = 381,
     PLUGIN = 382,
     CHANNEL = 383,
     IDENTIFIER = 384,
     VERSION = 385,
     SHARES = 386,
     MONITORS = 387,
     UPDATES = 388,
     SENDS = 389,
     RECEIVES = 390,
     INTERFACE = 391,
     EXTENDS = 392,
     KEY = 393,
     MACHINES = 394,
     MATCHING = 395,
     NAME = 396,
     EXPORTS = 397,
     CONSTRAINT = 398,
     IGNORE = 399,
     IGNORES = 400,
     THROTTLE = 401,
     INTEGER = 402,
     SYMBOL = 403,
     STRINGVAL = 404,
     PATTERN = 405,
     UMINUS = 406,
     IFX = 407,
     IFELSE = 408
   };
#endif
/* Tokens.  */
#define MODULES 258
#define MODULE 259
#define DEFAULTS 260
#define DEFAULT 261
#define STATE 262
#define ENTER 263
#define NUMBER 264
#define SEPARATOR 265
#define OEXPR 266
#define EEXPR 267
#define OBRACE 268
#define EBRACE 269
#define QUOTE 270
#define LE 271
#define LT 272
#define GE 273
#define GT 274
#define NE 275
#define EQ 276
#define NOT 277
#define SET 278
#define AND 279
#define OR 280
#define SQUOTE 281
#define LOG 282
#define FLAG 283
#define PROPERTY 284
#define DEFINE 285
#define COLLECT 286
#define FROM 287
#define TEST 288
#define EXECUTE 289
#define SPAWN 290
#define RUN 291
#define CALL 292
#define TRIM 293
#define LINE 294
#define OF 295
#define USING 296
#define MATCH 297
#define IN 298
#define REPLACE 299
#define WITH 300
#define INTERPRET 301
#define COMMA 302
#define BEGINPROP 303
#define ENDPROP 304
#define PROPSEP 305
#define STATEMACHINE 306
#define WHEN 307
#define TO 308
#define RECEIVE 309
#define DURING 310
#define WAIT 311
#define WAITFOR 312
#define TRANSITION 313
#define TAG 314
#define INC 315
#define DEC 316
#define BY 317
#define CONCAT 318
#define SEND 319
#define CONDITION 320
#define INITSTATE 321
#define IF_ 322
#define ELSE 323
#define ENABLE 324
#define DISABLE 325
#define BECOMES 326
#define GLOBAL 327
#define GROUP 328
#define OPTION 329
#define LOCK 330
#define UNLOCK 331
#define ON 332
#define RESUME 333
#define AT 334
#define SHUTDOWN 335
#define COMMAND 336
#define EXPORT 337
#define READONLY 338
#define READWRITE 339
#define WORD 340
#define DOUBLEWORD 341
#define STRINGTYPE 342
#define STATES 343
#define MATCHES 344
#define COMMANDS 345
#define COPY 346
#define EXTRACT 347
#define ALL 348
#define REQUIRES 349
#define WHERE 350
#define ROUTE 351
#define ANY 352
#define ARE 353
#define COUNT 354
#define SELECT 355
#define TAKE 356
#define LENGTH 357
#define INCLUDES 358
#define INCLUDE 359
#define CREATE 360
#define BITSET 361
#define PROPERTIES 362
#define ENTRIES 363
#define SORT 364
#define ENABLED 365
#define DISABLED 366
#define SIZE 367
#define ITEM 368
#define FIRST 369
#define LAST 370
#define COMMON 371
#define BETWEEN 372
#define DIFFERENCE 373
#define COMBINATION 374
#define CLEAR 375
#define LEAVE 376
#define ASSIGN 377
#define WITHIN 378
#define PUSH 379
#define MOVE 380
#define ITEMS 381
#define PLUGIN 382
#define CHANNEL 383
#define IDENTIFIER 384
#define VERSION 385
#define SHARES 386
#define MONITORS 387
#define UPDATES 388
#define SENDS 389
#define RECEIVES 390
#define INTERFACE 391
#define EXTENDS 392
#define KEY 393
#define MACHINES 394
#define MATCHING 395
#define NAME 396
#define EXPORTS 397
#define CONSTRAINT 398
#define IGNORE 399
#define IGNORES 400
#define THROTTLE 401
#define INTEGER 402
#define SYMBOL 403
#define STRINGVAL 404
#define PATTERN 405
#define UMINUS 406
#define IFX 407
#define IFELSE 408




#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 123 "/Users/martin/projects/latproc/github/latproc/iod/src//cwlang.ypp"
{
	int iVal;
	const char *sVal;
	const char *symbol;
    const char *pVal;
	Predicate *expr;
	PredicateOperator opr;
	Value *val;
	Parameter *param;
	struct Subcondition *subcond;
}
/* Line 1529 of yacc.c.  */
#line 367 "/Users/martin/projects/latproc/github/latproc/iod/src//cwlang.tab.hpp"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif

extern YYSTYPE yylval;

