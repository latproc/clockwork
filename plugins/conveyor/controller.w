% Note that to use the listing command to read the source
% file into an appendix, you need to enable eplain
% which will clash with cwebmac because of the multiple use of ifpdf
% the easiest way around this is to input cwebmac after eplain and
% remove the auto generated input cwebmac at the beginning
% of {thisfile}.tex
%\input eplain
%\input cwebmac

\acrofalse
\def\title{TITLE}
%\pagewidth=15.5cm
%\pageheight=25.5cm
%\fullpageheight=25.8cm
%\setpage
\input graphicx
\input picture
\def\startlist{\medskip\leftskip=2pc}
\def\endlist{\medskip\leftskip=0pt}
\def\label#1{\expandafter\let\csname lbl#1\endcsname\secno}
\def\ref#1{\csname lbl#1\endcsname}
	
\noinx
\nosecs
\nocon
\noatl
%
%\titletrue
%\vskip1in\noindent{\titlefont \title}\hfill\par
%\vskip0.5cm\noindent{Author\par\noindent\today}\hfill\par


@* Introduction.\medskip

\noindent
This program attempts to control a conveyor carrying a wool bale
by appropriate variations of a an power output value 
given an input reading of a position encoder. There
are several practical considerations such as the fact that:

\startlist
$\bullet$ the conveyor control hardware has a certain delay before
control instructions have an effect at the output

$\bullet$ the control change itself takes time to move from
one setting to another, this time may be insignificant
 
\endlist

The program attempts to determine an approximate power setting
to achieve a certain speed and makes small adjustments to that level 
to control the speed.

The program outline is constructed as follows.
% note that the 'h' tag inserts defines created using the 'd' tag

@c
@<Include necessary header files@>@;
@h
@<Define fundamental types and declarations needed by other declarations@>@;
@<Declare types, functions and shared variables@>@;
@<Implement functions@>@;

int main(int argc, char *argv[])
{
   @<Declare main function variables@>@;
   @<Initialise the application structures@>@;
   @<Perform the application task@>@;
   return 0;
}

@ The \CEE/ language does not have a boolean type so we 
define one for use throughout our program.
%@f BOOL double
@d FALSE 0
@d TRUE 1

@<Define fundamental types and declarations needed by other declarations@>=
typedef int BOOL;

@ The program needs some standard libraries for access to 
input and output on standard io streams and for access to 
functions such as {\tt exit()}.

@<Include necessary header files@>=
#include <stdio.h>
#include <stdlib.h>

@ In this template there are no functions or shared variables.

@<Declare types, functions and shared variables@>=

@ There are no functions to implement.

@<Implement functions@>=

@ There are no variables.

@<Declare main function variables@>=

@ No initialisation is required.

@<Initialise the application structures@>=

@ This is a hello world application so we simply
say hello.

@<Perform the application task@>=
  printf("Hello World\n");

@*Programmers notes.
Under Mac~OS~X or Linux, the program 
can be compiled and linked using:
\startlist\par {\tt ctangle controller.w}
\par {\tt cc -o controller controller.c}
\endlist

@ Index.\medskip

% include the C source (requires eplain)
@% % * The \CEE/ source file.
%
%\listing{controller.c}

