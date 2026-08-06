/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2022 ETH Zuerich
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Seyedali Tabaeiaghdaei seyedali.tabaeiaghdaei@inf.ethz.ch,
 *         Christelle Gloor  christelle.gloor@inf.ethz.ch
 */

#ifndef SCION_SIMULATOR_UTILS_H
#define SCION_SIMULATOR_UTILS_H

#include <map>
#include <set>

#include "ns3/rapidxml.hpp"

#include "src/SCION/model/beaconing/beacon.h"

namespace ns3 {

#define UPPER_16_BITS(input) ((uint16_t) ((input) >> 48))

#define LOWER_16_BITS(input) ((uint16_t) ((input) &0x000000000000ffff))

#define SECOND_UPPER_16_BITS(input) ((uint16_t) (((input) &0x0000ffff00000000) >> 32))

#define SECOND_LOWER_16_BITS(input) ((uint16_t) (((input) &0x00000000ffff0000) >> 16))

#define UPPER_32_BITS(input) ((uint32_t) ((input) >> 32))

#define LOWER_32_BITS(input) ((uint32_t) ((input) &0x00000000ffffffff))

double GetMedian (std::multiset<int64_t> &data);

std::vector<std::string> &Split (const std::string &s, char delim, std::vector<std::string> &elems);

ld LinkLevelJaccardDistanceBetweenTwoPaths (Beacon *beacon1, Beacon *beacon2);

ld AsLevelJaccardDistanceBetweenTwoPaths (Beacon *beacon1, Beacon *beacon2);

ld CalculateGreatCircleLatency (ld lat1_deg, ld long1_deg, ld lat2_deg, ld long2_deg);

ld CalculateGreatCircleDistance (ld lat1_deg, ld long1_deg, ld lat2_deg, ld long2_deg);

std::string GetAttribute (rapidxml::xml_node<> *node, const std::string &name);

class PropertyContainer
{
public:
  std::string GetProperty (const std::string &name) const;
  void SetProperty (const std::string &name, const std::string &value);
  bool HasProperty (const std::string &name) const;

private:
  typedef std::map<std::string, std::string> propertiesType;
  propertiesType properties;
};

PropertyContainer ParseProperties (rapidxml::xml_node<> *node);

} // namespace ns3
#endif //SCION_SIMULATOR_UTILS_H