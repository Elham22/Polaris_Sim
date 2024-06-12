/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2024 ETH Zuerich
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
 * Author: Patrick Wicki <patrick.wicki@inf.ethz.ch>
 */

#ifndef WEBRTC_CC_TYPES_H
#define WEBRTC_CC_TYPES_H

#include "src/core/model/simulator.h"
#include <vector>

namespace ns3 {

/**
 * Record of a packet with all information relevant for congestion control
 */
struct PacketRecord
{
  Time time_sent;
  Time time_received;
  uint32_t seq_no;
  uint32_t frame_no;
  uint16_t size; // size in bytes
};

/**
 * Report of packets received by the receiver
 */
struct PacketsReport
{
  Time time_created = Simulator::Now ();
  std::vector<PacketRecord> packets;

  void
  AddPacket (PacketRecord packet)
  {
    packets.push_back (packet);
  }

  // Frame is assumed complete when the newest packet is part of a new frame
  bool
  IsFrameComplete ()
  {
    if (packets.size () < 2)
      {
        return false;
      }
    return packets[packets.size () - 1].frame_no != packets[packets.size () - 2].frame_no;
  }

  Time
  Age ()
  {
    return Simulator::Now () - time_created;
  }

  double
  Loss ()
  {
    if (packets.size () < 2)
      {
        return 0;
      }
    uint32_t expected = packets[packets.size () - 1].seq_no - packets[0].seq_no + 1;
    uint32_t received = packets.size ();
    return 1.0 - (double) received / expected;
  }
};

} // namespace ns3

#endif // WEBRTC_CC_TYPES_H