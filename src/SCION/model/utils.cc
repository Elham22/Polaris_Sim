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

#include <cmath>
#include <random>
#include <set>

#include "src/SCION/model/beaconing/beacon-server.h"
#include "utils.h"

namespace ns3 {

double
GetMedian (std::multiset<int64_t> &data)
{
  if (data.empty ())
    throw std::length_error ("Cannot calculate median value for empty dataset");

  const size_t n = data.size ();
  double median = 0;

  auto iter = data.cbegin ();
  std::advance (iter, n / 2);

  // Middle or average of two middle values
  if (n % 2 == 0)
    {
      const auto iter2 = iter--;
      median = double (*iter + *iter2) / 2; // data[n/2 - 1] AND data[n/2]
    }
  else
    {
      median = *iter;
    }
  return median;
}

std::vector<std::string> &
Split (const std::string &s, char delim, std::vector<std::string> &elems)
{
  std::stringstream ss (s);
  std::string item;
  while (std::getline (ss, item, delim))
    {
      elems.push_back (item);
    }
  return elems;
}

ld
LinkLevelJaccardDistanceBetweenTwoPaths (Beacon *beacon1, Beacon *beacon2)
{
  std::set<uint32_t> set_of_links_on_path1;
  int32_t intersection = 0;

  for (auto const &link_info : beacon1->the_path)
    {
      set_of_links_on_path1.insert (UPPER_32_BITS (link_info));
    }

  for (auto const &link_info : beacon2->the_path)
    {
      if (set_of_links_on_path1.find (UPPER_32_BITS (link_info)) != set_of_links_on_path1.end ())
        {
          intersection++;
        }
      else
        {
          set_of_links_on_path1.insert (UPPER_32_BITS (link_info));
        }
    }

  return 1 - 1.0 * intersection / set_of_links_on_path1.size ();
}

ld
AsLevelJaccardDistanceBetweenTwoPaths (Beacon *beacon1, Beacon *beacon2)
{
  std::set<uint16_t> set_of_ASes_on_path1;
  int32_t intersection = 0;

  for (auto const &link_info : beacon1->the_path)
    {
      set_of_ASes_on_path1.insert (UPPER_16_BITS (link_info));
    }

  for (auto const &link_info : beacon2->the_path)
    {
      if (set_of_ASes_on_path1.find (UPPER_16_BITS (link_info)) != set_of_ASes_on_path1.end ())
        {
          intersection++;
        }
      else
        {
          set_of_ASes_on_path1.insert (UPPER_16_BITS (link_info));
        }
    }
  return 1 - 1.0 * intersection / set_of_ASes_on_path1.size ();
}

ld
CalculateGreatCircleLatency (ld lat1_deg, ld long1_deg, ld lat2_deg, ld long2_deg)
{
  ld distance = CalculateGreatCircleDistance (lat1_deg, long1_deg, lat2_deg, long2_deg);
  // 0.005 millisecods of latency per kilometer
  ld latency = distance * 0.005;
  return latency;
}

ld
CalculateGreatCircleDistance (ld lat1_deg, ld long1_deg, ld lat2_deg, ld long2_deg)
{
  ld lat1 = lat1_deg * (M_PI) / 180;
  ld long1 = long1_deg * (M_PI) / 180;
  ld lat2 = lat2_deg * (M_PI) / 180;
  ld long2 = long2_deg * (M_PI) / 180;

  // Haversine Formula
  ld dlong = long2 - long1;
  ld dlat = lat2 - lat1;

  ld distance =
      6371 * 2 *
      asin (sqrt (pow (sin (dlat / 2), 2) + cos (lat1) * cos (lat2) * pow (sin (dlong / 2), 2)));

  return distance;
}

std::string
GetAttribute (rapidxml::xml_node<> *node, const std::string &name)
{
  rapidxml::xml_attribute<> *attr = node->first_attribute (name.c_str ());
  if (attr)
    {
      return attr->value ();
    }
  else
    {
      return std::string ();
    }
}

std::string
PropertyContainer::GetProperty (const std::string &name) const
{
  propertiesType::const_iterator it;
  it = this->properties.find (name);

  if (it != this->properties.end ())
    return it->second;
  else
    exit (1);
}

void
PropertyContainer::SetProperty (const std::string &name, const std::string &value)
{
  this->properties[name] = value;
}

bool
PropertyContainer::HasProperty (const std::string &name) const
{
  propertiesType::const_iterator it = this->properties.find (name);
  if (it == this->properties.end ())
    {
      return false;
    }
  else
    {
      return true;
    }
}

PropertyContainer
ParseProperties (rapidxml::xml_node<> *node)
{
  PropertyContainer p;
  rapidxml::xml_node<> *cur_node = node->first_node ("property");

  while (cur_node)
    {
      std::string name = GetAttribute (cur_node, "name");
      if (name != "")
        {
          p.SetProperty (name, cur_node->value ());
        }
      cur_node = cur_node->next_sibling ("property");
    }

  return p;
}

} // namespace ns3