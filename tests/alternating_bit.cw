# This demonstrates the alternating bit protocol
#
# The sender (A) sends a value and a bit continuously
# when the sender receives a matching bit, A toggles
# its bit and gets a new value to send
#
# The receiver (B) sends a bit value continuously.
# When the receiver gets a message from A it sets its
# bit value to newly received value and processes the message

# message queues
msgs LIST; # A uses this to send a message,bit to B
acks LIST; # B users this to send a bit to A

A Sender msgs, acks;
B Receiver msgs, acks;

Sender MACHINE sendQ, recvQ {
    OPTION message "";
    OPTION bit 1;
    OPTION received "";

    ENTER INIT { message := "" + NOW + "," + bit; }

    timer Timer(timestep: 550);

    ack WHEN received == bit;
    send WHEN timer IS on;
    receive WHEN SELF IS idle AND recvQ IS nonempty;
    idle DEFAULT;

    ENTER ack {
        bit := 1 - bit;
        message := NOW;
        LOG "next message: " + message;
        message := "" + message + "," + bit;
    }

    ENTER send {
        SET timer TO off;
        PUSH message TO sendQ;
    }

    ENTER receive {
        x := TAKE FIRST FROM recvQ;
        received := x AS INTEGER;
    }
}


Receiver MACHINE recvQ, sendQ {
    OPTION received "";
    OPTION bit 0;

    timer Timer(timestep: 410);

    send WHEN timer IS on;
    receive WHEN SELF IS idle AND recvQ IS nonempty;
    idle DEFAULT;

    ENTER send {
        SET timer TO off;
        PUSH bit TO sendQ;
    }

    ENTER receive {
        x := TAKE FIRST FROM recvQ;
        received := COPY `([0-9]+)` FROM x;
        bit := COPY `.*,([0-9]*)` FROM x;
        LOG "got " + received;
    }
}

# A helper machine to trigger message sends in A and B
Timer MACHINE {
    OPTION timestep 1000;

    on WHEN SELF IS on || TIMER >= timestep;
    off DEFAULT;

    TRANSITION off TO on USING next;
}

