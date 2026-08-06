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

#include "scionlab-algo.h"
#include "ns3/point-to-point-channel.h"
#include "src/SCION/model/utils.h"
#include <omp.h>

namespace ns3 {

void
Scionlab::DoInitializations (uint32_t num_ases, rapidxml::xml_node<> *xml_node,
                             const YAML::Node &config)
{
  BeaconServer::DoInitializations (num_ases, xml_node, config);
}

void
Scionlab::CreateInitialStaticInfoExtension (static_info_extension_t &static_info_extension,
                                                uint16_t self_egress_if_no,
                                                const OptimizationTarget *optimization_target)
{
  static_info_extension.insert (std::make_pair (StaticInfoType::LATENCY, 0));
  static_info_extension.insert (
      std::make_pair (StaticInfoType::BW, as->inter_as_bwds.at (self_egress_if_no)));
}

void
Scionlab::DisseminateBeacons (NeighbourRelation relation)
{
  uint32_t neighbors_cnt = as->neighbors.size ();
  std::vector<Beacon *> valid_candidates;

  for (auto const &dst_as_beacons_pair : beacon_store)
    {
      auto const &equal_dst_as_beacons = dst_as_beacons_pair.second;

      std::vector<Beacon *> rest_of_beacons;
      std::vector<Beacon *> selected_beacons_per_dst;

      for (auto const &len_beacons_pair : equal_dst_as_beacons)
        { // for each length
          if (rest_of_beacons.size () + selected_beacons_per_dst.size () >= MAX_SET_SIZE)
            {
              break;
            }

          auto const &beacons = len_beacons_pair.second;
          for (auto const &the_beacon : beacons)
            {
              if (rest_of_beacons.size () + selected_beacons_per_dst.size () >= MAX_SET_SIZE)
                {
                  break;
                }

              if (!the_beacon->is_valid)
                {
                  continue;
                }

              if (selected_beacons_per_dst.size () < MAX_BEACONS_TO_SEND - 1)
                {
                  valid_candidates.push_back (the_beacon);
                  selected_beacons_per_dst.push_back (the_beacon);
                }
              else
                {
                  rest_of_beacons.push_back (the_beacon);
                }
            }
        }

      if (rest_of_beacons.size () + selected_beacons_per_dst.size () == MAX_BEACONS_TO_SEND)
        {
          valid_candidates.push_back (rest_of_beacons.at (0));
          continue;
        }

      if (rest_of_beacons.size () + selected_beacons_per_dst.size () < MAX_BEACONS_TO_SEND)
        {
          continue;
        }

      std::pair<Beacon *, int32_t> diversity_wr_to_selected =
          SelectMostDiverse (selected_beacons_per_dst, selected_beacons_per_dst.at (0));
      std::pair<Beacon *, int32_t> diversity_wr_to_rest =
          SelectMostDiverse (rest_of_beacons, selected_beacons_per_dst.at (0));

      if (diversity_wr_to_rest.second > diversity_wr_to_selected.second)
        {
          valid_candidates.push_back (diversity_wr_to_rest.first);
        }
      else
        {
          valid_candidates.push_back (rest_of_beacons.at (0));
        }
    }

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

      for (auto const &the_beacon : valid_candidates)
        {
          uint16_t dst_as_no = UPPER_16_BITS (the_beacon->the_path.at (0));

          if (remote_as_no == dst_as_no)
            {
              continue;
            }

          bool generates_loop = false;

          for (auto const &link_info : the_beacon->the_path)
            { // filter AS loops
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

          // filter isd loops
          uint16_t remote_isd_no = as->GetRemoteAsInfo (interfaces.back ()).second->isd_number;
          if (remote_isd_no != as->isd_number)
            {
              for (uint16_t isd_number : the_beacon->the_isd_path)
                {
                  if (isd_number == remote_isd_no)
                    {
                      generates_loop = true;
                      break;
                    }
                }
            }

          if (generates_loop)
            {
              continue;
            }

          // Iterate over all the valid interfaces of this remote AS and send the beacons
          for (auto const &egress_interface_no : interfaces)
            {
              std::pair<uint16_t, ScionAs *> remote_as_if_pair =
                  as->GetRemoteAsInfo (egress_interface_no);

              uint16_t remote_ingress_if_no = remote_as_if_pair.first;
              ScionAs *remote_as = remote_as_if_pair.second;

              ld latency =
                  the_beacon->static_info_extension.at (StaticInfoType::LATENCY) +
                  as->latencies_between_interfaces.at (LOWER_16_BITS (the_beacon->the_path.back ()))
                      .at (egress_interface_no);
              ld bwd = the_beacon->static_info_extension.at (StaticInfoType::BW) >
                               (ld) as->inter_as_bwds.at (egress_interface_no)
                           ? (ld) as->inter_as_bwds.at (egress_interface_no)
                           : the_beacon->static_info_extension.at (StaticInfoType::BW);

              static_info_extension_t static_info_extension;
              static_info_extension.insert (std::make_pair (StaticInfoType::LATENCY, latency));
              static_info_extension.insert (std::make_pair (StaticInfoType::BW, bwd));

              GenerateBeaconAndSend (the_beacon, egress_interface_no, remote_ingress_if_no,
                                     remote_as, static_info_extension);
            }
        }
    }
}

std::tuple<bool, bool, bool, Beacon *, ld>
Scionlab::AlgSpecificImportPolicy (Beacon &the_beacon, uint16_t sender_as,
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
Scionlab::InsertToAlgorithmDataStructures (Beacon *the_beacon, uint16_t sender_as,
                                               uint16_t remote_egress_if_no,
                                               uint16_t self_ingress_if_no)
{
}

void
Scionlab::DeleteFromAlgorithmDataStructures (Beacon *the_beacon, ld replacement_key)
{
}

void
Scionlab::UpdateAlgorithmDataStructuresPeriodic (Beacon *the_beacon, bool invalidated)
{
}

std::pair<Beacon *, int32_t>
Scionlab::SelectMostDiverse (std::vector<Beacon *> &beacons, Beacon *the_beacon)
{
  if (beacons.size () == 0)
    {
      return std::make_pair (the_beacon, -1);
    }
  Beacon *diverse = NULL;
  int32_t max_diversity = -1;
  uint32_t min_len = std::numeric_limits<uint32_t>::max ();

  for (auto const &other_beacon : beacons)
    {
      int32_t diversity = CalcDiversity (the_beacon, other_beacon);
      uint32_t l = other_beacon->the_path.size ();

      if (diversity > max_diversity || (diversity == max_diversity && min_len > l))
        {
          diverse = other_beacon;
          min_len = l;
          max_diversity = diversity;
        }
    }
  return std::make_pair (diverse, max_diversity);
}

int32_t
Scionlab::CalcDiversity (Beacon *beacon1, Beacon *beacon2)
{
  int32_t diff = 0;

  for (uint64_t link_info : beacon1->the_path)
    {
      bool found = false;
      for (uint64_t other_link_info : beacon2->the_path)
        {
          if (link_info == other_link_info)
            {
              found = true;
              break;
            }
        }
      if (!found)
        {
          diff++;
        }
    }
  return diff;
}

} // namespace ns3
