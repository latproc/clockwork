# Here are some examples that uses a global setting as a determinant for
# property value lookups.

# given a user-defined global:

Language CONSTANT "ES";

# and some title messages:

Titles MACHINE {
  OPTION EN "A walk in the park";
  OPTION ES "Una caminata en el parque";
}
titles Titles;

# machine can be defined that uses the Language setting to 
# determine which message to use

TRANSLATOR MACHINE Strings {
  OPTION VALUE "";
  ENTER INIT { SET VALUE TO PROPERTY Language OF Strings; }
  ready DEFAULT;
}
title TRANSLATOR titles;

# This machine will set its VALUE on initialisation and then
# sit in the 'ready' state. An example of how to use the
# above might be to instantiate a machine:

check TitleCheck title;

# whre TitleCheck might be defined as:

TitleCheck MACHINE title {
  ENTER INIT {
    WAITFOR title IS ready;
    LOG title.VALUE;
  }
}

# As an alternative to the above, the Titles machine, above
# could be defined differently

RegionalTitle MACHINE {
  OPTION EN "A walk in the park";
  OPTION ES "Una caminata en el parque";
  OPTION VALUE "";
  ENTER INIT { SET VALUE TO PROPERTY Language; }
  ready DEFAULT;
}

regional_title RegionalTitle;

# and used using the same TitleCheck machine as above:

rt_check TitleCheck regional_title;


# A fairly convoluted alternative would be to use the
# LIST features to build a list of strings and select 
# a result from them 

RegionString MACHINE { OPTION lang "EN"; OPTION VALUE ""; }

EN_Title RegionString (lang:"EN", VALUE: "Happy days" );
EU_Title RegionString (lang:"ES", VALUE: "Días felices");

title_list LIST EN_Title, EU_Title;

book_title StringSelector title_list;
bt_check TitleCheck book_title;

StringSelector MACHINE strings {
  OPTION VALUE "";
  found REFERENCE;

  setup INITIAL;
  ready WHEN found IS ASSIGNED;

  ENTER setup {
    COPY 1 FROM strings TO found WHERE strings.ITEM.lang == Language;
    LOG "selected: " + found.ITEM.VALUE;
    VALUE := found.ITEM.VALUE;
    SET SELF TO ready;
  } 
}

