# a suduko style solver (4x4)
#
# This program generates lists with all permutations of 1..4 by
# solving a suduko style problem
#
# We have a grid of cells that can each take a value of
# 1,2,3 or 4 but only if there are no other cells in the 
# row or column with that value.

# The configuration has 9 cells, 
all LIST c11,c12,c13,c14,c21,c22,c23,c24,c31,c32,c33,c34,c41,c42,c42,c44;

# There are four rows with four cells each
row1 LIST c11,c12,c13,c14;
row2 LIST c21,c22,c23,c24;
row3 LIST c31,c32,c33,c34;
row4 LIST c41,c42,c43,c44;

# and there are four columns with four cells each
col1 LIST c11,c21,c31,c41;
col2 LIST c12,c22,c32,c42;
col3 LIST c13,c23,c33,c43;
col4 LIST c14,c24,c34,c44;
#
# and there are four corner groups with four cells each
grp1 LIST c11,c12,c21,c22;
grp2 LIST c13,c14,c23,c24;
grp3 LIST c31,c32,c41,c42;
grp4 LIST c33,c34,c43,c44;

# each cell is told the row, column and group that they participate in
c11 Cell(r:1, c:1) row1,col1,grp1;
c12 Cell(r:1, c:2) row1,col2,grp1;
c13 Cell(r:1, c:3) row1,col3,grp2;
c14 Cell(r:1, c:4) row1,col4,grp2;

c21 Cell(r:2, c:1) row2,col1,grp1;
c22 Cell(r:2, c:2) row2,col2,grp1;
c23 Cell(r:2, c:3) row2,col3,grp2;
c24 Cell(r:2, c:4) row2,col4,grp2;

c31 Cell(r:3, c:1) row3,col1,grp3;
c32 Cell(r:3, c:2) row3,col2,grp3;
c33 Cell(r:3, c:3) row3,col3,grp4;
c34 Cell(r:3, c:4) row3,col4,grp4;

c41 Cell(r:4, c:1) row4,col1,grp3;
c42 Cell(r:4, c:2) row4,col2,grp3;
c43 Cell(r:4, c:3) row4,col3,grp4;
c44 Cell(r:4, c:4) row4,col4,grp4;

# A cell will tentatively pick a value and once all cells in the row or column
# also have picked a tentative value the cell will be able to lock into that value.
#
Cell MACHINE row, col, grp {
	LOCAL OPTION r 0;
	LOCAL OPTION c 0;
	related LIST; # the set of all cells that this cell shares row or columns with

    one   WHEN SELF IS one   OR (SELF IS tentative_one   AND NOT ANY related ARE unknown AND NOT ANY related ARE blocked);
    two   WHEN SELF IS two   OR (SELF IS tentative_two   AND NOT ANY related ARE unknown AND NOT ANY related ARE blocked);
    three WHEN SELF IS three OR (SELF IS tentative_three AND NOT ANY related ARE unknown AND NOT ANY related ARE blocked);
    four  WHEN SELF IS four  OR (SELF IS tentative_four  AND NOT ANY related ARE unknown AND NOT ANY related ARE blocked);

	tentative_one WHEN
			( SELF IS unknown AND COUNT tentative_one FROM related == 0 AND COUNT one FROM related == 0
	            OR SELF IS tentative_one AND COUNT tentative_one FROM related == 1);

    tentative_two WHEN
			 ( SELF IS unknown AND COUNT tentative_two FROM related == 0 AND COUNT two FROM related == 0
	            OR SELF IS tentative_two AND COUNT tentative_two FROM related == 1);

    tentative_three WHEN
			( SELF IS unknown AND COUNT tentative_three FROM related == 0 AND COUNT three FROM related == 0
	            OR SELF IS tentative_three AND COUNT tentative_three FROM related == 1);

    tentative_four WHEN
				( SELF IS unknown AND COUNT tentative_four FROM related == 0 AND COUNT four FROM related == 0
	            OR SELF IS tentative_four AND COUNT tentative_four FROM related == 1);

	unknown DEFAULT;

	ENTER INIT {
		# NOTE: In clockwork a LIST of objects is actually a SET of objects. This
		# 	machine will break when we fix that problem
		CLEAR related; # protection from ENABLE/DISABLE causing the list to grow
		COPY ALL FROM row TO related;
		COPY ALL FROM col TO related;
		COPY ALL FROM grp TO related;
	}

}

