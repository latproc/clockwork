#
# railroad.awk, 11 Jun 12
#
# Take a yacc/bison file as input and generate railroad diagrams
# (well, at least something to feed into dot and which then ...)

function save_rhs()
{
rule_rhs[rule_name]=rule_rhs[rule_name] "|" rhs_str # not yet used
all_rule[num_lhs]=all_rule[num_lhs] "|" rhs_str
rhs_str=""
}

function get_rhs(start_field)
{
# Get all tokens to the right of the colon in a grammar
if ($start_field == "|")
   {
   start_field++
   save_rhs()
   }
# Does not yet handle { ... } embedded in rhs
for (i=start_field; i < NF; i++)
   {
   word_cnt[$i]++  # how many times have we seen this word?
   if (rhs_str == "")
      rhs_str=$i
   else
      rhs_str=rhs_str " " $i
   }

if ($NF == ";")
   in_rule=0
if (($NF == ";") || ($NF == "|"))
   save_rhs()
else if (NF >= start_field)
   rhs_str=rhs_str " " $NF

}


function gen_start_rule(r_num, rule_name, num_rules)
{
# .dot stuff needed to encapsulate production rules
gen_nest_depth=0
total_nodes=0
print "digraph rule_" r_num "{"
print "fontname=helvetica ; fontsize=14"
print "node[width=0.3, height=0.2]"

# Name of the production
print "label=\"" rule_name "\""
gen_start_to_end(num_rules)
}

function gen_end_rule()
{
# matching end to gen_start_rule
print "}"
print "}"
}

function get_new_node()
{
# each .dot node has a uniqu enumber
total_nodes++
return total_nodes
}

function push_gen_nest()
{
# At the moment there is only one nesting level, but we have ambitions
gen_nest_depth++
start_stack[gen_nest_depth]=cur_start_node
end_stack[gen_nest_depth]=cur_end_node
cur_start_node=get_new_node()
cur_end_node=get_new_node()
}


function gen_start_to_end(num_rules)
{
# Generate a start/end node
push_gen_nest()
print "n__ [label=\"...\"]"
gen_edge("_", ":s", cur_start_node, ":n")
gen_node_right(cur_end_node, "...")

# Using a subgraph cluster gives us a box around everything in the rule
print "subgraph cluster_rule {"
print "node[fontsize=" int((17*7)/(7+num_rules)) "]"
#print "node[fontname=helvetica]"
print "label=\"\""
print "n_" cur_start_node " ["
print "label=\"\", color=\"white\", shape=point, height=0.01, width=0.01]"
print "n_" cur_end_node " ["
print "label=\"\", color=\"white\", shape=point, height=0.01, width=0.01]"
}


function gen_edge(from_n, from_pos, to_n, to_pos)
{
# Generate an edge
print "n_" from_n from_pos " -> n_" to_n to_pos
}

function gen_node_right(from_node, new_node_str)
{
# as if reading right to left
return gen_node(from_node, new_node_str, 1)
}

function gen_node_left(from_node, new_node_str, first_in_list)
{
# as if reading left to right
return gen_node(from_node, new_node_str, 0, first_in_list)
}

function gen_node(from_node, new_node_str, go_right, first_in_list)
{
# Assume everything in upper case is a token
if (new_node_str ~ /^[A-Z]+$/)
   new_node_str="\\\"" tolower(new_node_str) "\\\""
new_node_num=get_new_node()
print "n_" new_node_num "[label=\"" new_node_str "\"]"
if (go_right)
   gen_edge(from_node, ":s", new_node_num, ":n")
else
# The edge start end point can make a big difference to how the line looks
   if (first_in_list)
      gen_edge(from_node, ":w", new_node_num, ":se")
   else
      gen_edge(from_node, ":n", new_node_num, ":s")

return new_node_num
}

function gen_loop_back(rule_name, parts, num_parts)
{
# A loop back occurs when a rule is self recursive, e.g.,
# x : y |
#     x comma y
# we generate a "y" and then "comma" is in the loopback path

np_cur_node=cur_end_node
is_first=1
for (np=1; np <= num_parts; np++)
   {
#print "lb " np " " parts[np]
   if (parts[np] != "")
      {
      np_cur_node=gen_node_left(np_cur_node, parts[np], is_first) 
      is_first=0
      }
   }
# dot lays things out differently if the loopback is empty
if (is_first)
   gen_edge(np_cur_node, ":n", cur_start_node, ":e")
else
   gen_edge(np_cur_node, ":n", cur_start_node, ":w")
}


function gen_one_rule(rule_name, rule, end_node)
{
#print "gen one " rule_name " : " rule
num_parts=split(rule, parts, " ")
# TODO
# If there is only one name and it is only used here, then
# we should pint it here
if (num_parts == 1)
   {
   if (word_cnt[parts[1]] == 1)
      ;
   }
np_cur_node=cur_start_node
for (np=1; np <= num_parts; np++)
   {
   np_cur_node=gen_node_right(np_cur_node, parts[np]) 
   }
if (end_node != 0)
   gen_edge(np_cur_node, ":s", end_node, ":n")

return np_cur_node
}


