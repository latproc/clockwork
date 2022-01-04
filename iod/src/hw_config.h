/*
    Copyright (C) 2012 Martin Leadbeater, Michael O'Connor

    This file is part of Latproc

    Latproc is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    Latproc is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Latproc; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifndef TEST
/*  Master 0, Slave 1, "EL1008"
    Vendor ID:       0x00000002
    Product code:    0x03f03052
    Revision number: 0x00100000
*/

ec_pdo_entry_info_t slave_1_pdo_entries[] = {
    {0x6000, 0x01, 1}, /* Input */
    {0x6010, 0x01, 1}, /* Input */
    {0x6020, 0x01, 1}, /* Input */
    {0x6030, 0x01, 1}, /* Input */
    {0x6040, 0x01, 1}, /* Input */
    {0x6050, 0x01, 1}, /* Input */
    {0x6060, 0x01, 1}, /* Input */
    {0x6070, 0x01, 1}, /* Input */
};

ec_pdo_info_t slave_1_pdos[] = {
    {0x1a00, 1, slave_1_pdo_entries + 0}, /* Channel 1 */
    {0x1a01, 1, slave_1_pdo_entries + 1}, /* Channel 2 */
    {0x1a02, 1, slave_1_pdo_entries + 2}, /* Channel 3 */
    {0x1a03, 1, slave_1_pdo_entries + 3}, /* Channel 4 */
    {0x1a04, 1, slave_1_pdo_entries + 4}, /* Channel 5 */
    {0x1a05, 1, slave_1_pdo_entries + 5}, /* Channel 6 */
    {0x1a06, 1, slave_1_pdo_entries + 6}, /* Channel 7 */
    {0x1a07, 1, slave_1_pdo_entries + 7}, /* Channel 8 */
};

ec_sync_info_t slave_1_syncs[] = {{0, EC_DIR_INPUT, 8, slave_1_pdos + 0, EC_WD_DISABLE}, {0xff}};

/*  Master 0, Slave 2, "EL2008"
    Vendor ID:       0x00000002
    Product code:    0x07d83052
    Revision number: 0x00100000
*/

ec_pdo_entry_info_t slave_2_pdo_entries[] = {
    {0x7000, 0x01, 1}, /* Output */
    {0x7010, 0x01, 1}, /* Output */
    {0x7020, 0x01, 1}, /* Output */
    {0x7030, 0x01, 1}, /* Output */
    {0x7040, 0x01, 1}, /* Output */
    {0x7050, 0x01, 1}, /* Output */
    {0x7060, 0x01, 1}, /* Output */
    {0x7070, 0x01, 1}, /* Output */
};

ec_pdo_info_t slave_2_pdos[] = {
    {0x1600, 1, slave_2_pdo_entries + 0}, /* Channel 1 */
    {0x1601, 1, slave_2_pdo_entries + 1}, /* Channel 2 */
    {0x1602, 1, slave_2_pdo_entries + 2}, /* Channel 3 */
    {0x1603, 1, slave_2_pdo_entries + 3}, /* Channel 4 */
    {0x1604, 1, slave_2_pdo_entries + 4}, /* Channel 5 */
    {0x1605, 1, slave_2_pdo_entries + 5}, /* Channel 6 */
    {0x1606, 1, slave_2_pdo_entries + 6}, /* Channel 7 */
    {0x1607, 1, slave_2_pdo_entries + 7}, /* Channel 8 */
};

ec_sync_info_t slave_2_syncs[] = {{0, EC_DIR_OUTPUT, 8, slave_2_pdos + 0, EC_WD_ENABLE}, {0xff}};

/*  Master 0, Slave 3, "EK1814"
    Vendor ID:       0x00000002
    Product code:    0x07162c52
    Revision number: 0x00100000
*/

ec_pdo_entry_info_t slave_3_pdo_entries[] = {
    {0x7080, 0x01, 1}, /* Output */
    {0x7090, 0x01, 1}, /* Output */
    {0x70a0, 0x01, 1}, /* Output */
    {0x70b0, 0x01, 1}, /* Output */
    {0x6000, 0x01, 1}, /* Input */
    {0x6010, 0x01, 1}, /* Input */
    {0x6020, 0x01, 1}, /* Input */
    {0x6030, 0x01, 1}, /* Input */
};

