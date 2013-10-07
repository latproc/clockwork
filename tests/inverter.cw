Inverter MACHINE input, inverted { active WHEN 1==1, TAG inverted WHEN input != on;}
Inverted MACHINE input { on WHEN input == off; off DEFAULT; }

a FLAG;
a_dash FLAG;
b Inverter a, a_dash;

