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

#include "border-router.h"
#include "scion-packet.h"
#include "scion-as.h"

namespace ns3 {
NS_LOG_COMPONENT_DEFINE ("BorderRouter");
void
BorderRouter::ProcessReceivedPacket (uint16_t if_rcv, ScionPacket *packet, Time receive_time)
{
  NS_LOG_FUNCTION ("packet received " << packet);
  NS_LOG_FUNCTION (
      isd_number
      << ":" << as_number << " packet from " << GET_ISDN (packet->src_ia) << ":"
      << GET_ASN (packet->src_ia) << " to " << GET_ISDN (packet->dst_ia) << ":"
      << GET_ASN (packet->dst_ia) << ", currIF: " << packet->curr_inf
      << ", currHopF: " << packet->cur_hopf << ", path segments: " << packet->path.size ()
      << ", current hop field: isd: "
      << GET_HOP_ISD (packet->path.at (packet->curr_inf)->hops.at (packet->cur_hopf))
      << ", as:" << GET_HOP_AS (packet->path.at (packet->curr_inf)->hops.at (packet->cur_hopf))
      << ", ing:" << GET_HOP_ING_IF (packet->path.at (packet->curr_inf)->hops.at (packet->cur_hopf))
      << ", eg:" << GET_HOP_EG_IF (packet->path.at (packet->curr_inf)->hops.at (packet->cur_hopf)));

  ScionCapableNode::ProcessReceivedPacket (if_rcv, packet, Time ());

  if (packet->src_ia == packet->dst_ia)
    {
      return;
    }

  if (packet->payload_type == PayloadType::BACKGROUND_TRAFFIC)
    {
      packet->packet_originator->DestroyScionPacket (packet);
      return;
    }

  if (packet->dst_ia == ia_addr)
    {
      NS_ASSERT (GET_HOP_ISD (packet->path.at (packet->curr_inf)->hops.at (packet->cur_hopf)) ==
                 isd_number);
      NS_ASSERT (GET_HOP_AS (packet->path.at (packet->curr_inf)->hops.at (packet->cur_hopf)) ==
                 as_number);

      if (forwarding_table_to_addresses_inside_as.find (packet->dst_host) ==
          forwarding_table_to_addresses_inside_as.end ())
        {
          NS_LOG_FUNCTION ("Address not in the forwarding table");
          std::cout << "BR process received at IA: " << ia_addr
                    << " Address not in the forwarding table " << packet->dst_host << std::endl;
          return;
        }

      uint16_t local_if_to_send = forwarding_table_to_addresses_inside_as.at (packet->dst_host);
      ScheduleForSend (local_if_to_send, packet);

      return;
    }

  uint16_t old_hopf = packet->cur_hopf;
  uint16_t old_inf = packet->curr_inf;
  bool received_from_local_as = std::get<2> (remote_nodes_info.at (if_rcv));

  if (!received_from_local_as)
    {
      if (packet->path_reversed && packet->cur_hopf == 0)
        {
          packet->curr_inf--;
          packet->cur_hopf = packet->path.at (packet->curr_inf)->hops.size () - 1;
        }
      else if (!packet->path_reversed &&
               packet->cur_hopf == packet->path.at (packet->curr_inf)->hops.size () - 1)
        {
          packet->curr_inf++;
          packet->cur_hopf = 0;
        }
      else if (packet->shortcut_hopfs.size () == 2 && packet->path.size () == 2)
        {
          if (packet->shortcut_hopfs.at (packet->curr_inf) == packet->cur_hopf)
            {
              if (packet->path_reversed)
                {
                  packet->curr_inf--;
                }
              else
                {
                  packet->curr_inf++;
                }
            }
          packet->cur_hopf = packet->shortcut_hopfs.at (packet->curr_inf);
        }
    }

  NS_ASSERT (packet->curr_inf >= 0);
  NS_ASSERT (packet->curr_inf < packet->path.size ());

  uint64_t hopf = packet->path.at (packet->curr_inf)->hops.at (packet->cur_hopf);
  NS_ASSERT (GET_HOP_ISD (hopf) == isd_number);
  NS_ASSERT (GET_HOP_AS (hopf) == as_number);
  bool reverse = packet->path_reversed ^ packet->path.at (packet->curr_inf)->reverse;

  uint16_t as_if_to_send;
  if (reverse)
    {
      as_if_to_send = GET_HOP_ING_IF (hopf);
    }
  else
    {
      as_if_to_send = GET_HOP_EG_IF (hopf);
    }

  if (received_from_local_as)
    {
      if (packet->path_reversed)
        {
          packet->cur_hopf--;
        }
      else
        {
          packet->cur_hopf++;
        }
    }

  NS_ASSERT (packet->cur_hopf >= 0);
  NS_ASSERT (packet->cur_hopf < packet->path.at (packet->curr_inf)->hops.size ());

  uint16_t local_if_to_send = forwarding_table_to_other_as_ifaces.at (as_if_to_send);

  // Update probe packets that pass through
  if (packet->payload_type == PayloadType::SCMP && std::get<Scmp> (packet->payload).type == PROBE)
    {
      UpdateCiaoProbe (packet, local_if_to_send);
    }

  // // NOTE: Quick and dirty way demonstrate bandwidth squeezing using congestion alerts
  // if (as_number == 0 && as_if_to_send == 1 && packet->payload_type == PayloadType::SCMP)
  //   {
  //     if (local_time > Seconds (1950.0) && local_time < Seconds (2100.0))
  //       {
  //         ReturnCongestionAlert (packet, as_if_to_send);
  //         return;
  //       }
  //   }

  ScheduleForSend (local_if_to_send, packet);
}

void
BorderRouter::UpdateCiaoProbe (ScionPacket *packet, uint16_t local_if)
{
  Scmp &scmp = std::get<Scmp> (packet->payload);
  BottleneckProbe &probe = std::get<BottleneckProbe> (scmp.data);

  int64_t transmission_delay = transmission_delays[local_if].ToInteger (Time::Unit::PS);
  unsigned int num_flows = estimated_num_flows[local_if] + 1; // Account for the potential new flow
  double total_bw = 8000.0 / transmission_delay; // in Gbps
  double fair_share = total_bw / num_flows;

  if (fair_share < probe.bottleneck_share || probe.interface_id == -1)
    {
      probe.ia = ia_addr;
      probe.interface_id = local_if;
      probe.bottleneck_share = fair_share;
      probe.bottleneck_num_flows = num_flows;
    }

  Time queuing_delay = transmission_delays[local_if] * transmission_queues_lengths[local_if];
  probe.cum_queueing_delay += queuing_delay.ToInteger (Time::Unit::US);
}

int64_t
BorderRouter::GetBwdGbit (uint16_t local_if)
{
  int64_t transmission_delay = transmission_delays.at (local_if).ToInteger (Time::Unit::PS);
  if (transmission_delay <= 0)
    {
      std::cout << "Transmission delay was 0. Transmission delays should be set for bandwidth "
                   "estimation. Assuming bwd = 400Gbps."
                << " Note that a too large time resolution might result in transmission delay 0."
                << std::endl;
      transmission_delay = 20;
    }
  return 8000 / transmission_delay;
}

uint16_t
BorderRouter::GetLocalIfFromASIf (uint16_t as_if)
{
  return forwarding_table_to_other_as_ifaces.at (as_if);
}

void
BorderRouter::SendBackgroundPacket (uint32_t size, ia_t dst_ia,
                                    std::vector<const ns3::PathSegment *> path, uint16_t inf,
                                    uint16_t hopf)
{
  Payload payload = {};
  PayloadType payload_type = PayloadType::BACKGROUND_TRAFFIC;
  ScionPacket *packet = CreateScionPacket (payload, payload_type, dst_ia, 0, size, path);
  packet->curr_inf = inf;
  packet->cur_hopf = hopf;
  SendScionPacket (packet);
}

} // namespace ns3