ec_pdo_info_t slave_3_pdos[] = {
    {0x1608, 1, slave_3_pdo_entries + 0}, /* Channel 5 */
    {0x1609, 1, slave_3_pdo_entries + 1}, /* Channel 6 */
    {0x160a, 1, slave_3_pdo_entries + 2}, /* Channel 7 */
    {0x160b, 1, slave_3_pdo_entries + 3}, /* Channel 8 */
    {0x1a00, 1, slave_3_pdo_entries + 4}, /* Channel 1 */
    {0x1a01, 1, slave_3_pdo_entries + 5}, /* Channel 2 */
    {0x1a02, 1, slave_3_pdo_entries + 6}, /* Channel 3 */
    {0x1a03, 1, slave_3_pdo_entries + 7}, /* Channel 4 */
};

ec_sync_info_t slave_3_syncs[] = {{0, EC_DIR_OUTPUT, 4, slave_3_pdos + 0, EC_WD_ENABLE},
                                  {1, EC_DIR_INPUT, 4, slave_3_pdos + 4, EC_WD_DISABLE},
                                  {0xff}};

#else
/*  Master 0, Slave 0, "EK1814"
    Vendor ID:       0x00000002
    Product code:    0x07162c52
    Revision number: 0x00110000
*/

ec_pdo_entry_info_t slave_0_pdo_entries[] = {
    {0x7080, 0x01, 1}, /* Output */
    {0x7090, 0x01, 1}, /* Output */
    {0x70a0, 0x01, 1}, /* Output */
    {0x70b0, 0x01, 1}, /* Output */
    {0x6000, 0x01, 1}, /* Input */
    {0x6010, 0x01, 1}, /* Input */
    {0x6020, 0x01, 1}, /* Input */
    {0x6030, 0x01, 1}, /* Input */
};

ec_pdo_info_t slave_0_pdos[] = {
    {0x1608, 1, slave_0_pdo_entries + 0}, /* Channel 5 */
    {0x1609, 1, slave_0_pdo_entries + 1}, /* Channel 6 */
    {0x160a, 1, slave_0_pdo_entries + 2}, /* Channel 7 */
    {0x160b, 1, slave_0_pdo_entries + 3}, /* Channel 8 */
    {0x1a00, 1, slave_0_pdo_entries + 4}, /* Channel 1 */
    {0x1a01, 1, slave_0_pdo_entries + 5}, /* Channel 2 */
    {0x1a02, 1, slave_0_pdo_entries + 6}, /* Channel 3 */
    {0x1a03, 1, slave_0_pdo_entries + 7}, /* Channel 4 */
};

ec_sync_info_t slave_0_syncs[] = {{0, EC_DIR_OUTPUT, 4, slave_0_pdos + 0, EC_WD_ENABLE},
                                  {1, EC_DIR_INPUT, 4, slave_0_pdos + 4, EC_WD_DISABLE},
                                  {0xff}};

/*  Master 0, Slave 1, "EL1819"
    Vendor ID:       0x00000002
    Product code:    0x071b3052
    Revision number: 0x00100000
*/

ec_pdo_entry_info_t slave_1_pdo_entries[] = {
    {0x6000, 0x01, 1}, /* Input */
    {0x6010, 0x01, 1}, /* Input */
    {0x6020, 0x01, 1}, /* Input */
    {0x6030, 0x01, 1}, /* Input */
    {0x6040, 0x01, 1}, /* Input */
    {0x6050, 0x01, 1}, /* Input */
    {0x6060, 0x01, 1}, /* Input */
    {0x6070, 0x01, 1}, /* Input */
    {0x6080, 0x01, 1}, /* Input */
    {0x6090, 0x01, 1}, /* Input */
    {0x60a0, 0x01, 1}, /* Input */
    {0x60b0, 0x01, 1}, /* Input */
    {0x60c0, 0x01, 1}, /* Input */
    {0x60d0, 0x01, 1}, /* Input */
    {0x60e0, 0x01, 1}, /* Input */
    {0x60f0, 0x01, 1}, /* Input */
};

