# Fixture for the persist load filter (IOD_PERSIST_FILTER).
#
# `declared` is set by the class, so a store value for it is always admitted.
# Its class default is 1 and the store says 2, so the log proves the store was
# read at all.
#
# `phantom` is never declared: it exists only in the .persist store, put there
# by an earlier revision of this program. It is admitted only when the filter is
# off (default) or dryrun; under enforce it must not reach the instance.
#
# The two markers are therefore:
#   declared=2            always
#   phantom=7             off and dryrun
#   phantom=phantom       enforce (the name resolves to itself, i.e. unset)
ProbeMachine MACHINE {
    OPTION PERSISTENT true;
    OPTION declared 1;
    ENTER INIT { LOG "PROBE declared=" + declared + " phantom=" + phantom; }
}

probe ProbeMachine;
