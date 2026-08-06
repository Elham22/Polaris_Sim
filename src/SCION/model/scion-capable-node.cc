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

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"

#include "scion-as.h"
#include "scion-capable-node.h"

namespace ns3 {
NS_LOG_COMPONENT_DEFINE ("ScionCapableNode");

void
ScionCapableNode::ScheduleReceive (uint16_t local_if, ScionPacket *packet, Time propagation_delay)
{
  AdvanceLocalTime ();
  Simulator::Schedule (propagation_delay, &ScionCapableNode::Receive, this, local_if, packet);
}

void
ScionCapableNode::Receive (uint16_t local_if, ScionPacket *packet)
{
  AdvanceLocalTime ();
  processing_queue_length++;
  Time delay = processing_throughput_delay * processing_queue_length + processing_delay;
  Simulator::Schedule (delay, &ScionCapableNode::ProcessReceivedPacket, this, local_if, packet,
                       local_time);
}

void
ScionCapableNode::ProcessReceivedPacket (uint16_t local_if, ScionPacket *packet,
                                           Time receive_time)
{
  AdvanceLocalTime ();
  processing_queue_length--;
  // Other tasks should be done in derived classes
}

void
ScionCapableNode::ScheduleForSend (uint16_t local_if, ScionPacket *packet)
{
  NS_LOG_FUNCTION (packet);
  transmission_queues_lengths.at (local_if) += packet->size;
  Time delay = transmission_delays.at (local_if) * transmission_queues_lengths.at (local_if);
  Simulator::Schedule (delay, &ScionCapableNode::Send, this, local_if, packet);
}

void
ScionCapableNode::Send (uint16_t local_if, ScionPacket *packet)
{
  AdvanceLocalTime ();
  NS_LOG_FUNCTION (packet);
  transmission_queues_lengths.at (local_if) -= packet->size;
  ScionCapableNode *remote_node = std::get<0> (remote_nodes_info.at (local_if));
  uint16_t remote_if = std::get<1> (remote_nodes_info.at (local_if));
  ModifyPktUponSend (packet);
  remote_node->ScheduleReceive (remote_if, packet, propagation_delays.at (local_if));
}

void
ScionCapableNode::AddToIfForwadingTable (uint16_t as_if, uint16_t local_if)
{
  forwarding_table_to_other_as_ifaces.insert (std::make_pair (as_if, local_if));
}

void
ScionCapableNode::AddToAddressForwardingTable (host_addr_t addr, uint16_t local_if)
{
  forwarding_table_to_addresses_inside_as.insert (std::make_pair (addr, local_if));
}

host_addr_t
ScionCapableNode::GetLocalAddress () const
{
  return local_address;
}

double
ScionCapableNode::GetLatitude () const
{
  return latitude;
}
double
ScionCapableNode::GetLogitude () const
{
  return longitude;
}

void
ScionCapableNode::AddToPropagationDelays (Time delay)
{
  propagation_delays.push_back (delay);
}
void
ScionCapableNode::AddToTransmissionDelays (Time delay)
{
  transmission_delays.push_back (delay);
}
void
ScionCapableNode::SetProcessingDelay (Time delay, Time throughput_delay)
{
  processing_delay = delay;
  processing_throughput_delay = throughput_delay;
}

void
ScionCapableNode::AddToRemoteNodesInfo (ScionCapableNode *remote_node, uint16_t remote_if,
                                        uint16_t remote_isd, uint16_t remote_as)
{
  if (remote_isd == isd_number && remote_as == as_number)
    {
      remote_nodes_info.push_back (std::make_tuple (remote_node, remote_if, true));
    }
  else
    {
      remote_nodes_info.push_back (std::make_tuple (remote_node, remote_if, false));
    }
}

void
ScionCapableNode::InitializeTransmissionQueues ()
{
  transmission_queues_lengths.resize (GetNDevices ());
}

void
ScionCapableNode::DestroyScionPacket (ScionPacket *packet)
{
  NS_ASSERT (packet == &on_the_flight_packets.at (packet->id));
  on_the_flight_packets.erase (packet->id);
}

void
ScionCapableNode::SendScionPacket (ScionPacket *packet)
{
  NS_LOG_FUNCTION ("I am host " << isd_number << ":" << as_number << ":" << local_address
                                << ". Packet sent to " << GET_ISDN (packet->dst_ia) << ":"
                                << GET_ASN (packet->dst_ia) << ":" << packet->dst_host);

  uint16_t local_if_to_send;
  if (packet->dst_ia != ia_addr)
    {
      uint64_t hopf = packet->path.at (packet->curr_inf)->hops.at (packet->cur_hopf);
      NS_ASSERT (GET_HOP_ISD (hopf) == isd_number && GET_HOP_AS (hopf) == as_number);
      bool reverse = packet->path_reversed ^ packet->path.at (packet->curr_inf)->reverse;

      NS_LOG_FUNCTION (reverse << " " << packet->path_reversed << " "
                               << packet->path.at (packet->curr_inf)->reverse);

      uint16_t as_if_to_send;
      if (reverse)
        {
          as_if_to_send = GET_HOP_ING_IF (hopf);
        }
      else
        {
          as_if_to_send = GET_HOP_EG_IF (hopf);
        }

      NS_LOG_FUNCTION (
          " first hop field: isd: "
          << GET_HOP_ISD (packet->path.at (packet->curr_inf)->hops.at (packet->cur_hopf)) << ", as:"
          << GET_HOP_AS (packet->path.at (packet->curr_inf)->hops.at (packet->cur_hopf)) << ", ing:"
          << GET_HOP_ING_IF (packet->path.at (packet->curr_inf)->hops.at (packet->cur_hopf))
          << ", eg:"
          << GET_HOP_EG_IF (packet->path.at (packet->curr_inf)->hops.at (packet->cur_hopf)));
      NS_LOG_FUNCTION ("as_if_to_send: " << as_if_to_send);

      local_if_to_send = forwarding_table_to_other_as_ifaces.at (as_if_to_send);
    }
  else
    {
      local_if_to_send = forwarding_table_to_addresses_inside_as.at (packet->dst_host);
    }

  ScheduleForSend (local_if_to_send, packet);
}

ScionPacket *
ScionCapableNode::CreateScionPacket (const Payload &payload, PayloadType payload_type,
                                       ia_t dst_ia, host_addr_t dst_host, int32_t payload_size,
                                       const std::vector<const PathSegment *> &the_path,
                                       const std::vector<uint8_t> &shortcut_hopfs)
{
  on_the_flight_packets.insert (
      std::make_pair (next_packet_id, ScionPacket (this, next_packet_id)));
  ScionPacket *packet = &on_the_flight_packets.at (next_packet_id);
  next_packet_id++;

  packet->src_ia = ia_addr;
  packet->dst_ia = dst_ia;
  packet->src_host = local_address;
  packet->dst_host = dst_host;

  packet->path = the_path;
  packet->shortcut_hopfs = shortcut_hopfs;
  packet->path_reversed = false;
  packet->curr_inf = 0;
  packet->cur_hopf = 0;

  packet->payload_type = payload_type;
  packet->payload = payload;

  packet->timestamp = local_time;
  packet->size = 14 + 12 + 24 + payload_size; // MAC + Common Header + Address Header + payload size

  if (packet->path.size () > 0)
    {
      packet->size += 4 + packet->path.size () * 8; //Path Meta Hdr + Info fields
      for (auto const &path_seg : packet->path)
        {
          packet->size += path_seg->hops.size () * 12; // Hop Fields
        }
    }

  return packet;
}

void
ScionCapableNode::ReturnScionPacket (ScionPacket *packet)
{
  packet->dst_host = packet->src_host;
  packet->dst_ia = packet->src_ia;
  packet->src_ia = ia_addr;
  packet->src_host = local_address;

  packet->path_reversed = !packet->path_reversed;
  packet->timestamp = local_time;

  SendScionPacket (packet);
}

uint32_t
ScionCapableNode::GetNDevices (void) const
{
  return propagation_delays.size ();
}

void
ScionCapableNode::AdvanceLocalTime ()
{
  local_time = Simulator::Now ();
}

void
ScionCapableNode::ModifyPktUponSend (ScionPacket *packet)
{
}

Time
ScionCapableNode::GetLocalTime (void) const
{
  return local_time;
}
} // namespace ns3