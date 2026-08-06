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

#ifndef SCION_SIMULATOR_SCION_CAPABLE_NODE_H
#define SCION_SIMULATOR_SCION_CAPABLE_NODE_H

#include "ns3/node.h"

#include "scion-packet.h"

namespace ns3 {
class ScionAs;

class ScionCapableNode : public Node
{
public:
  ScionCapableNode (uint32_t system_id, uint16_t isd_number, uint16_t as_number,
                    host_addr_t local_address, double latitude, double longitude, ScionAs *as)
      : Node (system_id),
        isd_number (isd_number),
        as_number (as_number),
        local_address (local_address),
        latitude (latitude),
        longitude (longitude),
        as (as)
  {
    ia_addr = (((uint32_t) isd_number) << 16) | ((uint32_t) as_number);
    next_packet_id = 0;
    processing_queue_length = 0;
    local_time = TimeStep (0);
  }

  void AddToIfForwadingTable (uint16_t as_if, uint16_t local_if);
  void AddToAddressForwardingTable (host_addr_t addr, uint16_t local_if);
  void ScheduleReceive (uint16_t local_if, ScionPacket *packet, Time propagation_delay);

  host_addr_t GetLocalAddress () const;

  double GetLatitude () const;
  double GetLogitude () const;

  void AddToPropagationDelays (Time delay);
  void AddToTransmissionDelays (Time delay);
  void SetProcessingDelay (Time delay, Time throughput_delay);
  void AddToRemoteNodesInfo (ScionCapableNode *remote_node, uint16_t remote_if, uint16_t remote_isd,
                             uint16_t remote_as);
  void InitializeTransmissionQueues ();
  void DestroyScionPacket (ScionPacket *packet);
  uint32_t GetNDevices (void) const;
  Time GetLocalTime (void) const;

  virtual void AdvanceLocalTime ();

protected:
  uint16_t isd_number;
  uint16_t as_number;

  ia_t ia_addr;

  host_addr_t local_address;

  double latitude;
  double longitude;

  ScionAs *as;

  // Queueing delay is modeled by the processing and send scheduling queues, but no drop function is implemented yet
  std::vector<Time> propagation_delays;
  std::vector<Time> transmission_delays; // In picoseconds/byte
  Time processing_delay, processing_throughput_delay;

  std::vector<uint32_t> transmission_queues_lengths; // In bytes
  uint32_t processing_queue_length; // In packets

  packet_id_t next_packet_id;

  Time local_time;

  std::unordered_map<uint16_t, uint16_t> forwarding_table_to_other_as_ifaces;
  std::unordered_map<host_addr_t, uint16_t> forwarding_table_to_addresses_inside_as;

  std::unordered_map<packet_id_t, ScionPacket> on_the_flight_packets;

  std::vector<std::tuple<ScionCapableNode *, uint16_t, bool>> remote_nodes_info;

  void Receive (uint16_t local_if, ScionPacket *packet);
  void Send (uint16_t local_if, ScionPacket *packet);
  virtual void ModifyPktUponSend (ScionPacket *packet);
  virtual void ProcessReceivedPacket (uint16_t local_if, ScionPacket *packet, Time receive_time);
  void ScheduleForSend (uint16_t local_if, ScionPacket *packet);

  void SendScionPacket (ScionPacket *packet);
  ScionPacket *CreateScionPacket (
      const Payload &payload, PayloadType payload_type, ia_t dst_ia, host_addr_t dst_host,
      int32_t payload_size,
      const std::vector<const PathSegment *> &the_path = std::vector<const PathSegment *> (),
      const std::vector<uint8_t> &shortcut_hopfs = std::vector<uint8_t> ());

  void ReturnScionPacket (ScionPacket *packet);
};
} // namespace ns3
#endif //SCION_SIMULATOR_SCION_CAPABLE_NODE_H
