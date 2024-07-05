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

enum class ControllerState { DECREASE, HOLD, INCREASE };

enum class BandwidthUsage {
  kBwNormal = 0,
  kBwUnderusing = 1,
  kBwOverusing = 2,
};
const char *BandwidthUsageToString (BandwidthUsage usage);

/**
 * Congestion control related constants
 */
const Time CC_INITIAL_RTT = Time ("100ms");
// const double CC_INITIAL_SEND_RATE = 300'000 / 8; // 300 kbps
const double CC_INITIAL_SEND_RATE = 500'000;
const double CC_ADDITIVE_TERM = 1e3; // KB, per frame or at least every 50ms
const double CC_MULTI_INCREASE = 1.05; // factor, per RTT

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

  double
  AverageOneWayDelay ()
  {
    if (packets.size () < 1)
      {
        return 0;
      }
    Time sum = Time (0);
    for (size_t i = 0; i < packets.size (); i++)
      {
        auto delay = packets[i].time_received - packets[i].time_sent;
        if (delay < Time (0))
          {
            NS_FATAL_ERROR ("Negative one way delay detected: "
                            << delay << " for packet " << i << " with seq_no " << packets[i].seq_no
                            << " and frame_no " << packets[i].frame_no << " sent at "
                            << packets[i].time_sent << " and received at "
                            << packets[i].time_received << " with size " << packets[i].size
                            << " bytes.");
          }
        sum += delay;
      }
    return sum.GetSeconds () / (packets.size ());
  }
};

/**
 * Enum for the congestion control phase
 */
enum class CongestionControlPhase : uint8_t {
  STARTUP,
  CONGESTION_AVOIDANCE,
};

} // namespace ns3

#endif // WEBRTC_CC_TYPES_H