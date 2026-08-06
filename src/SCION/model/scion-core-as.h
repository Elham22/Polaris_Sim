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

#ifndef SCION_SIMULATOR_SCION_CORE_AS_H
#define SCION_SIMULATOR_SCION_CORE_AS_H

#include "scion-as.h"

namespace ns3 {

class ScionCoreAs : public ScionAs
{
public:
  ScionCoreAs (uint32_t system_id, bool parallel_scheduler, uint16_t as_number,
                 rapidxml::xml_node<> *xml_node, const YAML::Node &config,
                 bool malicious_border_routers, Time local_time)
      : ScionAs (system_id, parallel_scheduler, as_number, xml_node, config,
                  malicious_border_routers, local_time)
  {
  }
};
} // namespace ns3
#endif //SCION_SIMULATOR_SCION_CORE_AS_H
