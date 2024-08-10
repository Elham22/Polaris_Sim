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
#include <iomanip>

namespace ns3 {
NS_LOG_COMPONENT_DEFINE ("ScionCapableNode");

void
ScionCapableNode::ScheduleReceive (uint16_t local_if, ScionPacket *packet, Time propagation_delay)
{
  AdvanceLocalTime ();
  if (propagation_delay > PROP_DELAY_LOG_TRESHOLD)
    {
      std::cout << GetLogPrefix () << "Receiving packet of type " << packet->payload_type
                << " size " << packet->size << " on interface " << local_if
                << " with propagation delay " << propagation_delay.ToDouble (Time::Unit::MS) << "ms"
                << ((packet->payload_type == PayloadType::APPLICATION_DATA)
                        ? " with sequence number: " +
                              std::to_string (std::get<AppData> (packet->payload).seq_no)
                        : "")
                << std::endl;
    }
  Simulator::Schedule (propagation_delay, &ScionCapableNode::Receive, this, local_if, packet);
}

void
ScionCapableNode::Receive (uint16_t local_if, ScionPacket *packet)
{
  AdvanceLocalTime ();
  processing_queue_length++;
  Time delay = processing_throughput_delay * processing_queue_length + processing_delay;
  if (delay > PROC_DELAY_LOG_TRESHOLD)
    {
      std::cout << GetLogPrefix () << "Processing packet of type " << packet->payload_type
                << " size " << packet->size << " on interface " << local_if
                << " with processing delay " << delay.ToDouble (Time::Unit::MS) << "ms"
                << ((packet->payload_type == PayloadType::APPLICATION_DATA)
                        ? " with sequence number: " +
                              std::to_string (std::get<AppData> (packet->payload).seq_no)
                        : "")
                << std::endl;
    }
  Simulator::Schedule (delay, &ScionCapableNode::ProcessReceivedPacket, this, local_if, packet,
                       local_time);
}

void
ScionCapableNode::ProcessReceivedPacket (uint16_t local_if, ScionPacket *packet, Time receive_time)
{
  AdvanceLocalTime ();
  processing_queue_length--;
  // Other tasks should be done in derived classes
}

void
ScionCapableNode::ScheduleForSend (uint16_t local_if, ScionPacket *packet)
{
  NS_LOG_FUNCTION (packet);
  UpdateInterfaceEstimation (local_if);
  current_throughput_bytes.at (local_if) += packet->size;
  arrived_packets.at (local_if) += 1;

  // Reduce queue size by the amount of bytes that have been transmitted since the last update
  auto time_since_last_update = local_time - transmission_queues_size_last_updated.at (local_if);
  auto bytes_transmitted = (time_since_last_update / transmission_delays.at (local_if)).GetHigh ();
  transmission_queues_lengths.at (local_if) =
      std::max (0l, transmission_queues_lengths.at (local_if) - bytes_transmitted);
  transmission_queues_size_last_updated.at (local_if) = local_time;

  /*if (as_number == 0 && local_address == 0)
    {
      std::cout << local_time.ToDouble (Time::Unit::S) << ": Schedule packet type " << packet->payload_type << " size " << packet->size
                << " Q before " << transmission_queues_lengths.at (local_if) << "/" << max_transmission_queues_lengths.at (local_if)
                << std::endl;
    }*/

  // Store and update the arrival time of data packets in the map based on their app id
  // we use this to estimate the number of flows based on active app ids
  if (packet->payload_type == PayloadType::APPLICATION_DATA)
    {
      app_id_last_seen.at (local_if).insert_or_assign (std::get<AppData> (packet->payload).app_id,
                                                       Simulator::Now ());
    }

  auto new_size = transmission_queues_lengths.at (local_if) + packet->size;
  if (new_size > max_transmission_queues_lengths.at (local_if) &&
      packet->payload_type == PayloadType::APPLICATION_DATA)
    {
      /*std::cout << local_time.ToDouble (Time::Unit::S) << ": Node " << isd_number << ":" << as_number << ":" << local_address << ", dropping packet "
                << packet->id << ", type " << packet->payload_type << " size " << packet->size << ", if " << local_if << ", queue length: " 
                << new_size << "/" << max_transmission_queues_lengths.at (local_if) << std::endl;*/
      // TODO some packets are never dropped to make their transmission reliable. Handling loss of these packets should be implemented later.
      current_loss_bytes.at (local_if) += packet->size;
      lost_packets.at (local_if) += 1;

      if (false) // TODO(wickip): When? How often? Do we keep state?
        {
          ReturnCongestionAlert (packet, local_if);
        }
      else
        {
          Drop (packet);
        }
      return;
    }
  else if (packet->ecn_capable && new_size > max_transmission_queues_lengths.at (local_if) / 2)
    {
      packet->ecn = 1;
    }

  transmission_queues_lengths.at (local_if) = new_size;
  Time delay = transmission_delays.at (local_if) * transmission_queues_lengths.at (local_if);
  if (delay > TRANSM_DELAY_LOG_TRESHOLD)
    {
      std::cout << GetLogPrefix () << "Scheduling packet of type " << packet->payload_type
                << " size " << packet->size << " on interface " << local_if
                << " with transmission delay " << delay.ToDouble (Time::Unit::MS) << "ms"
                << ((packet->payload_type == PayloadType::APPLICATION_DATA)
                        ? " with sequence number: " +
                              std::to_string (std::get<AppData> (packet->payload).seq_no)
                        : "")
                << std::endl;
    }
  Simulator::Schedule (delay, &ScionCapableNode::Send, this, local_if, packet);
}

void
ScionCapableNode::Send (uint16_t local_if, ScionPacket *packet)
{
  AdvanceLocalTime ();
  NS_LOG_FUNCTION (packet);
  ScionCapableNode *remote_node = std::get<0> (remote_nodes_info.at (local_if));
  uint16_t remote_if = std::get<1> (remote_nodes_info.at (local_if));
  ModifyPktUponSend (packet);
  remote_node->ScheduleReceive (remote_if, packet, propagation_delays.at (local_if));
}

void
ScionCapableNode::Drop (ScionPacket *packet)
{
  NS_LOG_FUNCTION (packet);

  // auto app_id = packet->payload_type == PayloadType::APPLICATION_DATA
  //                   ? std::get<AppData> (packet->payload).app_id
  //                   : "";
  // std::cout << GetLogPrefix () << "Dropping packet of type " << packet->payload_type << " size "
  //           << packet->size << " on interface " << local_if << " due to congestion: " << new_size
  //           << "/" << max_transmission_queues_lengths.at (local_if) << " app_id: " << app_id
  //           << std::endl;
  packet->packet_originator->DestroyScionPacket (packet);
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
  auto n_devices = GetNDevices ();
  NS_ASSERT (transmission_delays.size () == n_devices && propagation_delays.size () == n_devices);
  transmission_queues_lengths.resize (n_devices);
  transmission_queues_size_last_updated.resize (n_devices);
  max_transmission_queues_lengths.resize (n_devices);
  current_throughput_bytes.resize (n_devices);
  current_loss_bytes.resize (n_devices);
  last_update.resize (n_devices);
  estimated_throughput.resize (n_devices);
  predicted_new_throughput.resize (n_devices);
  arrived_packets.resize (n_devices);
  estimated_num_flows.resize (n_devices);
  lost_packets.resize (n_devices);
  estimated_packetloss.resize (n_devices);
  estimated_no_flows.resize (n_devices);
  estimation_times.resize (n_devices);
  app_id_last_seen.resize (n_devices);

  std::cout << GetInfoAsString () << "Initializing transmission queues" << std::endl;

  // set the max_queue sizes to the bwd-delay product
  for (uint32_t i = 0; i < n_devices; ++i)
    {
      double bwd_Gbit = 400; // default 400 Gbps
      auto transmission_delay = transmission_delays.at (i);
      if (transmission_delay != 0)
        {
          bwd_Gbit = 8000.0 / transmission_delay.ToInteger (Time::Unit::PS);
        }
      auto propagation_delay = propagation_delays.at (i).ToInteger (Time::Unit::NS);
      if (propagation_delay < 100000)
        {
          propagation_delay = 100000; // 0.1ms delay minimum
        }
      // units cancel out, 1Gbit = 10^9bit, 1NS = 10^(-9)s
      max_transmission_queues_lengths[i] = bwd_Gbit * propagation_delay;

      // TODO: temp change to make max queuing delay comparable to paper
      // max_transmission_queues_lengths[i] = bwd_Gbit * propagation_delay * 8;

      // Print target as and node info
      if (remote_nodes_info.size () > i)
        {
          auto remote_node = std::get<0> (remote_nodes_info.at (i));
          auto remote_if = std::get<1> (remote_nodes_info.at (i));
          std::cout << "  Link " << i << " to " << remote_node->GetInfoAsString () << " on iface "
                    << remote_if;
        }
      else
        {
          std::cout << "  Link " << i << " to unknown (remote_node info not found)";
        }

      // Print link info
      std::cout << "  Transm. delay[ps]: " << std::setw (8)
                << transmission_delay.ToInteger (Time::Unit::PS) << std::setw (0)
                << ", prop delay[ms]: " << std::setw (8)
                << propagation_delays.at (i).ToInteger (Time::Unit::MS) << std::setw (0)
                << ", bwd[Gbit]: " << std::setw (6) << bwd_Gbit << std::setw (0)
                << ", queue[B]: " << std::setw (12) << max_transmission_queues_lengths[i]
                << std::endl;
    }
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
  else // packet->dst_ia == ia_addr
    {
      if (this->local_address == packet->dst_host)
        {
          NS_FATAL_ERROR ("Attempted to send packet to self");
        }
      local_if_to_send = forwarding_table_to_addresses_inside_as.at (packet->dst_host);
    }

  ScheduleForSend (local_if_to_send, packet);
}

ScionPacket *
ScionCapableNode::CreateScionPacket (const Payload &payload, PayloadType payload_type, ia_t dst_ia,
                                     host_addr_t dst_host, int32_t payload_size,
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
ScionCapableNode::ReturnCongestionAlert (ScionPacket *src_packet, uint16_t local_if)
{
  CongestionAlert alert;
  alert.interface_id = local_if;
  alert.ia = ia_addr;

  uint32_t app_id;
  uint32_t path_id;
  uint16_t ca_code; // 1 for probes, 2 for data packets

  if (src_packet->payload_type == PayloadType::SCMP &&
      std::get<Scmp> (src_packet->payload).type == PROBE)
    {
      ca_code = 1;
      Scmp scmp = std::get<Scmp> (src_packet->payload);
      BottleneckProbe probe = std::get<BottleneckProbe> (scmp.data);
      app_id = scmp.app_id;
      path_id = scmp.path_id;

      // When C-CA is a response to a probe, copy the fields
      alert.seq_no = probe.seq_no;
      alert.id = probe.id;
    }
  else if (src_packet->payload_type == PayloadType::APPLICATION_DATA)
    {
      ca_code = 2;
      AppData data = std::get<AppData> (src_packet->payload);
      app_id = data.app_id;
      path_id = data.path_id;
    }
  else
    {
      src_packet->packet_originator->DestroyScionPacket (src_packet);
      return; // Only respond with C-CA to known types
    }

  // Re-use the existing packet and return to sender
  src_packet->payload = Scmp{.type = CONGESTION_ALERT,
                             .code = ca_code,
                             .data = alert,
                             .app_id = app_id,
                             .path_id = path_id};
  src_packet->payload_type = PayloadType::SCMP;

  ReturnScionPacket (src_packet);
}

void
ScionCapableNode::PrintPath (std::vector<const PathSegment *> the_path)
{
  std::cout << "Printing path: ";
  for (PathSegment const *segment : the_path)
    {
      std::cout << "[";
      for (uint64_t const hop : segment->hops)
        {
          std::cout << "-" << GET_HOP_ING_IF (hop) << "->(" << GET_HOP_ISD (hop) << ":"
                    << GET_HOP_AS (hop) << ")-" << GET_HOP_EG_IF (hop) << "->" << ", ";
        }
      std::cout << "], ";
    }
  std::cout << std::endl;
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

void
ScionCapableNode::UpdateInterfaceEstimation (uint16_t local_if)
{
  AdvanceLocalTime ();
  auto time_passed = local_time - last_update.at (local_if);
  if (time_passed > collection_period)
    {
      //estimated_loss.at (local_if).push_back (current_throughput_bytes.at (local_if) > 0 ? ((double) current_loss_bytes.at (local_if)) / current_throughput_bytes.at (local_if) : 0);
      uint64_t throughput =
          current_throughput_bytes.at (local_if) * 1000 / time_passed.ToInteger (Time::Unit::MS);
      estimated_throughput.at (local_if).push_back (throughput);
      auto predicted_new_throughput_local = predicted_new_throughput.at (local_if);
      uint64_t previous_new_througput = throughput;
      if (predicted_new_throughput_local.size () > 0)
        {
          // also consider some previous requests as they might have happened just before the update
          previous_new_througput +=
              predicted_new_throughput_local.at (predicted_new_throughput_local.size () - 1) / 10;
        }
      predicted_new_throughput_local.push_back (previous_new_througput);
      estimated_packetloss.at (local_if).push_back (arrived_packets.at (local_if) > 0
                                                        ? ((double) lost_packets.at (local_if)) /
                                                              arrived_packets.at (local_if)
                                                        : 0);
      estimation_times.at (local_if).push_back (local_time);
      if (current_throughput_bytes.at (local_if) > 0)
        {
          Simulator::Schedule (collection_period + TimeStep (1),
                               &ScionCapableNode::UpdateInterfaceEstimation, this, local_if);
        }
      //std::cout << "Lost / arrived " << lost_packets.at (local_if) << "/" << arrived_packets.at (local_if) << std::endl;

      estimated_no_flows.at (local_if).push_back (estimated_num_flows.at (local_if));

      // remove all the app ids that have not been seen for a while
      auto app_id_last_seen_local = app_id_last_seen.at (local_if);
      for (auto it = app_id_last_seen_local.begin (); it != app_id_last_seen_local.end ();)
        {
          if (local_time - it->second > collection_period)
            {
              std::cout << GetLogPrefix () << "Removing flow with app_id " << it->first
                        << " from interface " << local_if << " ("
                        << (local_time - it->second).ToDouble (Time::Unit::S) << " seconds old)"
                        << std::endl;
              it = app_id_last_seen_local.erase (it);
            }
          else
            {
              ++it;
            }
        }
      app_id_last_seen.at (local_if) = app_id_last_seen_local;

      if (estimated_num_flows.at (local_if) != app_id_last_seen_local.size ())
        {

          std::cout << GetLogPrefix () << "New estimated number of flows at local iface "
                    << local_if << ": " << app_id_last_seen_local.size ()
                    << " (before: " << estimated_num_flows.at (local_if) << ")" << std::endl;
        }
      // now the estimated number of flows is simply how many app ids we have seen in the last period
      estimated_num_flows.at (local_if) = app_id_last_seen_local.size ();

      // reset the counters
      current_loss_bytes.at (local_if) = 0;
      current_throughput_bytes.at (local_if) = 0;
      lost_packets.at (local_if) = 0;
      arrived_packets.at (local_if) = 0;
      last_update.at (local_if) = local_time;

      /*std::cout << local_time.ToInteger (Time::Unit::S) << ": Node " << isd_number << ":" << as_number << ":" << local_address
                << "(" << local_if << "):" << estimated_throughput.at (local_if). at(estimated_throughput.at (local_if).size () - 1)
                << std::endl;*/
    }
}

void
ScionCapableNode::PrintLinkInfo (uint16_t local_if)
{
  local_if = 0;
  auto times = estimation_times.at (local_if);
  auto throughput = estimated_throughput.at (local_if);
  auto loss = estimated_packetloss.at (local_if);

  for (uint64_t i = 0; i < times.size () && i < throughput.size () && i < loss.size (); ++i)
    {
      std::cout << times.at (i).ToInteger (Time::Unit::MS) << ", " << throughput.at (i) << ", "
                << loss.at (i) << std::endl;
    }
}

std::string
ScionCapableNode::GetAddressAsString ()
{
  return std::to_string (isd_number) + ":" + std::to_string (as_number) + ":" +
         std::to_string (local_address);
}

std::string
ScionCapableNode::GetTypeAsString ()
{
  if (typeid (*this) == typeid (ScionHost))
    {
      return "Host";
    }
  else if (typeid (*this) == typeid (PathServer))
    {
      return "PathServer";
    }
  else if (typeid (*this) == typeid (BorderRouter))
    {
      return "BorderRouter";
    }
  else
    {
      return "Node";
    }
}

std::string
ScionCapableNode::GetInfoAsString ()
{
  std::string s = GetAddressAsString () + " (" + GetTypeAsString () + ")";
  s.resize (22, ' '); // pad to align
  return s;
}

std::string
ScionCapableNode::GetLogPrefix ()
{

  std::string log_prefix = "[" + std::to_string (Simulator::Now ().ToDouble (Time::Unit::MIN)) +
                           "][" + GetInfoAsString () + "] ";
  return log_prefix;
}

} // namespace ns3
