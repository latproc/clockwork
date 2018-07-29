. vars

DIR=$(cd "$(dirname "$0")"; pwd)
echo $CW $TESTS/stdchannels.cw ${DIR}/shared/ ${DIR}/server/
sleep 1
$CW $TESTS/stdchannels.cw ${DIR}/shared/ ${DIR}/server/

#you can connect to the client with:  iosh -p 10001  and to the server with just iosh
#you can sample the client with sampler --cw-port 10001 and the server as normal
