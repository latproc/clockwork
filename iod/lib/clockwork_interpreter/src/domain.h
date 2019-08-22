/* 
	NOTE: This file is an editted version of ../ethercat/master/domain.h
 */
/******************************************************************************
 *
 *  $Id$
 *
 *  Copyright (C) 2006-2008  Florian Pose, Ingenieurgemeinschaft IgH
 *
 *  This file is part of the IgH EtherCAT Master.
 *
 *  The IgH EtherCAT Master is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License version 2, as
 *  published by the Free Software Foundation.
 *
 *  The IgH EtherCAT Master is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 *  Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the IgH EtherCAT Master; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *  ---
 *
 *  The license mentioned above concerns the source code only. Using the
 *  EtherCAT technology and brand is only permitted in compliance with the
 *  industrial property and similar rights of Beckhoff Automation GmbH.
 *
 *****************************************************************************/

/**
   \file
   EtherCAT domain structure.
*/

/*****************************************************************************/

#ifndef __EC_DOMAIN_H__
#define __EC_DOMAIN_H__

//#include "linux/list.h"

//#include "globals.h"
//#include "datagram.h"
//#include "master.h"
//#include "fmmu_config.h"

/*****************************************************************************/

/** EtherCAT domain.
 *
 * Handles the process data and the therefore needed datagrams of a certain
 * group of slaves.
 */
struct ec_domain
{
    struct list_head list; /**< List item. */
    void *master; /**< EtherCAT master owning the domain. */
    unsigned int index; /**< Index (just a number). */

    struct list_head fmmu_configs; /**< FMMU configurations contained. */
    size_t data_size; /**< Size of the process data. */
    size_t tx_size; /**< Size of the transmitted data. */
    uint8_t *data; /**< Memory for the process data. */
    ec_origin_t data_origin; /**< Origin of the \a data memory. */
    uint32_t logical_base_address; /**< Logical offset address of the
                                     process data. */
    struct list_head datagrams; /**< Datagrams for process data exchange. */

    uint16_t working_counter; /**< Last working counter value. */
    uint16_t expected_working_counter; /**< Expected working counter. */
    unsigned int working_counter_changes; /**< Working counter changes
                                             since last notification. */
    unsigned long notify_jiffies; /**< Time of last notification. */
};

#endif