ec_pdo_info_t slave_1_pdos[] = {
    {0x1a00, 1, slave_1_pdo_entries + 0},  /* Channel 1 */
    {0x1a01, 1, slave_1_pdo_entries + 1},  /* Channel 2 */
    {0x1a02, 1, slave_1_pdo_entries + 2},  /* Channel 3 */
    {0x1a03, 1, slave_1_pdo_entries + 3},  /* Channel 4 */
    {0x1a04, 1, slave_1_pdo_entries + 4},  /* Channel 5 */
    {0x1a05, 1, slave_1_pdo_entries + 5},  /* Channel 6 */
    {0x1a06, 1, slave_1_pdo_entries + 6},  /* Channel 7 */
    {0x1a07, 1, slave_1_pdo_entries + 7},  /* Channel 8 */
    {0x1a08, 1, slave_1_pdo_entries + 8},  /* Channel 9 */
    {0x1a09, 1, slave_1_pdo_entries + 9},  /* Channel 10 */
    {0x1a0a, 1, slave_1_pdo_entries + 10}, /* Channel 11 */
    {0x1a0b, 1, slave_1_pdo_entries + 11}, /* Channel 12 */
    {0x1a0c, 1, slave_1_pdo_entries + 12}, /* Channel 13 */
    {0x1a0d, 1, slave_1_pdo_entries + 13}, /* Channel 14 */
    {0x1a0e, 1, slave_1_pdo_entries + 14}, /* Channel 15 */
    {0x1a0f, 1, slave_1_pdo_entries + 15}, /* Channel 16 */
};

ec_sync_info_t slave_1_syncs[] = {{0, EC_DIR_INPUT, 16, slave_1_pdos + 0, EC_WD_DISABLE}, {0xff}};

/*  Master 0, Slave 2, "EL1819"
    Vendor ID:       0x00000002
    Product code:    0x071b3052
    Revision number: 0x00100000
*/

ec_pdo_entry_info_t slave_2_pdo_entries[] = {
    {0x6000, 0x01, 1}, /* Input */
    {0x6010, 0x01, 1}, /* Input */
    {0x6020, 0x01, 1}, /* Input */
    {0x6030, 0x01, 1}, /* Input */
    {0x6040, 0x01, 1}, /* Input */
    {0x6050, 0x01, 1}, /* Input */
    {0x6060, 0x01, 1}, /* Input */
    {0x6070, 0x01, 1}, /* Input */
    {0x6080, 0x01, 1}, /* Input */
    {0x6090, 0x01, 1}, /* Input */
    {0x60a0, 0x01, 1}, /* Input */
    {0x60b0, 0x01, 1}, /* Input */
    {0x60c0, 0x01, 1}, /* Input */
    {0x60d0, 0x01, 1}, /* Input */
    {0x60e0, 0x01, 1}, /* Input */
    {0x60f0, 0x01, 1}, /* Input */
};

ec_pdo_info_t slave_2_pdos[] = {
    {0x1a00, 1, slave_2_pdo_entries + 0},  /* Channel 1 */
    {0x1a01, 1, slave_2_pdo_entries + 1},  /* Channel 2 */
    {0x1a02, 1, slave_2_pdo_entries + 2},  /* Channel 3 */
    {0x1a03, 1, slave_2_pdo_entries + 3},  /* Channel 4 */
    {0x1a04, 1, slave_2_pdo_entries + 4},  /* Channel 5 */
    {0x1a05, 1, slave_2_pdo_entries + 5},  /* Channel 6 */
    {0x1a06, 1, slave_2_pdo_entries + 6},  /* Channel 7 */
    {0x1a07, 1, slave_2_pdo_entries + 7},  /* Channel 8 */
    {0x1a08, 1, slave_2_pdo_entries + 8},  /* Channel 9 */
    {0x1a09, 1, slave_2_pdo_entries + 9},  /* Channel 10 */
    {0x1a0a, 1, slave_2_pdo_entries + 10}, /* Channel 11 */
    {0x1a0b, 1, slave_2_pdo_entries + 11}, /* Channel 12 */
    {0x1a0c, 1, slave_2_pdo_entries + 12}, /* Channel 13 */
    {0x1a0d, 1, slave_2_pdo_entries + 13}, /* Channel 14 */
    {0x1a0e, 1, slave_2_pdo_entries + 14}, /* Channel 15 */
    {0x1a0f, 1, slave_2_pdo_entries + 15}, /* Channel 16 */
};

