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
#include "apps/rtc-app.cc"

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

  // make sure this path segment is not already in the cache
  for (auto const &[key, path_segment] : *cached_path_segs_data_set->at (dst_ia)->at (src_ia))
    {
      if (path_segment == path_seg)
        {
          return;
        }
    }
  cached_path_segs_data_set->at (dst_ia)->at (src_ia)->insert (
      std::make_pair (path_seg->hops.size (), path_seg));
}

void
ScionHost::SendRequestForPathSegments (PathSegmentType seg_type, ia_t src_ia, ia_t dst_ia)
{
  PayloadType payload_type = PayloadType::PATH_REQ_FROM_HOST;

  Payload payload = PathReqFromHost{
      .src_ia = src_ia,
      .dst_ia = dst_ia,
      .seg_type = seg_type,
  };

  ScionPacket *packet = CreateScionPacket (payload, payload_type, ia_addr, 1, 0);
  SendScionPacket (packet);
}

void
ScionHost::SearchAllInCachedSegments (ia_t dst_ia,
                                      std::vector<std::vector<const PathSegment *>> &paths)
{
  if (dst_ia == ia_addr)
    {
      return;
    }
  if (cached_core_path_segments.find (dst_ia) != cached_core_path_segments.end () &&
      cached_core_path_segments.at (dst_ia)->find (ia_addr) !=
          cached_core_path_segments.at (dst_ia)->end ())
    {
      // currently only considers only-core topologies
      auto const segments = cached_core_path_segments.at (dst_ia)->at (ia_addr);
      for (auto const seg : *segments)
        {
          std::vector<const PathSegment *> the_path;
          the_path.push_back (seg.second);
          paths.push_back (the_path);
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

  if (auto *payload = std::get_if<RegPathsFromLocalPs> (&packet->payload))
    {
      ReceiveRegisteredPathSegments (payload->seg_type, payload->src_ia, payload->dst_ia,
                                     payload->registered_path_segments);
    }
  else if (auto *payload = std::get_if<ProbeReq> (&packet->payload))
    {
      // ReceiveProbeRequest (packet->src_ia, packet->src_host, packet->path,
      //                      packet->payload.probe_req, receive_time);
      ReceiveProbeRequest (packet->src_ia, packet->src_host, packet->path, *payload, receive_time);
    }
  else if (auto *payload = std::get_if<ProbeResp> (&packet->payload))
    {
      ReceiveProbeResponse (packet->src_ia, packet->src_host, *payload);
    }
  else if (auto *payload = std::get_if<AppData> (&packet->payload))
    {
      ReceiveAppData (packet, payload);
    }
  else if (auto *payload = std::get_if<AppProbe> (&packet->payload))
    {
      auto app_id = payload->app_id;

      if (apps.find (app_id) != apps.end ())
        {
          RTCApp *rtc_app = dynamic_cast<RTCApp *> (apps.at (app_id));
          if (rtc_app != nullptr)
            {
              rtc_app->ReceiveProbeResponse (*payload);
            }
          else
            {
              NS_FATAL_ERROR ("App with id " << app_id << " is not an RTC app");
            }
        }
      else
        {
          ReturnAppProbe (packet->src_ia, packet->src_host, packet->path, *payload);
        }
    }
  else if (auto *payload = std::get_if<AppResp> (&packet->payload))
    {
      ReceiveAppResp (*payload);
    }
  else if (auto *payload = std::get_if<ScmpReqOrResp> (&packet->payload))
    {
      // TODO: SCMP packet is just addressed to our host, we don't know which
      // application. If we want to run many applications on one host we need a
      // better concept here.
      // for (auto app : apps)
      //   {
      //     app.second->HandleSCMP (*payload);
      //   }
    }
  else
    {
      NS_FATAL_ERROR ("Unknown payload type: " << packet->payload_type);
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
ScionHost::ReturnAppProbe (ia_t src_ia, host_addr_t src_addr,
                           std::vector<const ns3::PathSegment *> path, AppProbe app_probe)
{
  PayloadType payload_type = PayloadType::APPLICATION_PROBE;

  // Everything stays the same, we can just set the RX time and send it back
  app_probe.time_rx = local_time.ToInteger (Time::Unit::US);

  Payload payload = app_probe;

  ScionPacket *packet = CreateScionPacket (payload, payload_type, src_ia, src_addr, 0, path);
  packet->path_reversed = true;
  packet->curr_inf = path.size () - 1;
  packet->cur_hopf = path.at (packet->curr_inf)->hops.size () - 1;
  SendScionPacket (packet);
  // std::cout << "[host] Sent response to probe from " << src_ia << ":" << src_addr << std::endl;
}

void
ScionHost::PrintPath (std::vector<const PathSegment *> the_path)
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
ScionHost::PrintAppsEval ()
{
  std::cout << "Host at " << isd_number << ":" << as_number << ":" << local_address << " has "
            << apps.size () << " applications." << std::endl;
  for (auto app : apps)
    {
      app.second->PrintResults ();
    }
}

void
ScionHost::SendArbitraryPacket (ia_t dst_ia, host_addr_t dst_host)
{
  Payload payload = {};
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
      PrintPath (the_path);

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
ScionHost::StartApplication (std::string app_type, uint32_t app_id, ia_t dst_ia,
                             host_addr_t dst_host, double backgroundBwdFactor,
                             uint32_t runtime_config)
{
  std::vector<const PathSegment *> the_path;
  /*ScionHost::active_path = the_path;
  ScionHost::app_dst_ia = dst_ia;
  ScionHost::app_dst_host = dst_host;*/

  std::vector<std::vector<const PathSegment *>> all_paths;
  SearchAllInCachedSegments (dst_ia, all_paths);

  if (!all_paths.empty ())
    {
      std::cout << "Paths found: " << std::endl;
      for (auto const &path : all_paths)
        {
          PrintPath (path);
        }
    }

  if (dst_ia == ia_addr || all_paths.size () != 0)
    {
      App *app;
      if (app_type == "video conference active")
        {
          app = new VideoConferenceApp (this, app_id, ia_addr, dst_ia, dst_host, all_paths,
                                        runtime_config);
        }
      else if (app_type == "video conference passive")
        {
          app = new VCAPassive (this, app_id, ia_addr, dst_ia, dst_host, all_paths, runtime_config);
        }
      else if (app_type == "video conference naive")
        {
          app = new VCANaive (this, app_id, ia_addr, dst_ia, dst_host, all_paths, runtime_config);
        }
      else if (app_type.find ("video conference given:") == 0)
        {
          app = new VCAGiven (this, app_id, ia_addr, dst_ia, dst_host, all_paths,
                              app_type.substr (23), runtime_config);
        }
      else if (app_type == "general traffic")
        {
          app = new GeneralTrafficApp (this, app_id, ia_addr, dst_ia, dst_host, all_paths,
                                       runtime_config);
        }
      else if (app_type == "rtc")
        {
          app = new RTCApp (this, app_id, ia_addr, dst_ia, dst_host, all_paths, runtime_config);
        }
      else
        {
          app = new App (this, app_id, ia_addr, dst_ia, dst_host, all_paths, runtime_config);
        }

      if (apps.find (app_id) != apps.end ())
        {
          NS_FATAL_ERROR ("App with id " << app_id << " already exists");
        }
      apps[app_id] = app;

      BackgroundTrafficApp::AddBackgroundTraffic (all_paths, backgroundBwdFactor);
      app->StartAppTrafficDelayed (
          Seconds (15)); // 15s delay to allow background traffic to reach steady state
    }
  else
    {
      // no paths registered, request paths
      RequestForPathSegments (dst_ia);
      Simulator::Schedule (MilliSeconds (300), &ScionHost::StartApplication, this, app_type, app_id,
                           dst_ia, dst_host, backgroundBwdFactor, runtime_config);
    }
}

void
ScionHost::ReceiveProbeRequest (ia_t src_ia, host_addr_t src_addr,
                                std::vector<const ns3::PathSegment *> path, ProbeReq probe_req,
                                Time receive_time)
{
  PayloadType payload_type = PayloadType::QOS_PROBE_RESP;
  Payload payload = ProbeResp{
      .app_id = probe_req.app_id,
      .probe_id = probe_req.probe_id,
      .src_ia = ia_addr,
      .src_host_addr = local_address,
      .time_recv = receive_time.ToInteger (Time::Unit::MS),
      .score = 0,
  };

  ScionPacket *packet = CreateScionPacket (payload, payload_type, src_ia, src_addr, 0, path);
  packet->path_reversed = true;
  packet->curr_inf = path.size () - 1;
  packet->cur_hopf = path.at (packet->curr_inf)->hops.size () - 1;
  SendScionPacket (packet);
}

void
ScionHost::ReceiveProbeResponse (ia_t src_ia, host_addr_t src_addr, ProbeResp probe_resp)
{
  apps.at (probe_resp.app_id)->ReceiveProbeResponse (probe_resp);
}

void
ScionHost::SendAppPacket (App *app, Payload payload, PayloadType payload_type, uint32_t size,
                          std::vector<const ns3::PathSegment *> path)
{
  ScionPacket *packet =
      CreateScionPacket (payload, payload_type, app->dst_ia, app->dst_host_addr, size, path);
  SendScionPacket (packet);
}

void
ScionHost::ReceiveAppData (ScionPacket *packet, AppData *data)
{
  std::cout << GetLogPrefix () << "Receiving app data packet from " << data->app_id << " via path "
            << data->path_id << ", frame_no: " << data->frame_no << ", seq_no: " << data->seq_no
            << std::endl;

  app_connection_key_t key =
      std::make_tuple (packet->src_ia, packet->src_host, data->app_id, data->path_id);

  // New connection
  if (connection_infos.find (key) == connection_infos.end ())
    {
      std::cout << GetLogPrefix () << "Connection opened: New packets arrived for app_id "
                << std::get<2> (key) << " on path " << std::get<3> (key) << std::endl;
      ConnectionInfo connection;
      connection.seq_no_start = data->seq_no;
      connection.frame_no = data->frame_no;
      connection.path = packet->path;
      connection.report = new PacketsReport ();
      connection_infos[key] = connection;
      Simulator::Schedule (connection_timeout, &ScionHost::CheckConnectionTimeout, this, key);
    }

  // Add received packet to statistics for current interval
  ConnectionInfo *connection = &connection_infos.at (key);

  if (data->frame_no < connection->frame_no)
    {
      std::cout << GetLogPrefix () << "Received packet from old frame " << data->frame_no
                << " instead of " << connection->frame_no << std::endl;
      return;
    }

  connection->last_update = local_time;
  connection->num_packets++;
  connection->bytes_received += packet->size;

  if (connection->seq_no_last < data->seq_no)
    {
      connection->seq_no_last = data->seq_no;
    }
  auto latency = local_time.ToInteger (Time::Unit::US) - data->timestamp;
  connection->aggregated_latencies += latency;

  // Keep updating ecn. Last packet to go into the report will determine what
  // sender will see.
  connection->ecn = packet->ecn;

  auto *report = connection->report;

  report->AddPacket (PacketRecord{
      .time_sent = MicroSeconds (data->timestamp),
      .time_received = local_time,
      .seq_no = data->seq_no,
      .frame_no = data->frame_no,
      .size = packet->size,
  });

  if (report->IsFrameComplete () || report->Age () > max_report_interval)
    {
      SendAppResp (key);
    }
}

void
ScionHost::CheckConnectionTimeout (app_connection_key_t key)
{
  if (connection_infos.find (key) == connection_infos.end ())
    {
      return;
    }

  ConnectionInfo *connection = &connection_infos.at (key);

  Time time_since_last_update = local_time - connection->last_update;
  if (time_since_last_update > connection_timeout)
    {
      delete connection->report;
      connection_infos.erase (key);
      std::cout << GetLogPrefix () << "Connection closed: No update in "
                << connection_timeout.GetSeconds () << " seconds from app " << std::get<2> (key)
                << " on path " << std::get<3> (key) << std::endl;
      return;
    }

  Simulator::Schedule (connection_timeout - time_since_last_update,
                       &ScionHost::CheckConnectionTimeout, this, key);
}

void
ScionHost::SendAppResp (app_connection_key_t key)
{
  if (connection_infos.find (key) == connection_infos.end ())
    {
      return;
    }

  ConnectionInfo *connection = &connection_infos.at (key);

  PayloadType payload_type = PayloadType::APPLICATION_RESP;
  auto num_packets_expected = connection->seq_no_last - connection->seq_no_start +
                              1; // +1 because id_start is id of first packet in period
  // std::cout << "app resp 1 from " << std::get<2> (key) << std::endl;

  AppResp app_resp;
  app_resp.app_id = std::get<2> (key);
  app_resp.loss =
      ((double) num_packets_expected - connection->num_packets) / num_packets_expected / 2;
  app_resp.avg_latency = ((double) connection->aggregated_latencies) / connection->num_packets;

  // Warn if avg_latency is larger than 5 seconds
  if (app_resp.avg_latency > 5000000)
    {
      std::cout << "[host] Very high latency detected " << app_resp.app_id << ", exp_num_packets "
                << num_packets_expected << ", packets arrived " << connection->num_packets
                << ", loss " << app_resp.loss << ", latency " << app_resp.avg_latency << std::endl;
    }

  app_resp.bytes_received = connection->bytes_received;
  app_resp.ecn = connection->ecn; // Notify sender of latest ecn status
  app_resp.timestamp = local_time.ToInteger (Time::Unit::US);
  app_resp.path_id = std::get<3> (key);
  app_resp.packets_report = connection->report;

  Payload payload = app_resp;

  ScionPacket *packet = CreateScionPacket (payload, payload_type, std::get<0> (key),
                                           std::get<1> (key), sizeof (AppResp), connection->path);
  packet->path_reversed = true;
  packet->curr_inf = packet->path.size () - 1;
  packet->cur_hopf = packet->path.at (packet->curr_inf)->hops.size () - 1;
  std::cout << GetLogPrefix () << "Sending app report to app " << app_resp.app_id << " via path "
            << app_resp.path_id << ", sequence numbers " << connection->seq_no_start << " to "
            << connection->seq_no_last << ", num_packets: " << connection->num_packets
            << ", loss: " << app_resp.loss << ", avg_latency: " << app_resp.avg_latency
            << ", bytes_received: " << connection->bytes_received << ", ecn: " << app_resp.ecn
            << std::endl;

  SendScionPacket (packet);

  // Reset values again for next window
  connection->aggregated_latencies = 0;
  connection->num_packets = 0;
  connection->bytes_received = 0;
  connection->seq_no_start = connection->seq_no_last + 1;

  // NOTE: Old report will be taken care of by sender. We don't drop app
  // response packets, so it should never be lost.
  connection->report = new PacketsReport ();
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

void
ScionHost::PrintAddress ()
{
  std::cout << isd_number << ":" << as_number << ":" << local_address;
}
} // namespace ns3
