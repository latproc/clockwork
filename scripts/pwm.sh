#!/bin/bash

EC=/opt/etherlab/bin/ethercat

pos=`$EC slaves | awk '$0 ~ /EL2535/{print $1}'`

device=0x1000:0x00
name=0x1008:0x00

ch1_ready_to_enable=0x6000:0x05

# The following dictionary was generated with
#cat pwm_sdos.txt | grep '\"' | egrep -v SubIndex \
# | while read a b; do echo $a; echo $b | tr ' ' '_' \
# | awk -F\" '{print $(NF-1)}' ; done \
# | awk '{ if (a!="") { b=$0; print b,a; a="" }else {a=$0}}' \
# | grep , | tr -d , | sed 's/:/:0x/;s/ /=/'
Device_type=0x1000:0x00
Device_name=0x1008:0x00
Hardware_version=0x1009:0x00
Software_version=0x100a:0x00
Vendor_ID=0x1018:0x01
Product_code=0x1018:0x02
Revision=0x1018:0x03
Serial_number=0x1018:0x04
Checksum=0x10f0:0x01
Sync_mode=0x1c32:0x01

Cycle_time=0x1c32:0x02
Shift_time=0x1c32:0x03
Sync_modes_supported=0x1c32:0x04
Minimum_cycle_time=0x1c32:0x05
Calc_and_copy_time=0x1c32:0x06
Minimum_delay_time=0x1c32:0x07
Command=0x1c32:0x08
Maximum_delay_time=0x1c32:0x09
SM_event_missed_counter=0x1c32:0x0b
Cycle_exceeded_counter=0x1c32:0x0c
Shift_too_short_counter=0x1c32:0x0d
Sync_error=0x1c32:0x20
Sync_mode=0x1c33:0x01

ch2_Cycle_time=0x1c33:0x02
ch2_Shift_time=0x1c33:0x03
ch2_Sync_modes_supported=0x1c33:0x04
ch2_Minimum_cycle_time=0x1c33:0x05
ch2_Calc_and_copy_time=0x1c33:0x06
ch2_Minimum_delay_time=0x1c33:0x07
ch2_Command=0x1c33:0x08
ch2_Maximum_delay_time=0x1c33:0x09
ch2_SM_event_missed_counter=0x1c33:0x0b
ch2_Cycle_exceeded_counter=0x1c33:0x0c
ch2_Shift_too_short_counter=0x1c33:0x0d
ch2_Sync_error=0x1c33:0x20

Digital_input_1=0x6000:0x01
Ready_to_enable=0x6000:0x05
Warning=0x6000:0x06
Error=0x6000:0x07
TxPDO_Toggle=0x6000:0x10
Info_data_1=0x6000:0x11
Info_data_2=0x6000:0x12

ch2_Digital_input_1=0x6010:0x01
ch2_Ready_to_enable=0x6010:0x05
ch2_Warning=0x6010:0x06
ch2_Error=0x6010:0x07
ch2_TxPDO_Toggle=0x6010:0x10
ch2_Info_data_1=0x6010:0x11
ch2_Info_data_2=0x6010:0x12

Enable_dithering=0x7000:0x01
Enable=0x7000:0x06
Reset=0x7000:0x07
PWM_output=0x7000:0x11

ch2_Enable_dithering=0x7010:0x01
ch2_Enable=0x7010:0x06
ch2_Reset=0x7010:0x07
ch2_PWM_output=0x7010:0x11

Enable_dithering=0x8000:0x03
Invert_polarity=0x8000:0x04
Watchdog=0x8000:0x05
Enable_feedforward=0x8000:0x09
Offset=0x8000:0x0b
Gain=0x8000:0x0c
Default_output=0x8000:0x0d
Default_output_ramp=0x8000:0x0e
Max_current_percent=0x8000:0x10
Kp_factor=0x8000:0x12
Ki_factor=0x8000:0x13
Kd_factor=0x8000:0x14
Coil_resistance_01_Ohm=0x8000:0x19
Dithering_frequency_Hz=0x8000:0x1e
Dithering_amplitude_percent=0x8000:0x1f
Select_info_data_1=0x8000:0x21
Select_info_data_2=0x8000:0x22
Offset=0x800f:0x01
Gain=0x800f:0x02
Nominal_current=0x800f:0x06

