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

#include <iostream>
#include "Scheduler.h"
#include <stdlib.h>
#include <Message.h>

struct Test : public Receiver {
	Test(const char *name) : Receiver(name) { }
	bool receives(const Message &m, Transmitter *from) { return true; }
	void handle(const Message &m, Transmitter *from) {
		std::cout << "Received " << m << "\n";
	}
};


int main(int argc, char *argv[]) {
	Test t("t");
	ScheduledItem *new_item = new ScheduledItem(2000000, new Package(&t, &t, new Message("A")));
	Scheduler::instance()->add(new_item);
	new_item = new ScheduledItem(100000, new Package(&t, &t, new Message("B")));
	Scheduler::instance()->add(new_item);
	while (Scheduler::instance()->next()) {
		Scheduler::instance()->idle();
		std::cout << "." << std::flush;
		usleep(500);
	}
	return 0;
}
