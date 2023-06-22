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

#include <vector>

#include "ns3/ptr.h"

#include "path-segment.h"
#include "path-server.h"
#include "scion-core-as.h"
#include "scion-host.h"
#include "apps/app.h"
#include "apps/general-traffic-app.h"
#include "apps/video-conference-app.h"

namespace ns3 {
NS_LOG_COMPONENT_DEFINE ("ScionHost");

void
ScionHost::ReceiveRegisteredPathSegments (PathSegmentType seg_type, ia_t src_ia, ia_t dst_ia,
                                             const reg_path_segs_to_one_as_t *path_segments)
{
  NS_LOG_FUNCTION ("I am host " << isd_number << ":" << as_number << ":" << local_address
                                << ". Registered paths fetched: from " << src_ia << " "
                                << GET_ISDN (src_ia) << ":" << GET_ASN (src_ia) << " to "
                                << GET_ISDN (dst_ia) << ":" << GET_ASN (dst_ia)
                                << " number of segments: " << path_segments->size ());

  for (auto const &key_path_segment_pair : *path_segments)
    {
      PathSegment *path_segment = key_path_segment_pair.second;
      if (path_segment->expiration_time > local_time.GetMinutes ())
        {
          CachePathSegment (seg_type, src_ia, dst_ia, path_segment);
        }
    }
}

void
ScionHost::ReceiveCachedPathSegments (PathSegmentType seg_type, ia_t src_ia, ia_t dst_ia,
                                         cached_path_segs_per_dst_t *path_seg)
{
}

void
ScionHost::RemoveExpiredSegments ()
{
}

void
ScionHost::RequestForPathSegments (ia_t dst_ia)
{
  uint16_t dst_isd = GET_ISDN (dst_ia);

  //std::cout << std::endl << "Host " << as_number << ":" << local_address << " requesting path segments" << std::endl;

  SendRequestForPathSegments (PathSegmentType::UP_SEG, ia_addr, 0);
  if (dst_isd == isd_number)
    {
      SendRequestForPathSegments (PathSegmentType::CORE_SEG, 0, 0);
      SendRequestForPathSegments (PathSegmentType::DOWN_SEG, 0, dst_ia);
    }
  else
    {
      SendRequestForPathSegments (PathSegmentType::CORE_SEG, 0, MAKE_IA (dst_isd, 0));
      SendRequestForPathSegments (PathSegmentType::DOWN_SEG, MAKE_IA (dst_isd, 0), dst_ia);
    }
}

void
ScionHost::CachePathSegment (PathSegmentType seg_type, ia_t src_ia, ia_t dst_ia,
                               PathSegment *path_seg)
{
  cached_path_segs_dataset_t *cached_path_segs_data_set;

  if (seg_type == PathSegmentType::CORE_SEG)
    {
      cached_path_segs_data_set = &cached_core_path_segments;
    }
  else if (seg_type == PathSegmentType::UP_SEG)
    {
      cached_path_segs_data_set = &cached_up_path_segments;
    }
  else
    {
      cached_path_segs_data_set = &cached_down_path_segments;
    }

  if (cached_path_segs_data_set->find (dst_ia) == cached_path_segs_data_set->end ())
    {
      cached_path_segs_data_set->insert (
          std::make_pair (dst_ia, new cached_path_segs_per_dst_t ()));
    }

  if (cached_path_segs_data_set->at (dst_ia)->find (src_ia) ==
      cached_path_segs_data_set->at (dst_ia)->end ())
    {
      cached_path_segs_data_set->at (dst_ia)->insert (
          std::make_pair (src_ia, new cached_path_segs_per_src_dst_t ()));
    }

  cached_path_segs_data_set->at (dst_ia)->at (src_ia)->insert (
      std::make_pair (path_seg->hops.size (), path_seg));
}

void
ScionHost::SendRequestForPathSegments (PathSegmentType seg_type, ia_t src_ia, ia_t dst_ia)
{
  PayloadType payload_type = PayloadType::PATH_REQ_FROM_HOST;

  Payload payload;
  payload.path_req_from_host.src_ia = src_ia;
  payload.path_req_from_host.dst_ia = dst_ia;
  payload.path_req_from_host.seg_type = seg_type;

  ScionPacket *packet = CreateScionPacket (payload, payload_type, ia_addr, 1, 0);
  SendScionPacket (packet);
}

void
ScionHost::SearchAllInCachedSegments(ia_t dst_ia, std::vector<std::vector <const PathSegment*>> &paths)
{
  if (dst_ia == ia_addr)
    {
      return;
    }
  if (cached_core_path_segments.find (dst_ia) != cached_core_path_segments.end () &&
      cached_core_path_segments.at(dst_ia)->find(ia_addr) != cached_core_path_segments.at (dst_ia)->end ())
    {
      // currently only considers only-core topologies
      auto const segments = cached_core_path_segments.at(dst_ia)->at(ia_addr);
      for (auto const seg: *segments)
      {
        std::vector<const PathSegment *> the_path;
        the_path.push_back(seg.second);
        paths.push_back(the_path);
      }
    }
}

void
ScionHost::SearchInCachedSegments (ia_t dst_ia, std::vector<const PathSegment *> &path,
                                      std::vector<uint8_t> &shortcuts)
{
  //std::cout << "Searching cached path segements from " << ia_addr << " to " << dst_ia << std::endl;
  if (dst_ia == ia_addr)
    {
      return;
    }

  int16_t dst_in_which_cache = -1;

  if (cached_core_path_segments.find (dst_ia) != cached_core_path_segments.end ())
    {
      dst_in_which_cache = 0;
    }
  else if (cached_up_path_segments.find (dst_ia) != cached_up_path_segments.end ())
    {
      dst_in_which_cache = 1;
    }
  else if (cached_down_path_segments.find (dst_ia) != cached_down_path_segments.end ())
    {
      dst_in_which_cache = 2;
    }

  if (dst_in_which_cache == -1)
    {
      return;
    }

  if (dst_in_which_cache == 0 && dynamic_cast<ScionCoreAs *> (as) != NULL &&
      cached_core_path_segments.at (dst_ia)->find (ia_addr) !=
          cached_core_path_segments.at (dst_ia)->end ())
    {
      path.push_back (cached_core_path_segments.at (dst_ia)->at (ia_addr)->begin ()->second);
      return;
    }

  if (dst_in_which_cache == 0 && dynamic_cast<ScionCoreAs *> (as) == NULL)
    {
      for (auto const &[core_seg_src_ia, core_path_segs] : *cached_core_path_segments.at (dst_ia))
        {
          if (cached_up_path_segments.find (core_seg_src_ia) != cached_up_path_segments.end ())
            {
              NS_ASSERT (cached_up_path_segments.at (core_seg_src_ia)->find (ia_addr) !=
                         cached_up_path_segments.at (dst_ia)->end ());

              path.push_back (
                  cached_up_path_segments.at (core_seg_src_ia)->at (ia_addr)->begin ()->second);
              path.push_back (core_path_segs->begin ()->second);

              return;
            }
        }
      return;
    }

  if (dst_in_which_cache == 1 && dynamic_cast<ScionCoreAs *> (as) == NULL)
    {
      NS_ASSERT (cached_up_path_segments.at (dst_ia)->find (ia_addr) !=
                 cached_up_path_segments.at (dst_ia)->end ());
      path.push_back (cached_up_path_segments.at (dst_ia)->at (ia_addr)->begin ()->second);
      return;
    }

  if (dst_in_which_cache == 2 && dynamic_cast<ScionCoreAs *> (as) != NULL)
    {
      if (cached_down_path_segments.at (dst_ia)->find (ia_addr) !=
          cached_down_path_segments.at (dst_ia)->end ())
        {
          path.push_back (
              cached_down_path_segments.find (dst_ia)->second->begin ()->second->begin ()->second);
          return;
        }

      for (auto const &[down_seg_src_ia, down_path_segs] : *cached_down_path_segments.at (dst_ia))
        {
          if (cached_core_path_segments.find (down_seg_src_ia) != cached_up_path_segments.end ())
            {
              path.push_back (cached_core_path_segments.at (down_seg_src_ia)
                                      ->begin ()
                                      ->second->begin ()
                                      ->second);
              path.push_back (down_path_segs->begin ()->second);

              return;
            }
        }
      return;
    }

  if (dst_in_which_cache == 2 && dynamic_cast<ScionCoreAs *> (as) == NULL)
    {
      for (auto const &[down_seg_src_ia, down_path_segs] : *cached_down_path_segments.at (dst_ia))
        {
          if (cached_core_path_segments.find (down_seg_src_ia) != cached_core_path_segments.end ())
            {
              for (auto const &[core_seg_src_ia, core_path_segs] :
                   *cached_core_path_segments.at (down_seg_src_ia))
                {
                  if (cached_up_path_segments.find (core_seg_src_ia) !=
                      cached_up_path_segments.end ())
                    {
                      path.push_back (cached_up_path_segments.at (core_seg_src_ia)
                                              ->at (ia_addr)
                                              ->begin ()
                                              ->second);
                      path.push_back (core_path_segs->begin ()->second);
                      path.push_back (down_path_segs->begin ()->second);

                      return;
                    }
                }
            }
        }
    }

  return;
}

void
ScionHost::ProcessReceivedPacket (uint16_t local_if, ScionPacket *packet, Time receive_time)
{
  NS_ASSERT (packet->dst_ia == ia_addr && packet->dst_host == local_address);
  NS_LOG_FUNCTION ("I am host " << isd_number << ":" << as_number << ":" << local_address
                                << ". Packet received from " << GET_ISDN (packet->src_ia) << ":"
                                << GET_ASN (packet->src_ia) << ":" << packet->src_host);
  /*std::cout << "I am host " << isd_number << ":" << as_number << ":" << local_address
                                << ". Packet received from " << GET_ISDN (packet->src_ia) << ":"
                                << GET_ASN (packet->src_ia) << ":" << packet->src_host
                                << ", type " << packet->payload_type << std::endl;*/

  ScionCapableNode::ProcessReceivedPacket (local_if, packet, receive_time);

  if (packet->payload_type == PayloadType::REG_PATHS_FROM_LOCAL_PS)
    {
      RegPathsFromLocalPs registered_paths_from_local_ps =
          packet->payload.registered_paths_from_local_ps;
      ReceiveRegisteredPathSegments (registered_paths_from_local_ps.seg_type,
                                     registered_paths_from_local_ps.src_ia,
                                     registered_paths_from_local_ps.dst_ia,
                                     registered_paths_from_local_ps.registered_path_segments);
    }
  if (packet->payload_type == PayloadType::QOS_PROBE_REQ)
    {
      ReceiveProbeRequest (packet->src_ia, packet->src_host, packet->path, packet->payload.probe_req, receive_time);
    }
  if (packet->payload_type == PayloadType::QOS_PROBE_RESP)
    {
      ReceiveProbeResponse (packet->src_ia, packet->src_host, packet->payload.probe_resp);
    }
  if (packet->payload_type == PayloadType::APPLICATION_DATA)
    {
      ReceiveAppData (packet);
    }
  if (packet->payload_type == PayloadType::APPLICATION_RESP)
    {
      ReceiveAppResp (packet->payload.app_resp);
    }

  packet->packet_originator->DestroyScionPacket (packet);
  /*
        if (packet->packet_originator == this) {
            NS_ASSERT(on_the_flight_packets.find(packet->id) != on_the_flight_packets.end());
            NS_ASSERT(&on_the_flight_packets.at(packet->id) == packet);
            NS_LOG_FUNCTION("I am host " << isd_number << ":" << as_number << ":" << local_address << ". Response received from " << GET_ISDN(packet->src_ia) << ":" << GET_ASN(packet->src_ia) << ":" << packet->src_host);

            DestroyScionPacket(packet);
            // The repose of a  previously-sent message has received; do whatever is necessary
        } else {
            ReturnScionPacket(packet);
        }
*/
}

void
ScionHost::PrintPath (std::vector<const PathSegment *> the_path)
{
  std::cout << "Printing path: ";
  for (auto const seg: the_path)
  {
    std::cout << "[";
    for (link_information const hop: seg->hops)
    {
      std::cout << "(" << GET_HOP_ISD (hop) << ":"
                                << GET_HOP_AS (hop) << ", ing: "
                                << GET_HOP_ING_IF (hop) << ", eg: "
                                << GET_HOP_EG_IF (hop) << "), ";
    }
    std::cout << "], ";
  }
  std::cout << std::endl;
}


void
ScionHost::PrintAppsEval ()
{
  std::cout << "Host at " << isd_number << ":" << as_number << ":" << local_address
            << " has " << apps.size () << " applications." << std::endl;
  for (auto app : apps)
    {
      app->PrintResults ();
    }
}

void
ScionHost::SendArbitraryPacket (ia_t dst_ia, host_addr_t dst_host)
{
  Payload payload;
  PayloadType payload_type = PayloadType::EMPTY;

  if (dst_ia == ia_addr)
    {
      ScionPacket *packet = CreateScionPacket (payload, payload_type, dst_ia, dst_host, 0);
      SendScionPacket (packet);
    }
  else
    {
      std::vector<const PathSegment *> the_path;
      std::vector<uint8_t> shortcuts;

      SearchInCachedSegments (dst_ia, the_path, shortcuts);
      PrintPath(the_path);

      if (the_path.size () != 0)
        {
          ScionPacket *packet =
              CreateScionPacket (payload, payload_type, dst_ia, dst_host, 0, the_path, shortcuts);
          SendScionPacket (packet);
        }
      else
        {
          RequestForPathSegments (dst_ia);
          Simulator::Schedule (MilliSeconds (300), &ScionHost::SendArbitraryPacket, this, dst_ia,
                               dst_host);
        }
    }
}

void
ScionHost::StartApplication (std::string app_type, ia_t dst_ia, host_addr_t dst_host)
{
  std::vector<const PathSegment *> the_path;
  /*ScionHost::active_path = the_path;
  ScionHost::app_dst_ia = dst_ia;
  ScionHost::app_dst_host = dst_host;*/

  std::vector<std::vector<const PathSegment *>> all_paths;
  SearchAllInCachedSegments (dst_ia, all_paths);
  std::cout << "Paths found: " << std::endl; 
  for (auto const path: all_paths) {
    PrintPath(path);
  }

  if (dst_ia == ia_addr || all_paths.size() != 0)
    {
      App *app;
      if (app_type == "video conference")
        {
          app = new VideoConferenceApp (this, apps.size(), ia_addr, dst_ia, dst_host, all_paths);
        }
      else if (app_type == "general traffic")
        {
          app = new GeneralTrafficApp (this, apps.size(), ia_addr, dst_ia, dst_host, all_paths);
        }
      else
        {
          app = new App (this, apps.size(), ia_addr, dst_ia, dst_host, all_paths);
        }
      apps.push_back(app);
      app->StartAppTraffic();
    }
  else
    {
      RequestForPathSegments (dst_ia);
      Simulator::Schedule (MilliSeconds (300), &ScionHost::StartApplication, this, app_type, dst_ia,
                            dst_host);
    }
}

void
ScionHost::ReceiveProbeRequest (ia_t src_ia, host_addr_t src_addr, std::vector<const ns3::PathSegment *> path,
                                ProbeReq probe_req, Time receive_time)
{
  PayloadType payload_type = PayloadType::QOS_PROBE_RESP;
  Payload payload;
  payload.probe_resp.app_id = probe_req.app_id;
  payload.probe_resp.probe_id = probe_req.probe_id;
  payload.probe_resp.time_recv = receive_time.ToInteger(Time::Unit::MS);
  payload.probe_resp.score = 0;
  payload.probe_resp.src_host_addr = local_address;
  payload.probe_resp.src_ia = ia_addr;

  ScionPacket *packet = CreateScionPacket(payload, payload_type, src_ia, src_addr, 0, path);
  packet->path_reversed = true;
  packet->curr_inf = path.size() - 1;
  packet->cur_hopf = path.at(packet->curr_inf)->hops.size() - 1;
  SendScionPacket(packet);
}

void
ScionHost::ReceiveProbeResponse (ia_t src_ia, host_addr_t src_addr, ProbeResp probe_resp) 
{
  apps.at(probe_resp.app_id)->ReceiveProbeResponse(probe_resp);
}

void
ScionHost::SendAppPacket (App *app, Payload payload, PayloadType payload_type, uint32_t size, std::vector<const ns3::PathSegment *> path)
{
  ScionPacket *packet = CreateScionPacket(payload, payload_type, app->dst_ia, app->dst_host_addr, size, path);
  SendScionPacket(packet);
}

void
ScionHost::ReceiveAppData (ScionPacket *packet)
{
  auto key = std::make_tuple (packet->src_ia, packet->src_host, packet->payload.app_data.app_id);
  if (app_infos.find (key) == app_infos.end ())
    {
      AppInfo info;
      info.packet_id_start = packet->payload.app_data.app_packet_id;
      info.path = packet->path;
      app_infos[key] = info;
      Simulator::Schedule (Seconds (app_info_period_s), &ScionHost::SendAppResp, this, key);
    }
  auto info = app_infos.at (key);
  info.num_packets++;
  info.bytes_received += packet->size;
  if (info.packet_id_last < packet->payload.app_data.app_packet_id)
  {
    info.packet_id_last = packet->payload.app_data.app_packet_id;
  }
  auto latency = local_time.ToInteger (Time::Unit::US) - packet->payload.app_data.timestamp;
  info.aggregated_latencies += latency;
  app_infos[key] = info;
}

void
ScionHost::SendAppResp (std::tuple <ia_t, host_addr_t, app_id_t> key)
{
  if (app_infos.find (key) == app_infos.end ())
    {
      return;
    }
  auto info = app_infos.at (key);
  PayloadType payload_type = PayloadType::APPLICATION_RESP;
  Payload payload;
  auto num_packets_expected = info.packet_id_last - info.packet_id_start + 1; // +1 because id_start is id of first packet in period
  if (num_packets_expected == 0)
    {
      // no packets arrived in interval. Stop sending updates.
      app_infos.erase (key);
      return;
    }
  payload.app_resp.app_id = std::get<2> (key);
  payload.app_resp.loss = ((double) num_packets_expected - info.num_packets) / num_packets_expected;
  payload.app_resp.avg_latency = ((double) info.aggregated_latencies) / info.num_packets;
  payload.app_resp.bytes_received = info.bytes_received;
  
  /*std::cout << "Sending app resp for app_id " << payload.app_resp.app_id << ", exp_num_packets " << num_packets_expected << ", packets arrived "
            << info.num_packets << ", loss " << payload.app_resp.loss << ", latency " << payload.app_resp.avg_latency << std::endl;*/

  ScionPacket *packet = CreateScionPacket (payload, payload_type, std::get<0> (key), std::get<1> (key), sizeof (AppResp), info.path);
  packet->path_reversed = true;
  packet->curr_inf = packet->path.size() - 1;
  packet->cur_hopf = packet->path.at(packet->curr_inf)->hops.size() - 1;
  SendScionPacket(packet);

  info.aggregated_latencies = 0;
  info.num_packets = 0;
  info.bytes_received = 0;
  info.packet_id_start = info.packet_id_last + 1;
  app_infos[key] = info;

  Simulator::Schedule (Seconds (app_info_period_s), &ScionHost::SendAppResp, this, key);
}


void
ScionHost::ReceiveAppResp (AppResp app_resp)
{
  apps.at (app_resp.app_id)->ReceiveAppResponse (app_resp);
}

void
ScionHost::ModifyPktUponSend (ScionPacket *packet)
{
}
} // namespace ns3
