#!/bin/sh

[ "$CWBIN" = "" ] && CWBIN=/opt/latproc/iod/

"$CWBIN"/cw stdchannels.cw coresim.cw &
cwpid=$!

"$CWBIN"/modbusd &
mbpid=$!

../core_panel

kill $mbpid
kill $cwpid

