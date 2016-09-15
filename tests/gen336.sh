#!/bin/sh

# This script generates a grid of cells, each of which
# is given a list that contains all cells in its
# neighbourhood. See nkos336.cw for an example of
# its usage.

awk 'BEGIN { rows=10; cols=10; }
END{
	for (i=0; i<rows; i++) {
		r = sprintf("r%02d LIST ", i);
		rdelim = "";
		for (j=0; j<cols; j++) {
			r = sprintf("%s%s c%02d%02d", r, rdelim, i, j);
			rdelim = ",";
			neighbours = sprintf("l%02d%02d LIST ",i,j);
			delim="";
			for (l=-1; l<=1;l++) {
				ii=(i+rows+l) % rows
				for (m=-1; m<=1; m++) {
					jj = (j+cols+m)%cols
					neighbours = sprintf("%s%s c%02d%02d", neighbours, delim, ii,jj)
					delim = ","
				}
			}
			printf "%s;\n", neighbours
			printf "c%02d%02d Cell l%02d%02d;\n",i,j,i,j
		}
		printf "%s;\n", r
	}
	
}' </dev/null
