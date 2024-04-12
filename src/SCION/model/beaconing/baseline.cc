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
 * Author: Seyedali Tabaeiaghdaei seyedali.tabaeiaghdaei@inf.ethz.ch,
 *         Christelle Gloor  christelle.gloor@inf.ethz.ch
 */

#include <omp.h>

#include "ns3/point-to-point-channel.h"

#include "baseline.h"
#include "src/SCION/model/utils.h"

namespace ns3 {

void
Baseline::DoInitializations (uint32_t num_ases, rapidxml::xml_node<> *xml_node,
                             const YAML::Node &config)
{
  BeaconServer::DoInitializations (num_ases, xml_node, config);
}

void
Baseline::CreateInitialStaticInfoExtension (static_info_extension_t &static_info_extension,
                                            uint16_t self_egress_if_no,
                                            const OptimizationTarget *optimization_target)
{
  static_info_extension.insert (std::make_pair (StaticInfoType::LATENCY, 0));
  static_info_extension.insert (
      std::make_pair (StaticInfoType::BW, as->inter_as_bwds.at (self_egress_if_no)));
}

void
Baseline::DisseminateBeacons (NeighbourRelation relation)
{
  uint32_t neighbors_cnt = as->neighbors.size ();
  omp_set_num_threads (num_core);
#pragma omp parallel for
  for (uint32_t i = 0; i < neighbors_cnt; ++i)
    {
      if (as->neighbors.at (i).second != relation)
        {
          continue;
        }

      uint16_t &remote_as_no = as->neighbors.at (i).first;
      const std::vector<uint16_t> &interfaces = as->interfaces_per_neighbor_as.at (remote_as_no);

      for (auto const &dst_as_beacons_pair : beacon_store)
        {
          const uint16_t &dst_as_no = dst_as_beacons_pair.first;
          auto const &equal_dst_as_beacons = dst_as_beacons_pair.second;

          uint32_t sent_count = 0;

          if (remote_as_no == dst_as_no)
            {
              continue;
            }

          for (auto const &len_beacons_pair : equal_dst_as_beacons)
            { // for each length
              if (sent_count >= MAX_BEACONS_TO_SEND)
                {
                  break;
                }

              auto const &beacons = len_beacons_pair.second;

              for (auto const &the_beacon : beacons)
                {
                  if (sent_count >= MAX_BEACONS_TO_SEND)
                    {
                      break;
                    }

                  if (!the_beacon->is_valid)
                    {
                      continue;
                    }

                  bool generates_loop = false;
                  for (auto const &link_info : the_beacon->the_path)
                    { // remove loops
                      if (UPPER_16_BITS (link_info) == remote_as_no)
                        {
                          generates_loop = true;
                          break;
                        }
                    }

                  if (generates_loop)
                    {
                      continue;
                    }

                  sent_count++;

                  // Iterate over all the valid interfaces of this remote AS and send the beacons
                  for (auto const &egress_interface_no : interfaces)
                    {
                      std::pair<uint16_t, ScionAs *> remote_as_if_pair =
                          as->GetRemoteAsInfo (egress_interface_no);

                      uint16_t remote_ingress_if_no = remote_as_if_pair.first;
                      ScionAs *remote_as = remote_as_if_pair.second;

                      ld latency = the_beacon->static_info_extension.at (StaticInfoType::LATENCY) +
                                   as->latencies_between_interfaces
                                       .at (LOWER_16_BITS (the_beacon->the_path.back ()))
                                       .at (egress_interface_no);
                      ld bwd = the_beacon->static_info_extension.at (StaticInfoType::BW) >
                                       (ld) as->inter_as_bwds.at (egress_interface_no)
                                   ? (ld) as->inter_as_bwds.at (egress_interface_no)
                                   : the_beacon->static_info_extension.at (StaticInfoType::BW);

                      static_info_extension_t static_info_extension;
                      static_info_extension.insert (
                          std::make_pair (StaticInfoType::LATENCY, latency));
                      static_info_extension.insert (std::make_pair (StaticInfoType::BW, bwd));

                      GenerateBeaconAndSend (the_beacon, egress_interface_no, remote_ingress_if_no,
                                             remote_as, static_info_extension);
                    }
                }
            }
        }
    }
}

std::tuple<bool, bool, bool, Beacon *, ld>
Baseline::AlgSpecificImportPolicy (Beacon &the_beacon, uint16_t sender_as,
                                   uint16_t remote_egress_if_no, uint16_t self_ingress_if_no,
                                   uint16_t now)
{
  uint16_t dst_as = UPPER_16_BITS (the_beacon.the_path.at (0));

  if (next_round_valid_beacons_count_per_dst_as.find (dst_as) ==
      next_round_valid_beacons_count_per_dst_as.end ())
    {
      return std::tuple<bool, bool, bool, Beacon *, ld> (true, false, false, NULL, 0);
    }

  if (this->next_round_valid_beacons_count_per_dst_as.at (dst_as) < MAX_BEACONS_TO_STORE)
    {
      return std::tuple<bool, bool, bool, Beacon *, ld> (true, false, false, NULL, 0);
    }

  return std::tuple<bool, bool, bool, Beacon *, ld> (false, false, false, NULL, 0);
}

void
Baseline::InsertToAlgorithmDataStructures (Beacon *the_beacon, uint16_t sender_as,
                                           uint16_t remote_egress_if_no,
                                           uint16_t self_ingress_if_no)
{
}

void
Baseline::DeleteFromAlgorithmDataStructures (Beacon *the_beacon, ld replacement_key)
{
}

void
Baseline::UpdateAlgorithmDataStructuresPeriodic (Beacon *the_beacon, bool invalidated)
{
}

} // namespace ns3