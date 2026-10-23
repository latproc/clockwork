# VELOCITY is a first-class Beckhoff input. Its two parameters identify the
# module and PDO entry, the same addressing used by COUNTER.

EL5152_test MODULE (position:2);
axis_position COUNTER (tab:Input, Position:0) EL5152_test, 36;
axis_velocity VELOCITY (WindowMs:100, SampleMs:20, MinSamples:4, Scale:0.000192,
                        StaleMs:250, MaxAbsValue:1000) EL5152_test, 36;
