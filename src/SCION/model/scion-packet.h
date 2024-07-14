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

#ifndef SCION_SIMULATOR_SCION_PACKET_H
#define SCION_SIMULATOR_SCION_PACKET_H

#include "ns3/node.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include "path-segment.h"
#include "webrtc-cc/types.h"
#include <unordered_set>
#include <variant>

namespace ns3 {
typedef uint16_t host_addr_t;
typedef uint32_t packet_id_t;
typedef uint32_t app_packet_id_t;
typedef uint32_t app_id_t;
typedef uint32_t app_path_id_t;

class ScionCapableNode;

enum PayloadType {
  EMPTY = 0,
  PATH_REQ_FROM_HOST = 1,
  REG_PATHS_FROM_LOCAL_PS = 2,
  REG_PATHS_FROM_REMOTE_PS = 3,
  REQ_FOR_LIST_OF_ALL_CORE_ASES = 4,
  LIST_OF_ALL_CORE_ASES = 5,
  BROADCAST_LIST_OF_ALL_CORE_ASES = 6,
  NTP_REQ = 7,
  NTP_RESP = 8,
  QOS_PROBE_REQ = 9,
  QOS_PROBE_RESP = 10,
  APPLICATION_DATA = 11,
  APPLICATION_RESP = 12,
  APPLICATION_PROBE,
  BACKGROUND_TRAFFIC,
  SCMP,
};

enum SCMPType {
  // Error types
  DESTINATION_UNREACHABLE = 1,
  PACKET_TOO_BIG = 2,
  PARAMETER_PROBLEM = 4,
  EXTERNAL_INTERFACE_DOWN = 5,
  INTERNAL_CONNECTIVITY_DOWN = 6,
  // Informational types
  ECHO_REQUEST = 128,
  ECHO_REPLY = 129,
  TRACEROUTE_REQUEST = 130,
  TRACEROUTE_REPLY = 131,
  LINK_CONGESTED = 132,
};

struct PathReqFromHost
{
  ia_t src_ia, dst_ia;
  PathSegmentType seg_type;
};

struct RegPathsFromLocalPs
{
  const reg_path_segs_to_one_as_t *registered_path_segments;
  ia_t src_ia, dst_ia;
  PathSegmentType seg_type;
};

struct ListOfAllASes
{
  std::set<ia_t> *set_of_all_ases;
};

struct NtpReqOrResp
{
  int64_t t0, t1, t2, t3;
};

struct ProbeReq
{
  app_id_t app_id;
  uint32_t probe_id;
  uint64_t expected_bandwidth;
};

struct ProbeResp
{
  app_id_t app_id;
  uint32_t probe_id;
  ia_t src_ia; // ia_addr of the machine creating the response
  host_addr_t src_host_addr; // local addr of the machine creating the response
  int32_t raw_bwd; // total bandwidth available in Gbps
  double expected_loss; // expected loss rate if this path is chosen
  int64_t time_recv;

  /*  machines may give the proposed path a score based on their own view on the
      current network state. Allows for the ASes to run more sophisticated methods and
      send it back to the host.
      hosts can take these scores into consideration when choosing paths but don't have to.
  */
  double score;
};

struct AppData
{
  app_id_t app_id;
  app_path_id_t path_id; // identifies the sender path
  app_packet_id_t seq_no;
  uint32_t frame_no; // number of the video frame this packet is part of
  int64_t timestamp; // timestamp of sending in US
  Ptr<Packet> ip_packet; // IP Layer 3 packet for encapsulated IP traffic
};

enum class AppProbeType {
  HEARTBEAT = 0,
  LATENCY = 1,
  BANDWIDTH = 2,
};

struct AppProbe
{
  app_id_t app_id;
  AppProbeType type;
  app_path_id_t path_id; // identifies the sender path
  app_packet_id_t probe_id; // identifies the probe action
  app_packet_id_t probe_seq_no; // seq no of packets belonging to a probe
  int64_t time_tx; // transmission time of the probe, in µs
  int64_t time_rx; // time of arrival at the app receiver (one-way), in µs
  double bottleneck_share; // minimum fair share bw along the path, in Gbps
  uint32_t bottleneck_share_no_flows; // number of flows that share the minimum fair share
  uint64_t bottleneck_hop; // link where minimum was found
  uint64_t max_queuing_delay; // highest queuing delay along the path
};

struct AppResp
{
  app_id_t app_id;
  app_path_id_t path_id; // identifies the sender path
  app_packet_id_t seq_no; // separate sequence number for the responses
  int64_t timestamp; // timestamp of sending in US
  double avg_latency;
  double loss;
  uint8_t ecn; // explicit congestion notification
  uint64_t bytes_received;

  // Simplify by sending back report via pointer. Real implementation would use
  // some form of run-length encoding to minimize feedback overhead
  PacketsReport *packets_report;
};

struct ScmpReqOrResp
{
  SCMPType type;
  uint16_t code;
  // Fields not implemented: Checksum, InfoBlock, DataBlock
};

typedef std::variant<PathReqFromHost, RegPathsFromLocalPs, ListOfAllASes, NtpReqOrResp,
                     ScmpReqOrResp, ProbeReq, ProbeResp, AppData, AppResp, AppProbe>
    Payload;

struct ScionPacket
{
public:
  Time timestamp;

  ScionCapableNode *const packet_originator; // This field is used for memory management of packets

  std::vector<const PathSegment *> path;

  Payload payload;

  const packet_id_t id; // This field is used for memory management of packets

  ia_t src_ia, dst_ia;

  PayloadType payload_type;

  host_addr_t src_host, dst_host;

  uint16_t curr_inf, cur_hopf;

  uint16_t size; // size in bytes

  // The index of cross overs in each segment
  std::vector<uint8_t> shortcut_hopfs;

  bool path_reversed;

  // TODO: Think about where the following would be stored. For reference.
  // In IP it's the two LSB in the Traffic Class field.

  uint8_t ecn = 0; // Explicit Congestion Notification (ECN)

  uint8_t ecn_capable = 0; // End hosts able to handle ECN

  ScionPacket (ScionCapableNode *const packet_originator, packet_id_t id)
      : packet_originator (packet_originator), id (id)
  {
  }
};

} // namespace ns3
#endif //SCION_SIMULATOR_SCION_PACKET_H
