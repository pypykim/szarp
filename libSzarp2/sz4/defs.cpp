/* 
  SZARP: SCADA software 
  

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
*/

#include "sz4/defs.h"

#include "szarp_config.h"

#include <cmath> 

namespace sz4 {

bool value_is_no_data(const double& v) {
	return std::isnan(v);
}

bool value_is_no_data(const float& v) {
#ifndef MINGW32
	return isnanf(v);
#else
	return std::isnan(v);
#endif
}

template<> float no_data<float>() {
	return nanf("");
}

template<> unsigned short no_data<unsigned short>() {
	return std::numeric_limits<unsigned short>::max();
}

template<> unsigned int no_data<unsigned int>() {
	return std::numeric_limits<unsigned int>::max();
}

template<> double no_data<double>() {
	return nan("");
}

bool no_data_search_condition::operator()(const short& v) const {
	return v != std::numeric_limits<short>::min();
}

bool no_data_search_condition::operator()(const int& v) const {
	return v != std::numeric_limits<int>::min();
}

bool no_data_search_condition::operator()(const float& v) const {
#ifndef MINGW32
	return !isnanf(v);
#else
	return !std::isnan(v);
	#endif
}

bool no_data_search_condition::operator()(const double& v) const {
	return !std::isnan(v);
}

bool no_data_search_condition::operator()(const uint32_t& v) const {
	return v != std::numeric_limits<uint32_t>::max();
}

bool no_data_search_condition::operator()(const uint16_t& v) const {
	return v != std::numeric_limits<uint16_t>::max();
}

}
