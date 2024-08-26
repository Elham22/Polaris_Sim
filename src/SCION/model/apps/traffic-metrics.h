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

#include "src/SCION/model/webrtc-cc/types.h"

namespace ns3 {

struct TrafficMetrics
{
  bool logging = false;

  double oldrate = 0;
  double newrate = 0;
  bool in_transition = false;
  double A_s = 0;
  double A_r = 0;
  double bottleneck_share = 0;

  double sendrate = 0;
  double latency = 0;
  double jitter = 0;
  double loss = 0;

  // Total bytes across entire connection life time
  double total_bytes_sent = 0;
  double total_bytes_received = 0;

  // Track delivered packets over a certain time window to calculate metrics like loss and jitter
  Time tracking_window = MilliSeconds (100);
  Time min_window = MilliSeconds (0);

  // Vector to keep track of delivered packets, ordered by sequence numbers
  std::vector<PacketRecord> delivered_packets;

  // Index of the active path
  int32_t active_path;

  TrafficMetrics (bool logging = false);

  void OnSentPacket (const PacketRecord &record);
  void TrackNewPacket (const PacketRecord &record);
  void OnReceivedPackets (const std::vector<PacketRecord> &packets);
  void ClearWindow ();

  bool UpdateStatistics ();
  double GetJitterMs ();
  double GetFractionLoss ();

  double GetVmafScore ();
};

} // namespace ns3