ch2_Enable_dithering=0x8010:0x03
ch2_Invert_polarity=0x8010:0x04
ch2_Watchdog=0x8010:0x05
ch2_Enable_feedforward=0x8010:0x09
ch2_Offset=0x8010:0x0b
ch2_Gain=0x8010:0x0c
ch2_Default_output=0x8010:0x0d
ch2_Default_output_ramp=0x8010:0x0e
ch2_Max_current_percent=0x8010:0x10
ch2_Kp_factor=0x8010:0x12
ch2_Ki_factor=0x8010:0x13
ch2_Kd_factor=0x8010:0x14
ch2_Coil_resistance_01_Ohm=0x8010:0x19
ch2_Dithering_frequency_Hz=0x8010:0x1e
ch2_Dithering_amplitude_percent=0x8010:0x1f
ch2_Select_info_data_1=0x8010:0x21
ch2_Select_info_data_2=0x8010:0x22
ch2_Offset=0x801f:0x01
ch2_Gain=0x801f:0x02
ch2_Nominal_current=0x801f:0x06

Overtemperature=0xa000:0x02
Undervoltage=0xa000:0x04
Overvoltage=0xa000:0x05
Short_circuit=0xa000:0x06

ch2_Overtemperature=0xa010:0x02
ch2_Undervoltage=0xa010:0x04
ch2_Overvoltage=0xa010:0x05
ch2_Short_circuit=0xa010:0x06

Module_index_distance=0xf000:0x01
Maximum_number_of_modules=0xf000:0x02
Code_word=0xf008:0x00
Voltage_mV=0xf900:0x01
Temperature_C=0xf900:0x02
Request=0xfb00:0x01
Status=0xfb00:0x02
Response=0xfb00:0x03


function show {
args=`echo $2 | tr : ' '`
echo "$1 " `$EC upload -p $pos $args`
}