function gen_two_rules(many_rule)
{
# Handle common special cases such an optional constructs
# and self recursive rules

rule_name=many_rule[1]
rule_1=many_rule[2]
rule_2=many_rule[3]
#print "gen two " rule_name " : " rule_1 " | " rule_2
num_part_1=split(rule_1, part_1, " ")
# an optional construct?
if (num_part_1 == 0)
   {
   gen_edge(cur_start_node, ":s", cur_end_node, ":n")
   gen_one_rule(rule_name, rule_2, cur_end_node)
   return
   }
num_part_2=split(rule_2, part_2, " ")
# a left recursive rule?
if ((part_2[1] == rule_name) &&
    (part_2[num_part_2] == part_1[1]))
   {
   part_2[1]=""
   gen_one_rule(rule_name, part_2[num_part_2], cur_end_node)
   part_2[num_part_2]=""
   gen_loop_back(rule_name, part_2, num_part_2)
   return
   }
# a right recursive rule?
if ((part_2[num_part_2] == rule_name) &&
    (part_2[1] == part_1[1]))
   {
   part_2[num_part_2]=""
   gen_one_rule(rule_name, part_2[1], cur_end_node)
   part_2[1]=""
   gen_loop_back(rule_name, part_2, num_part_2)
   return
   }
gen_many_rules(many_rule, 3)
}

function gen_many_rules(many_rule, num_prod)
{
# Check if all rules start with the same token sequence
r1_cnt=split(many_rule[2], r1_toks, " ")
num_common=r1_cnt

# What is the longest number of tokens?
for (ri=3; ri <= num_prod; ri++)
   {
   rn_cnt=split(many_rule[ri], rn_toks, " ")
   for (rc=1; rc <= num_common; rc++)
      if (r1_toks[rc] != rn_toks[rc])
	 {
	 num_common=rc-1
	 break
	 }
   if (num_common == 0)
      break
   }

if (num_common > 0)
   {
# Generate diagram for common sequence
   subrule=r1_toks[1]
   for (ri=2; ri <= num_common; ri++)
      subrule=subrule " " r1_toks[ri]
#print "sr " subrule
   new_start_node=gen_one_rule(many_rule[1], subrule, 0)
   cur_start_node=new_start_node

# Remove common part from all rules
   for (ri=2; ri <= num_prod; ri++)
      {
      rn_cnt=split(many_rule[ri], rn_toks, " ")
      subrule=rn_toks[num_common+1]
      for (rc=num_common+2; rc <= rn_cnt; rc++)
         subrule=subrule " " rn_toks[rc]
      many_rule[ri]=subrule
      }
   }

# Generate diagram for each rule
for (ri=2; ri <= num_prod; ri++)
   gen_one_rule(many_rule[1], many_rule[ri], cur_end_node)
}


BEGIN {
	in_rule=0
	curl_count=0
	num_lhs=0
	total_nodes=0
	in_C_comment=0
	}

NF == 0 ||
$1 == "//" {
	next
	}

in_C_comment == 1 {
	if ($0 ~ /\*\//)
	   in_C_comment=0
	next
	}

$1 == "/*" {
	in_C_comment=1
	if ($0 ~ /\*\//)
	   in_C_comment=0
	next
	}

$1 == "{" { # handle nested {}
	curl_count++
	next
	}

$1 == "}" { # handle nested {}
	curl_count--
	if (curl_count == 0)
	   {
	   if ($NF == ";")
	      in_rule=0
	   if (($NF == ";") || ($NF == "|"))
	      save_rhs()
	   }
	next
	}

curl_count > 0 { # skip code associated with rule
	next
	}

in_rule == 1 {
	get_rhs(1)
	next
	}

# Look for identifier following by colon
$1 ~ /^[a-zA-Z_0-9]+:$/ ||
($2 == ":" &&
 $1 ~ /^[a-zA-Z_0-9]+$/) {
	in_rule=1
	num_lhs++
	sub(":", "", $1)
	rhs_str=""
	rule_name=$1
	rule_rhs[rule_name]=""
	all_rule[num_lhs]=rule_name
	if ($2 == ":")
	   get_rhs(3)
	else
	   get_rhs(2)
	next
	}

	{
	next
	}

END {
	for (r=1; r <= num_lhs; r++)
	   {
	   num_prod=split(all_rule[r], rule_parts, "|")
	   if (num_prod == 0)
	      continue
	   gen_start_rule(r, rule_parts[1], num_prod)
	   if (num_prod == 2) # rule name always followed by |
	      gen_one_rule(rule_parts[1], rule_parts[2], cur_end_node)
	   else if (num_prod == 3) # rule name always followed by |
	      gen_two_rules(rule_parts)
	   else
	      gen_many_rules(rule_parts, num_prod)
	   gen_end_rule()
	   }
	}

