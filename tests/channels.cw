# Some examples of how to check channel status
sampler_channels ChannelSearch(class: "SAMPLER_CHANNEL");

channels_ok ChannelsOk 1; # exactly one channel is expected

# A general test that all channels are active
ChannelsOk MACHINE expected_channels {
    GLOBAL CHANNELS;
    ok WHEN SIZE OF CHANNELS == expected_channels AND ALL CHANNELS ARE ACTIVE;
    waiting DEFAULT;
}

# Watch the channel list for channel of a given type (eg SAMPLER_CHANNEL)
# and add them to a list.
# Also, assign the first channel found to a reference.
# Recheck every time the number of channels changes.
ChannelSearch MACHINE {
    GLOBAL CHANNELS;
    OPTION class "";

    channels LIST; # All of the sampler channels
    channel REFERENCE; # The first sampler channel found

    LOCAL OPTION num_channels 0;

    idle DEFAULT;
    found WHEN channel IS ASSIGNED;
    checking WHEN num_channels != SIZE OF CHANNELS;

    ENTER checking {
        # getting a list of all sampler channels
        COPY ALL FROM CHANNELS TO channels
            WHERE CLASS OF CHANNELS.ITEM IS class;
        # assigning a reference to the first sampler channel found
        COPY 1 FROM CHANNELS TO channel
            WHERE CLASS OF CHANNELS.ITEM IS class;
    }

    ENTER found {
        LOG "channel: " + channel.ITEM.NAME;
    }
}

