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
#include <unordered_set>

#include "ns3/nstime.h"
#include "ns3/object.h"

#include "path-segment.h"

namespace ns3 {
typedef uint16_t host_addr_t;
typedef uint32_t packet_id_t;

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
  NTP_RESP = 8
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

union Payload {
  PathReqFromHost path_req_from_host;
  RegPathsFromLocalPs registered_paths_from_local_ps;
  ListOfAllASes list_of_all_ases;
  NtpReqOrResp ntp_req_or_resp;
};

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

  ScionPacket (ScionCapableNode *const packet_originator, packet_id_t id)
      : packet_originator (packet_originator), id (id)
  {
  }
};

} // namespace ns3
#endif //SCION_SIMULATOR_SCION_PACKET_H
