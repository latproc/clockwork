# a suduko style solver (3x3)
#
# This program generates lists with all permutations of 1..3 by
# solving a suduko style problem
#
# We have a grid of cells that can each take a value of
# 1,2 or 3 but only if there are no other cells in the
# row or column with that value.

# The configuration has 9 cells,
all LIST c11,c12,c13,c21,c22,c23,c31,c32,c33;

# There are three rows with three cells each
row1 LIST c11,c12,c13;
row2 LIST c21,c22,c23;
row3 LIST c31,c32,c33;

# and there are three columns with three cells each
col1 LIST c11,c21,c31;
col2 LIST c12,c22,c32;
col3 LIST c13,c23,c33;

# each cell is told the row and column that they participate in
c11 Cell(r:1, c:1) row1,col1 ;
c12 Cell(r:1, c:2) row1,col2;
c13 Cell(r:1, c:3) row1,col3;

c21 Cell(r:2, c:1) row2,col1;
c22 Cell(r:2, c:2) row2,col2;
c23 Cell(r:2, c:3) row2,col3;

c31 Cell(r:3, c:1) row3,col1;
c32 Cell(r:3, c:2) row3,col2;
c33 Cell(r:3, c:3) row3,col3;

AllTentative MACHINE related {
  false WHEN ANY all ARE unknown OR ANY all ARE blocked OR ANY all ARE wait OR ANY all ARE INIT;
  true WHEN SIZE OF related == 9 AND SELF IS true OR
      ( COUNT tentative_one FROM related == 3
         AND COUNT tentative_two FROM related == 3
         AND COUNT tentative_three FROM related == 3);
  false DEFAULT;
}
all_tentative AllTentative all;

# Here is cell that ultimately tries to get into a state of 'one', 'two' or 'three'
# corresponding to the legal values.
#
# A cell will tentatively pick a value and once all cells in the row or column
# also have picked a tentative value the cell will be able to lock into that value.
#
Cell MACHINE row, col {
  LOCAL OPTION r 0;
  LOCAL OPTION c 0;
  GLOBAL all_tentative;
  LOCAL OPTION delay 0;
  related LIST; # the set of all cells that this cell shares row or columns with

  wait WHEN (SELF IS wait OR SELF IS INIT) AND TIMER < 400; # give sampler time to connect

    one   WHEN SELF IS one   OR (SELF IS tentative_one && all_tentative IS true);
    two   WHEN SELF IS two   OR (SELF IS tentative_two && all_tentative IS true);
    three WHEN SELF IS three OR (SELF IS tentative_three && all_tentative IS true);

    unknown WHEN (SELF IS tentative_one OR SELF IS tentative_two OR SELF IS tentative_three)
                 AND all_tentative IS false AND TIMER > 40 AND (ANY related ARE blocked OR ANY related ARE unknown);
     tentative_one WHEN
      ( SELF IS unknown AND COUNT tentative_one FROM related == 0
              OR SELF IS tentative_one AND COUNT tentative_one FROM related == 1);

    tentative_two WHEN
      ( SELF IS unknown AND COUNT tentative_two FROM related == 0
              OR SELF IS tentative_two AND COUNT tentative_two FROM related == 1);

    tentative_three WHEN
      ( SELF IS unknown AND COUNT tentative_three FROM related == 0
              OR SELF IS tentative_three AND COUNT tentative_three FROM related == 1);

  blocked WHEN TIMER < delay; # yuk. we need to sit in blocked long enough for the other machines to stabilise
  unknown DEFAULT;

  ENTER INIT {
    # NOTE: In clockwork a LIST of objects is actually a SET of objects. This
    #   machine will break when we fix that problem
    COPY ALL FROM row TO related;
    COPY ALL FROM col TO related;
    delay := RANDOM % 10 + 2;
  }

}

