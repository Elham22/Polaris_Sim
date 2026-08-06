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
          return;
        }

      uint16_t local_if_to_send = forwarding_table_to_addresses_inside_as.at (packet->dst_host);
      ScheduleForSend (local_if_to_send, packet);

      return;
    }

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
  ScheduleForSend (local_if_to_send, packet);
}

} // namespace ns3