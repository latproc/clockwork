z01 FLAG(tab:Test, type:Output, zone:1);
z02 FLAG(tab:Test, type:Output, zone:2);
z03 FLAG(tab:Test, type:Output, zone:3);
z04 FLAG(tab:Test, type:Output, zone:4);
z05 FLAG(tab:Test, type:Output, zone:5);
z06 FLAG(tab:Test, type:Output, zone:6);
z07 FLAG(tab:Test, type:Output, zone:7);
z08 FLAG(tab:Test, type:Output, zone:8);
z09 FLAG(tab:Test, type:Output, zone:9);
z10 FLAG(tab:Test, type:Output, zone:10);
z11 FLAG(tab:Test, type:Output, zone:11);
z12 FLAG(tab:Test, type:Output, zone:12);
z13 FLAG(tab:Test, type:Output, zone:13);
z14 FLAG(tab:Test, type:Output, zone:14);
z15 FLAG(tab:Test, type:Output, zone:15);
z16 FLAG(tab:Test, type:Output, zone:16);
z17 FLAG(tab:Test, type:Output, zone:17);
z18 FLAG(tab:Test, type:Output, zone:18);
zones LIST z01, z02, z03, z04, z05, z06, z07, 
        z08, z09, z10, z11, z12, 
        z13, z14, z15, z16, z17, z18;

TestFormula MACHINE L_Zones {
OPTION last_bitset 0;

fixing WHEN last_bitset != BITSET FROM L_Zones;
equal WHEN SIZE OF L_ZonesFront == SIZE OF L_ZonesBack;
front WHEN SIZE OF L_ZonesFront >= SIZE OF L_ZonesBack;
back WHEN SIZE OF L_ZonesFront < SIZE OF L_ZonesBack;
L_ZonesFront LIST(tab:Test);
L_ZonesBack LIST(tab:Test);

idle DEFAULT;

ENTER fixing {
    last_bitset := BITSET FROM L_Zones;
    CLEAR L_ZonesFront; CLEAR L_ZonesBack;
    COPY ALL FROM L_Zones TO L_ZonesFront 
        WHERE (L_Zones.ITEM.zone + -1)%9 < 3 AND L_Zones.ITEM != off;
    COPY ALL FROM L_Zones TO L_ZonesBack 
        WHERE (L_Zones.ITEM.zone + -1)%9 >=6 AND L_Zones.ITEM != off;
    NumFront := SIZE OF L_ZonesFront;
    NumBack := SIZE OF L_ZonesBack;
    SET SELF TO idle;
}

COMMAND run { CALL fixing_on_enter ON SELF }
}
test TestFormula(tab:Test) zones;

