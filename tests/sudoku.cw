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

# Here is cell that ultimately tries to get into a state of 'one', 'two' or 'three'
# corresponding to the legal values.
#
# A cell will tentatively pick a value and once all cells in the row or column
# also have picked a tentative value the cell will be able to lock into that value.
#
Cell MACHINE row, col {
	LOCAL OPTION r 0;
	LOCAL OPTION c 0;

    one WHEN SELF IS one OR (SELF IS tentative_one && NOT ANY row ARE unknown AND NOT ANY col ARE unknown 
			AND NOT ANY row ARE blocked AND NOT ANY col ARE blocked );
    two WHEN SELF IS two OR (SELF IS tentative_two && NOT ANY row ARE unknown AND NOT ANY col ARE unknown
			AND NOT ANY row ARE blocked AND NOT ANY col ARE blocked );
    three WHEN SELF IS three OR (SELF IS tentative_three && NOT ANY row ARE unknown AND NOT ANY col ARE unknown
			AND NOT ANY row ARE blocked AND NOT ANY col ARE blocked );

	tentative_one WHEN NOT ANY row ARE blocked AND NOT ANY col ARE blocked
			AND	( (SELF IS unknown 
				AND COUNT tentative_one FROM row == 0 AND COUNT tentative_one FROM col == 0
				AND COUNT one FROM row == 0 AND COUNT one FROM col == 0
				)                 
            OR SELF IS tentative_one AND COUNT tentative_one FROM row == 1 AND COUNT tentative_one FROM col == 1 );
    tentative_two WHEN NOT ANY row ARE blocked AND NOT ANY col ARE blocked
			AND ( (SELF IS unknown 
				AND COUNT tentative_two FROM row == 0 AND COUNT tentative_two FROM col == 0
				AND COUNT two FROM row == 0 AND COUNT two FROM col == 0
				)
            OR (SELF IS tentative_two AND COUNT tentative_two FROM row == 1 AND COUNT tentative_two FROM col == 1) );
    tentative_three WHEN NOT ANY row ARE blocked AND NOT ANY col ARE blocked
			AND	( (SELF IS unknown 
				AND COUNT tentative_three FROM row == 0 AND COUNT tentative_three FROM col == 0
				AND COUNT three FROM row == 0 AND COUNT three FROM col == 0
				)
            OR (SELF IS tentative_three AND COUNT tentative_three FROM row == 1 AND COUNT tentative_three FROM col == 1)) ;

	blocked WHEN TIMER < 40;
	unknown DEFAULT;
	unknown INITIAL;

}
