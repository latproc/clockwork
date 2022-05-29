# A simple dungen explorer

/*
  A hunter starts at a random position and has to find the treasure.

  Movement is described by a series of unit-length steps in a direction.

  Movement requires a clear space in the given direction.

  Move the hunter in a random walk over the map
*/

hunter Hunter;
map Map;
mover Mover map, hunter;
random RandomWalk mover;

Map MACHINE {
  OPTION width 10;
  OPTION height 10;
}
Hunter MACHINE {
  OPTION x 0;
  OPTION y 0;
}

# The following would be better as conditions once they are available
LeftOk MACHINE map, hunter {
  on WHEN hunter.x > 0;
  off DEFAULT;
}

RightOk MACHINE map, hunter {
  on WHEN hunter.x < map.width - 1;
  off DEFAULT;
}

ForwardOk MACHINE map, hunter {
  on WHEN hunter.y < map.height - 1;
  off DEFAULT;
}

BackwardOk MACHINE map, hunter {
  on WHEN hunter.y > 0;
  off DEFAULT;
}

# Moves the hunter on the map by following commands
Mover MACHINE map, hunter {
  left_ok LeftOk map, hunter;
  right_ok RightOk map, hunter;
  forward_ok ForwardOk map, hunter;
  back_ok BackwardOk map, hunter;

  idle DEFAULT;
  moving_left STATE; ENTER moving_left{ hunter.x := hunter.x - 1; }
  moving_right STATE; ENTER moving_right { hunter.x := hunter.x+1; }
  moving_forward STATE; ENTER moving_forward { hunter.y := hunter.y+1; }
  moving_back STATE;ENTER moving_back { hunter.y := hunter.y - 1; }

  TRANSITION idle TO moving_left USING go_left REQUIRES left_ok IS on;
  TRANSITION idle TO moving_right USING go_right REQUIRES right_ok IS on;
  TRANSITION idle TO moving_forward USING go_forward REQUIRES forward_ok IS on;
  TRANSITION idle TO moving_back USING go_back REQUIRES back_ok IS on;

}

RandomWalk MACHINE mover {
  commands LIST "go_left", "go_right", "go_forward", "go_back";

  idle DEFAULT;
  move STATE;
  move WHEN SELF IS idle AND TIMER >= 1000;
  ENTER move {
    i := RANDOM %4;
    cmd := ITEM i OF commands;
    SEND cmd TO mover;
  }

}
