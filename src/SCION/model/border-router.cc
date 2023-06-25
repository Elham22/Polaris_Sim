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
      /*std::cout << packet->id << " src " << packet->src_ia << ":" << packet->src_host << " Hop AS "
                << GET_HOP_AS (packet->path.at (packet->curr_inf)->hops.at (packet->cur_hopf))
                << "(" << packet->curr_inf << ", " << packet->cur_hopf << "), "
                << as_number << std::endl;*/
      NS_ASSERT (GET_HOP_ISD (packet->path.at (packet->curr_inf)->hops.at (packet->cur_hopf)) ==
                 isd_number);
      NS_ASSERT (GET_HOP_AS (packet->path.at (packet->curr_inf)->hops.at (packet->cur_hopf)) ==
                 as_number);

      if (forwarding_table_to_addresses_inside_as.find (packet->dst_host) ==
          forwarding_table_to_addresses_inside_as.end ())
        {
          NS_LOG_FUNCTION ("Address not in the forwarding table");
          std::cout << "BR process received: Address not in the forwarding table" << std::endl;
          return;
        }

      uint16_t local_if_to_send = forwarding_table_to_addresses_inside_as.at (packet->dst_host);
      if (packet->payload_type == PayloadType::QOS_PROBE_REQ)
        {
          /*std::cout << "border router at " << ia_addr << ":" << local_address << "(" << longitude << ", "
                    << latitude << ") received probe " << packet->payload.probe_req.probe_id << " if " << if_rcv << std::endl;*/
          ProcessQosProbeReq (local_if_to_send, packet, true, 0, packet->cur_hopf, packet->curr_inf);
        }
      //std::cout << "sending locally, if " << local_if_to_send << std::endl;
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
    if (packet->payload_type == PayloadType::QOS_PROBE_REQ)
      {
        /*std::cout << "border router at " << ia_addr << ":" << local_address << "(" << longitude << ", "
                  << latitude << ") received probe " << packet->payload.probe_req.probe_id << " if " << if_rcv << std::endl;*/
        /*std::cout << packet->id << " Current AS " << as_number << ", as in packet path " << GET_HOP_AS (packet->path.at (packet->curr_inf)->hops.at (packet->cur_hopf))
                  << ", as in old path " << GET_HOP_AS (packet->path.at (old_inf)->hops.at (old_hopf)) << std::endl;*/
        ProcessQosProbeReq (local_if_to_send, packet, false, as_if_to_send, old_hopf, old_inf);
      }
  //std::cout << "sending inter-as, if " << local_if_to_send << " bwd " << as->inter_as_bwds.at (as_if_to_send) << std::endl;
  ScheduleForSend (local_if_to_send, packet);
}

void
BorderRouter::ProcessQosProbeReq (uint16_t local_if, ScionPacket *packet, bool isDestinationAS,
                                  uint16_t as_if_to_send, uint16_t old_hopf, uint16_t old_inf)
{

  // std::cout << "BR " << ia_addr << " (" << longitude << ", " << latitude << "), Process probe " << packet->payload.probe_req.probe_id << std::endl;

  PayloadType payload_type = PayloadType::QOS_PROBE_RESP;
  ProbeReq request = packet->payload.probe_req;
  Payload payload;
  payload.probe_resp.app_id = request.app_id;
  payload.probe_resp.probe_id = request.probe_id;
  payload.probe_resp.time_recv = local_time.ToInteger(Time::Unit::MS);

  int64_t avail_bwd_bytes = GetBwdGbit (local_if) * 0.125e9;
  payload.probe_resp.raw_bwd = avail_bwd_bytes;
  //std::cout << "Raw bwd " << payload.probe_resp.raw_bwd  << " Gbps" << std::endl;
  int64_t new_bwd = estimated_throughput.at (local_if) + request.expected_bandwidth;
  double new_loss = new_bwd < avail_bwd_bytes ? 0 : ((double) new_bwd - avail_bwd_bytes) / new_bwd;
  std::cout << "Loss estimation, id " << request.app_id << "|" << request.probe_id << ", raw_bwd " << avail_bwd_bytes << ", est " << estimated_throughput.at (local_if) << ", req "
            << request.expected_bandwidth << ", new " << new_bwd << ", loss " << new_loss << std::endl;
  payload.probe_resp.expected_loss = new_loss;
  
  // TODO additional score is currently not implemented
  payload.probe_resp.score = 0;

  payload.probe_resp.src_ia = ia_addr;
  payload.probe_resp.src_host_addr = local_address;
  ScionPacket *response_packet = CreateScionPacket (payload, payload_type, packet->src_ia, packet->src_host, sizeof (ProbeResp), packet->path, packet->shortcut_hopfs);
  response_packet->path_reversed = !packet->path_reversed;
  response_packet->cur_hopf = old_hopf;
  response_packet->curr_inf = old_inf;
  
  if (packet->src_ia == ia_addr)
    {
      SendScionPacket(response_packet);
    }
  else
    {
      // ProcessReceivedPacket is called here because the hop & interface fields have to be advanced by one. Easier to just call ProcessReceivedPacket
      // instead of copying the logic of advancing the fields.
      processing_queue_length++;
      ProcessReceivedPacket (local_if, response_packet, Simulator::Now());
    }
  
}

int64_t
BorderRouter::GetBwdGbit (uint16_t local_if)
{
  int64_t transmission_delay = transmission_delays.at (local_if).ToInteger (Time::Unit::PS);
  if (transmission_delay <= 0)
    {
      std::cout << "Transmission delay was 0. Transmission delays should be set for bandwidth estimation. Assuming bwd = 400Gbps."
                << " Note that a too large time resolution might result in transmission delay 0." << std::endl;
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
BorderRouter::SendBackgroundPacket (uint32_t size, ia_t dst_ia, std::vector<const ns3::PathSegment *> path,
                                    uint16_t inf, uint16_t hopf)
{
  Payload payload;
  PayloadType payload_type = PayloadType::BACKGROUND_TRAFFIC;
  ScionPacket *packet = CreateScionPacket (payload, payload_type, dst_ia, 0, size, path);
  packet->curr_inf = inf;
  packet->cur_hopf = hopf;
  SendScionPacket (packet);

}

} // namespace ns3