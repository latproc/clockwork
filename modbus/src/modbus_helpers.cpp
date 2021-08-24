#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include "modbus_helpers.h"

int getSettings(const char *str, SerialSettings &settings) {
	int result = 0;
	char *buf = strdup(str);
	char *fld, *p = buf;
	enum SettingsStates { cs_baud, cs_bits, cs_parity, cs_stop, cs_end } state = cs_baud;

	while (state != cs_end) {
		fld = strsep(&p, ":");
		if (!fld) goto done_getSettings; // no more fields
        if (*fld == 0) continue; // skip blank fields

		char *tmp = 0;
		// most fields are numbers so we usually attempt to convert the field to a number
		long val = 8;
		if (state != cs_parity) {
            val = strtol(fld, &tmp, 10);
            if (tmp == 0) {
                result = -1; // no valid conversion
                break;
            }
            if (*tmp && *tmp != ':') {
                result = 1; // unexpected trailing data
                break;
            }
        }
        switch(state) {
            case cs_baud:
                settings.baud = val;
                state = cs_bits;
                break;
            case cs_bits:
                if (val == 8)
                    settings.bits = 8;
                else if (val == 7)
                    settings.bits = 7;
                state = cs_parity;
                break;
            case cs_parity:
                settings.parity = toupper(*fld);
                state = cs_stop;
                break;
            case cs_stop:
                if (val == 2)
                    settings.stop_bits = 2;
                else if (val == 1)
                    settings.stop_bits = 1;
                break;
                state = cs_end;
            case cs_end:
            default: ;
        }
	}
done_getSettings:
	free(buf);
	return result;
}

bool isPrintable(const char *str) {
	if (!str) return false;
	while (*str)
		if (!isprint(*str++)) { return false; }
	return true;
}
