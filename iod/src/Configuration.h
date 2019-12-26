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

#ifndef __cwlang_configuration_h__
#define __cwlang_configuration_h__

#include <string>
#include <map>

typedef std::map<std::string, std::string> ConfigurationMap;

class Configuration {
public:
  Configuration(std::string configuration_filename);
	const std::string &setting(std::string name) const;
	int asInt(std::string name) const;
private:
  ConfigurationMap settings;
	std::string missing;
};

#endif

