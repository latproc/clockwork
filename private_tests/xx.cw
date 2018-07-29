MOTORSTATUS MACHINE {
  OPTION status_msg "";
  OPTION status_cmp "";
  OPTION power 0;
  OPTION current 0;
  OPTION tempA 0;
  OPTION tempB 0;

  packet WHEN status_cmp != status_msg && status_msg MATCHES `Throttle: [0-9]* Current: [0-9]* TempA: [-]{0,1}[0-9]*\.[0-9]* TempB: [-]{0,1}[0-9]*\.[0-9]*`;
  good_reply WHEN status_cmp != status_msg && status_msg MATCHES `OK`;
  packet_error WHEN status_cmp != status_msg;
  wait WHEN status_cmp == status_msg;
  error DEFAULT;

  ENTER packet {
    msg := status_msg;
    tmp := COPY `Throttle: [0-9]*` FROM msg;
    power := COPY ALL `[0-9]` FROM tmp;

    tmp := COPY `Current: [0-9]*` FROM msg;
    current := COPY ALL `[0-9]` FROM tmp;

    tmp := COPY `TempA: [-]{0,1}[0-9]*\.[0-9]*` FROM msg;
    tempA := COPY ALL `[0-9]` FROM tmp;

    tmp := COPY `TempB: [-]{0,1}[0-9]*\.[0-9]*` FROM msg;
    tempB := COPY ALL `[0-9]` FROM tmp;

    status_cmp := msg;
    msg := "";
    tmp := "";
  }

  ENTER fix_lock {status_msg := "" }

  ENTER good_reply {status_cmp := status_msg}

}
Status MACHINE {
  OPTION tempA 2;
  OPTION tempB 1;
  ready  INITIAL;
}
TEMPDIFF MACHINE M_Status {
  OPTION diff 0;
  update WHEN diff != (M_Status.tempA - M_Status.tempB);
  waiting DEFAULT;

  ENTER update { diff := M_Status.tempA - M_Status.tempB; }

RECEIVE M_Status.wait_enter {
    tempA := M_Status.tempA;
    tempB := M_Status.tempB;
    diff := tempA - tempB;
  }
/*  RECEIVE M_Status.wait_enter {
    diff := M_Status.tempA - M_Status.tempB;
  }
*/
}
y Status;
x TEMPDIFF y;
