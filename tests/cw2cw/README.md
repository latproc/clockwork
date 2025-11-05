# Scenario

Machines, a and b instantiate a FlasherOne machine and
machine a also specifies a 'Link' that is a channel configuration
using a FlasherOneInterface to define the objects that are
shared between the two machines. A FlasherOne is a machine
that blinks between on and off once it has been started

To run this scenario, use the following:
```bash
Run a: cw -cp 10001 -c iod.conf common a
Run b: cw -cp 5555 -c iod.conf common b
```
