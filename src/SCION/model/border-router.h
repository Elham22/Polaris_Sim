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
 * Author: Seyedali Tabaeiaghdaei seyedali.tabaeiaghdaei@inf.ethz.ch
 */

#ifndef SCION_SIMULATOR_BORDER_ROUTER_H
#define SCION_SIMULATOR_BORDER_ROUTER_H

#include "ns3/network-module.h"
#include "ns3/node.h"
#include "ns3/nstime.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"

#include "scion-capable-node.h"

namespace ns3 {
class BorderRouter : public ScionCapableNode
{
public:
  BorderRouter (uint32_t system_id, uint16_t isd_number, uint16_t as_number, uint32_t local_address,
                double latitude, double longitude, ScionAs *as)
      : ScionCapableNode (system_id, isd_number, as_number, local_address, latitude, longitude, as)
  {
  }

private:
  void ProcessReceivedPacket (uint16_t local_if, ScionPacket *packet, Time receive_time) override;
};
} // namespace ns3
#endif //SCION_SIMULATOR_BORDER_ROUTER_H
