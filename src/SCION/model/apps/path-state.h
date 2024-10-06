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

#pragma once

#include "ns3/core-module.h"
#include "src/SCION/model/scion-packet.h"

#include "api/transport/network_types.h"

namespace ns3 {

/**
 * @brief Struct to store state of a path
 */
struct PathState
{
  uint16_t ecn;
  Time last_report; // time of last ecn mark
  double latency = 0; // observed latency, in µs
  double bottleneck_share = 0; // estimated fair share in Gbps
  double bottleneck_num_flows = 0; // number of flows at the bottleneck link
  // TODO queueing delay
  double loss = 0; // estimated loss fraction
  double sendrate = 0; // current send rate

  std::vector<webrtc::SentPacket> in_flight_packets;
  double in_flight_bytes = 0;

  Time last_probe_sent = Seconds (0); // time the last probe was sent
  BottleneckProbe last_probe_echo; // last probe response
  Time last_probe_echo_time = Seconds (0);
  bool HasFreshProbeResultsSince (Time t);
  Time last_congestion_alert = Seconds (0); // Last time we received a C-CA

  app_packet_id_t probe_seq_no = 0; // probe packet seq_no

  // Earliest point in time since which path has continously been a switching candidate
  Time is_candidate_since = Time::Max ();
  bool is_candidate = false;
};

} // namespace ns3