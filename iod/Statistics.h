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

#ifndef __STATISTICS_H
#define __STATISTICS_H 1

#include "Statistic.h"

struct Statistics {
	Statistic io_scan_time;
	Statistic points_processing;
	Statistic machine_processing;
	Statistic dispatch_processing;
	Statistic auto_states;	
	Statistic web_processing;

	Statistics() : 
	        io_scan_time("I/O Scan            "),
	   points_processing("POINTS sync         "),
	  machine_processing("Machine Processing  "),
	 dispatch_processing("Dispatch Processing "),
	         auto_states("Stable States       "),
	      web_processing("Web Processing      ")
	{}
	
};

#endif
