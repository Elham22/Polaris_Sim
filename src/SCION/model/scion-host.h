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

#ifndef SCION_SIMULATOR_SCION_HOST_H
#define SCION_SIMULATOR_SCION_HOST_H

#include <unordered_map>

#include "ns3/nstime.h"

#include "path-segment.h"
#include "scion-capable-node.h"
#include "scion-packet.h"

namespace ns3 {

class App;

struct AppInfo
{
  std::vector<const ns3::PathSegment *> path; // path over which to send response
  app_packet_id_t packet_id_start = 0;
  app_packet_id_t packet_id_last = 0;
  uint32_t num_packets = 0;
  uint64_t bytes_received = 0;
  int64_t aggregated_latencies = 0;
};

class ScionHost : public ScionCapableNode
{
public:
  ScionHost (uint32_t system_id, uint16_t isd_number, uint16_t as_number, host_addr_t local_address,
             double latitude, double longitude, ScionAs *as)
      : ScionCapableNode (system_id, isd_number, as_number, local_address, latitude, longitude, as)
  {
  }

  static void PrintPath (std::vector<const PathSegment *> the_path);
  void SendArbitraryPacket (ia_t dst_ia, host_addr_t dst_host);
  void StartApplication (std::string app_type, ia_t dst_ia, host_addr_t dst_host,
                         double backgroundBwdFactor, bool enable_logging);
  void SendProbes (uint32_t expected_bandwidth);
  void SendAppPacket (App *app, Payload payload, PayloadType payload_type, uint32_t size,
                      std::vector<const ns3::PathSegment *> path);
  void PrintAppsEval ();
  void PrintAddress ();

  std::vector<App *> apps;

protected:
  cached_path_segs_dataset_t cached_up_path_segments;
  cached_path_segs_dataset_t cached_core_path_segments;
  cached_path_segs_dataset_t cached_down_path_segments;

  uint32_t app_info_period_s = 2; // how long between app responses
  std::map<std::tuple<ia_t, host_addr_t, app_id_t>, AppInfo> app_infos;

  virtual void ProcessReceivedPacket (uint16_t local_if, ScionPacket *packet,
                                      Time receive_time) override;
  virtual void ModifyPktUponSend (ScionPacket *packet) override;
  void RemoveExpiredSegments ();
  void SearchAllInCachedSegments (ia_t dst_ia,
                                  std::vector<std::vector<const PathSegment *>> &paths);
  void SearchInCachedSegments (ia_t dst_ia, std::vector<const PathSegment *> &path,
                               std::vector<uint8_t> &shortcuts);
  void RequestForPathSegments (ia_t dst_ia);
  void SendRequestForPathSegments (PathSegmentType seg_type, ia_t src_ia, ia_t dst_ia);

  void ReceiveRegisteredPathSegments (PathSegmentType seg_type, ia_t src_ia, ia_t dst_ia,
                                      const reg_path_segs_to_one_as_t *path_segments);
  void ReceiveCachedPathSegments (PathSegmentType seg_type, ia_t src_ia, ia_t dst_ia,
                                  cached_path_segs_per_dst_t *path_seg);

  void CachePathSegment (PathSegmentType seg_type, ia_t src_ia, ia_t dst_ia, PathSegment *path_seg);

  void ReceiveProbeRequest (ia_t src_ia, host_addr_t src_addr,
                            std::vector<const ns3::PathSegment *> path, ProbeReq probe_req,
                            Time receive_time);
  void ReceiveProbeResponse (ia_t src_ia, host_addr_t src_addr,
                             ProbeResp probe_resp); // TODO score is just a placeholder
  void ReceiveAppData (ScionPacket *packet);
  void ReceiveAppResp (AppResp app_resp);
  void SendAppResp (std::tuple<ia_t, host_addr_t, app_id_t> key);
};
} // namespace ns3

#endif //SCION_SIMULATOR_SCION_HOST_H
