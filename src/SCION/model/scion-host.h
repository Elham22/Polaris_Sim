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
class ScionHost : public ScionCapableNode
{
public:
  ScionHost (uint32_t system_id, uint16_t isd_number, uint16_t as_number, host_addr_t local_address,
             double latitude, double longitude, ScionAs *as)
      : ScionCapableNode (system_id, isd_number, as_number, local_address, latitude, longitude, as)
  {
  }

  void SendArbitraryPacket (ia_t dst_ia, host_addr_t dst_host);

protected:
  cached_path_segs_dataset_t cached_up_path_segments;
  cached_path_segs_dataset_t cached_core_path_segments;
  cached_path_segs_dataset_t cached_down_path_segments;

  virtual void ProcessReceivedPacket (uint16_t local_if, ScionPacket *packet,
                                        Time receive_time) override;
  virtual void ModifyPktUponSend (ScionPacket *packet) override;
  void RemoveExpiredSegments ();
  void SearchInCachedSegments (ia_t dst_ia, std::vector<const PathSegment *> &path,
                                  std::vector<uint8_t> &shortcuts);
  void RequestForPathSegments (ia_t dst_ia);
  void SendRequestForPathSegments (PathSegmentType seg_type, ia_t src_ia, ia_t dst_ia);

  void ReceiveRegisteredPathSegments (PathSegmentType seg_type, ia_t src_ia, ia_t dst_ia,
                                         const reg_path_segs_to_one_as_t *path_segments);
  void ReceiveCachedPathSegments (PathSegmentType seg_type, ia_t src_ia, ia_t dst_ia,
                                     cached_path_segs_per_dst_t *path_seg);

  void CachePathSegment (PathSegmentType seg_type, ia_t src_ia, ia_t dst_ia,
                           PathSegment *path_seg);
};
} // namespace ns3

#endif //SCION_SIMULATOR_SCION_HOST_H