ec_sync_info_t slave_2_syncs[] = {{0, EC_DIR_INPUT, 16, slave_2_pdos + 0, EC_WD_DISABLE}, {0xff}};

/*  Master 0, Slave 3, "EL2809"
    Vendor ID:       0x00000002
    Product code:    0x0af93052
    Revision number: 0x00100000
*/

ec_pdo_entry_info_t slave_3_pdo_entries[] = {
    {0x7000, 0x01, 1}, /* Output */
    {0x7010, 0x01, 1}, /* Output */
    {0x7020, 0x01, 1}, /* Output */
    {0x7030, 0x01, 1}, /* Output */
    {0x7040, 0x01, 1}, /* Output */
    {0x7050, 0x01, 1}, /* Output */
    {0x7060, 0x01, 1}, /* Output */
    {0x7070, 0x01, 1}, /* Output */
    {0x7080, 0x01, 1}, /* Output */
    {0x7090, 0x01, 1}, /* Output */
    {0x70a0, 0x01, 1}, /* Output */
    {0x70b0, 0x01, 1}, /* Output */
    {0x70c0, 0x01, 1}, /* Output */
    {0x70d0, 0x01, 1}, /* Output */
    {0x70e0, 0x01, 1}, /* Output */
    {0x70f0, 0x01, 1}, /* Output */
};

ec_pdo_info_t slave_3_pdos[] = {
    {0x1600, 1, slave_3_pdo_entries + 0},  /* Channel 1 */
    {0x1601, 1, slave_3_pdo_entries + 1},  /* Channel 2 */
    {0x1602, 1, slave_3_pdo_entries + 2},  /* Channel 3 */
    {0x1603, 1, slave_3_pdo_entries + 3},  /* Channel 4 */
    {0x1604, 1, slave_3_pdo_entries + 4},  /* Channel 5 */
    {0x1605, 1, slave_3_pdo_entries + 5},  /* Channel 6 */
    {0x1606, 1, slave_3_pdo_entries + 6},  /* Channel 7 */
    {0x1607, 1, slave_3_pdo_entries + 7},  /* Channel 8 */
    {0x1608, 1, slave_3_pdo_entries + 8},  /* Channel 9 */
    {0x1609, 1, slave_3_pdo_entries + 9},  /* Channel 10 */
    {0x160a, 1, slave_3_pdo_entries + 10}, /* Channel 11 */
    {0x160b, 1, slave_3_pdo_entries + 11}, /* Channel 12 */
    {0x160c, 1, slave_3_pdo_entries + 12}, /* Channel 13 */
    {0x160d, 1, slave_3_pdo_entries + 13}, /* Channel 14 */
    {0x160e, 1, slave_3_pdo_entries + 14}, /* Channel 15 */
    {0x160f, 1, slave_3_pdo_entries + 15}, /* Channel 16 */
};

ec_sync_info_t slave_3_syncs[] = {{0, EC_DIR_OUTPUT, 8, slave_3_pdos + 0, EC_WD_ENABLE},
                                  {1, EC_DIR_OUTPUT, 8, slave_3_pdos + 8, EC_WD_ENABLE},
                                  {0xff}};

/*  Master 0, Slave 4, "EL2024"
    Vendor ID:       0x00000002
    Product code:    0x07e83052
    Revision number: 0x00100000
*/