# cat pwm.sh | grep = | sed 's/=.*//' | while read a; do echo 'show "'$a'" $'$a; done
show "Device_type" $Device_type
show "Device_name" $Device_name
show "Hardware_version" $Hardware_version
show "Software_version" $Software_version
show "Vendor_ID" $Vendor_ID
show "Product_code" $Product_code
show "Revision" $Revision
show "Serial_number" $Serial_number
show "Checksum" $Checksum
show "Sync_mode" $Sync_mode
show "Cycle_time" $Cycle_time
show "Shift_time" $Shift_time
show "Sync_modes_supported" $Sync_modes_supported
show "Minimum_cycle_time" $Minimum_cycle_time
show "Calc_and_copy_time" $Calc_and_copy_time
show "Minimum_delay_time" $Minimum_delay_time
show "Command" $Command
show "Maximum_delay_time" $Maximum_delay_time
show "SM_event_missed_counter" $SM_event_missed_counter
show "Cycle_exceeded_counter" $Cycle_exceeded_counter
show "Shift_too_short_counter" $Shift_too_short_counter
show "Sync_error" $Sync_error
show "Sync_mode" $Sync_mode
show "ch2_Cycle_time" $ch2_Cycle_time
show "ch2_Shift_time" $ch2_Shift_time
show "ch2_Sync_modes_supported" $ch2_Sync_modes_supported
show "ch2_Minimum_cycle_time" $ch2_Minimum_cycle_time
show "ch2_Calc_and_copy_time" $ch2_Calc_and_copy_time
show "ch2_Minimum_delay_time" $ch2_Minimum_delay_time
show "ch2_Command" $ch2_Command
show "ch2_Maximum_delay_time" $ch2_Maximum_delay_time
show "ch2_SM_event_missed_counter" $ch2_SM_event_missed_counter
show "ch2_Cycle_exceeded_counter" $ch2_Cycle_exceeded_counter
show "ch2_Shift_too_short_counter" $ch2_Shift_too_short_counter
show "ch2_Sync_error" $ch2_Sync_error
show "Digital_input_1" $Digital_input_1
show "Ready_to_enable" $Ready_to_enable
show "Warning" $Warning
show "Error" $Error
show "TxPDO_Toggle" $TxPDO_Toggle
show "Info_data_1" $Info_data_1
show "Info_data_2" $Info_data_2
show "ch2_Digital_input_1" $ch2_Digital_input_1
show "ch2_Ready_to_enable" $ch2_Ready_to_enable
show "ch2_Warning" $ch2_Warning
show "ch2_Error" $ch2_Error
show "ch2_TxPDO_Toggle" $ch2_TxPDO_Toggle
show "ch2_Info_data_1" $ch2_Info_data_1
show "ch2_Info_data_2" $ch2_Info_data_2
show "Enable_dithering" $Enable_dithering
show "Enable" $Enable
show "Reset" $Reset
show "PWM_output" $PWM_output
show "ch2_Enable_dithering" $ch2_Enable_dithering
show "ch2_Enable" $ch2_Enable
show "ch2_Reset" $ch2_Reset
show "ch2_PWM_output" $ch2_PWM_output
show "Enable_dithering" $Enable_dithering
show "Invert_polarity" $Invert_polarity
show "Watchdog" $Watchdog
show "Enable_feedforward" $Enable_feedforward
show "Offset" $Offset
show "Gain" $Gain
show "Default_output" $Default_output
show "Default_output_ramp" $Default_output_ramp
show "Max_current_percent" $Max_current_percent
show "Kp_factor" $Kp_factor
show "Ki_factor" $Ki_factor
show "Kd_factor" $Kd_factor
show "Coil_resistance_01_Ohm" $Coil_resistance_01_Ohm
show "Dithering_frequency_Hz" $Dithering_frequency_Hz
show "Dithering_amplitude_percent" $Dithering_amplitude_percent
show "Select_info_data_1" $Select_info_data_1
show "Select_info_data_2" $Select_info_data_2
show "Offset" $Offset
show "Gain" $Gain
show "Nominal_current" $Nominal_current
show "ch2_Enable_dithering" $ch2_Enable_dithering
show "ch2_Invert_polarity" $ch2_Invert_polarity
show "ch2_Watchdog" $ch2_Watchdog
show "ch2_Enable_feedforward" $ch2_Enable_feedforward
show "ch2_Offset" $ch2_Offset
show "ch2_Gain" $ch2_Gain
show "ch2_Default_output" $ch2_Default_output
show "ch2_Default_output_ramp" $ch2_Default_output_ramp
show "ch2_Max_current_percent" $ch2_Max_current_percent
show "ch2_Kp_factor" $ch2_Kp_factor
show "ch2_Ki_factor" $ch2_Ki_factor
show "ch2_Kd_factor" $ch2_Kd_factor
show "ch2_Coil_resistance_01_Ohm" $ch2_Coil_resistance_01_Ohm
show "ch2_Dithering_frequency_Hz" $ch2_Dithering_frequency_Hz
show "ch2_Dithering_amplitude_percent" $ch2_Dithering_amplitude_percent
show "ch2_Select_info_data_1" $ch2_Select_info_data_1
show "ch2_Select_info_data_2" $ch2_Select_info_data_2
show "ch2_Offset" $ch2_Offset
show "ch2_Gain" $ch2_Gain
show "ch2_Nominal_current" $ch2_Nominal_current
show "Overtemperature" $Overtemperature
show "Undervoltage" $Undervoltage
show "Overvoltage" $Overvoltage
show "Short_circuit" $Short_circuit
show "ch2_Overtemperature" $ch2_Overtemperature
show "ch2_Undervoltage" $ch2_Undervoltage
show "ch2_Overvoltage" $ch2_Overvoltage
show "ch2_Short_circuit" $ch2_Short_circuit
show "Module_index_distance" $Module_index_distance
show "Maximum_number_of_modules" $Maximum_number_of_modules
show "Code_word" $Code_word
show "Voltage_mV" $Voltage_mV
show "Temperature_C" $Temperature_C

