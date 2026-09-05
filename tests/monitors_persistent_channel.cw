# `PERSISTENT` is a soft keyword: a token only before OPTION (PERSISTENT OPTION),
# otherwise a plain property name. A channel can monitor the machine-level
# reserved "PERSISTENT" option.

PERSISTENCE_CHANNEL CHANNEL {
  OPTION host "localhost";
  OPTION port 7901;
  MONITORS PERSISTENT == "true";
  IGNORES STATE_CHANGES;
  PUBLISHER;
}