ec_pdo_entry_info_t slave_4_pdo_entries[] = {
    {0x7000, 0x01, 1}, /* Output */
    {0x7010, 0x01, 1}, /* Output */
    {0x7020, 0x01, 1}, /* Output */
    {0x7030, 0x01, 1}, /* Output */
};

ec_pdo_info_t slave_4_pdos[] = {
    {0x1600, 1, slave_4_pdo_entries + 0}, /* Channel 1 */
    {0x1601, 1, slave_4_pdo_entries + 1}, /* Channel 2 */
    {0x1602, 1, slave_4_pdo_entries + 2}, /* Channel 3 */
    {0x1603, 1, slave_4_pdo_entries + 3}, /* Channel 4 */
};

ec_sync_info_t slave_4_syncs[] = {{0, EC_DIR_OUTPUT, 4, slave_4_pdos + 0, EC_WD_ENABLE}, {0xff}};

/*  Master 0, Slave 5, "EL2024"
    Vendor ID:       0x00000002
    Product code:    0x07e83052
    Revision number: 0x00100000
*/

ec_pdo_entry_info_t slave_5_pdo_entries[] = {
    {0x7000, 0x01, 1}, /* Output */
    {0x7010, 0x01, 1}, /* Output */
    {0x7020, 0x01, 1}, /* Output */
    {0x7030, 0x01, 1}, /* Output */
};

ec_pdo_info_t slave_5_pdos[] = {
    {0x1600, 1, slave_5_pdo_entries + 0}, /* Channel 1 */
    {0x1601, 1, slave_5_pdo_entries + 1}, /* Channel 2 */
    {0x1602, 1, slave_5_pdo_entries + 2}, /* Channel 3 */
    {0x1603, 1, slave_5_pdo_entries + 3}, /* Channel 4 */
};

ec_sync_info_t slave_5_syncs[] = {{0, EC_DIR_OUTPUT, 4, slave_5_pdos + 0, EC_WD_ENABLE}, {0xff}};

/*  Master 0, Slave 6, "EL2024"
    Vendor ID:       0x00000002
    Product code:    0x07e83052
    Revision number: 0x00100000
*/

ec_pdo_entry_info_t slave_6_pdo_entries[] = {
    {0x7000, 0x01, 1}, /* Output */
    {0x7010, 0x01, 1}, /* Output */
    {0x7020, 0x01, 1}, /* Output */
    {0x7030, 0x01, 1}, /* Output */
};

ec_pdo_info_t slave_6_pdos[] = {
    {0x1600, 1, slave_6_pdo_entries + 0}, /* Channel 1 */
    {0x1601, 1, slave_6_pdo_entries + 1}, /* Channel 2 */
    {0x1602, 1, slave_6_pdo_entries + 2}, /* Channel 3 */
    {0x1603, 1, slave_6_pdo_entries + 3}, /* Channel 4 */
};

ec_sync_info_t slave_6_syncs[] = {{0, EC_DIR_OUTPUT, 4, slave_6_pdos + 0, EC_WD_ENABLE}, {0xff}};

/*  Master 0, Slave 7, "EL2024"
    Vendor ID:       0x00000002
    Product code:    0x07e83052
    Revision number: 0x00100000
*/

ec_pdo_entry_info_t slave_7_pdo_entries[] = {
    {0x7000, 0x01, 1}, /* Output */
    {0x7010, 0x01, 1}, /* Output */
    {0x7020, 0x01, 1}, /* Output */
    {0x7030, 0x01, 1}, /* Output */
};

ec_pdo_info_t slave_7_pdos[] = {
    {0x1600, 1, slave_7_pdo_entries + 0}, /* Channel 1 */
    {0x1601, 1, slave_7_pdo_entries + 1}, /* Channel 2 */
    {0x1602, 1, slave_7_pdo_entries + 2}, /* Channel 3 */
    {0x1603, 1, slave_7_pdo_entries + 3}, /* Channel 4 */
};

ec_sync_info_t slave_7_syncs[] = {{0, EC_DIR_OUTPUT, 4, slave_7_pdos + 0, EC_WD_ENABLE}, {0xff}};

#endif
