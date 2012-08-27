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

#ifndef latprocc_statistic_h
#define latprocc_statistic_h

#include <ostream>
#include <string>

class Statistic {
public:
    Statistic(const char *msg) : text(msg), sum(0), count(0), min_value() {};
    Statistic &operator=(const Statistic &other);
    std::ostream &operator<<(std::ostream &out) const;
    bool operator==(const Statistic &other);

	void add(long new_value) {
		++count;
		sum += new_value;
		if (new_value > max_value) max_value = new_value;
		if (new_value < min_value) min_value = new_value;
		ssq += new_value*new_value;
	}
	void report(std::ostream &out) {
		float ave = (count==0) ? 0 : sum/count;
		out << text << '\t' << count << '\t' << min_value << '\t' << max_value << '\t' << ave << "\n";
	}
#if 0	
	float rollingAverage(){
		int c=periods.size();
		if(c==0)return 0.0f;
		float s=0.0f;
		for(int i=0;i<c;i++){
			s+=average(i);
		}
			return s/c;
	}
#endif
	static void add(Statistic*new_stat) { stats.push_back(new_stat); }
	static void reportAll(std::ostream &out) {
		std::list<Statistic*>::iterator iter = stats.begin();
		while (iter != stats.end()) {
	 		Statistic *stat = *iter++;
			stat->report(out);
		}
	}
    
private:
    Statistic(const Statistic &orig);

    std::string text;
	float sum;
	int count;
	long min_value;
	long max_value;
	float ssq;
	static std::list<Statistic *> stats;
};

std::ostream &operator<<(std::ostream &out, const Statistic &m);

#endif
