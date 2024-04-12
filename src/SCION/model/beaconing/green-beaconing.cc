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

#include <omp.h>

#include "ns3/point-to-point-channel.h"

#include "green-beaconing.h"
#include "src/SCION/model/utils.h"

namespace ns3 {

void
GreenBeaconing::DoInitializations (uint32_t num_ases, rapidxml::xml_node<> *xml_node,
                                   const YAML::Node &config)
{
  BeaconServer::DoInitializations (num_ases, xml_node, config);

  beacons_per_dst_per_ing_if_sorted_by_pollution.resize (num_ases);

  for (uint32_t i = 0; i < num_ases; ++i)
    {
      beacons_per_dst_per_ing_if_sorted_by_pollution.at (i) =
          std::vector<std::multimap<ld, Beacon *>> ();
      beacons_per_dst_per_ing_if_sorted_by_pollution.at (i).resize (as->GetNDevices ());
      for (uint32_t j = 0; j < as->GetNDevices (); ++j)
        {
          beacons_per_dst_per_ing_if_sorted_by_pollution.at (i).at (j) =
              std::multimap<ld, Beacon *> ();
        }
    }
}

const std::vector<std::vector<std::multimap<ld, Beacon *>>> &
GreenBeaconing::GetBeaconsSortedByPollution () const
{
  return beacons_per_dst_per_ing_if_sorted_by_pollution;
}

void
GreenBeaconing::CreateInitialStaticInfoExtension (static_info_extension_t &static_info_extension,
                                                  uint16_t self_egress_if_no,
                                                  const OptimizationTarget *optimization_target)
{
  static_info_extension.insert (std::make_pair (StaticInfoType::LATENCY, 0));
  static_info_extension.insert (std::make_pair (StaticInfoType::CO2, 0));
}

std::tuple<bool, bool, bool, Beacon *, ld>
GreenBeaconing::AlgSpecificImportPolicy (Beacon &the_beacon, uint16_t sender_as,
                                         uint16_t remote_egress_if_no, uint16_t self_ingress_if_no,
                                         uint16_t now)
{
  uint16_t dst_as = UPPER_16_BITS (the_beacon.the_path.at (0));

  if (beacons_per_dst_per_ing_if_sorted_by_pollution.at (dst_as).at (self_ingress_if_no).size () <
      MAX_BEACONS_TO_STORE_PER_IFACE)
    {
      return std::tuple<bool, bool, bool, Beacon *, ld> (true, false, false, NULL, 0);
    }

  ld pollution_index = the_beacon.static_info_extension.at (StaticInfoType::CO2);

  std::multimap<ld, Beacon *>::reverse_iterator highest_previous_pollution_iterator =
      beacons_per_dst_per_ing_if_sorted_by_pollution.at (dst_as).at (self_ingress_if_no).rbegin ();
  ld highest_previous_pollution = highest_previous_pollution_iterator->first;
  if (highest_previous_pollution > pollution_index)
    {
      Beacon *to_be_removed_beacon = highest_previous_pollution_iterator->second;
      return std::tuple<bool, bool, bool, Beacon *, ld> (true, false, false, to_be_removed_beacon,
                                                         highest_previous_pollution);
    }
  return std::tuple<bool, bool, bool, Beacon *, ld> (false, false, false, NULL, 0);
}

void
GreenBeaconing::DeleteFromAlgorithmDataStructures (Beacon *the_beacon, ld replacement_key)
{
  uint16_t dst_as = UPPER_16_BITS (the_beacon->the_path.at (0));
  uint16_t self_ingress_if = LOWER_16_BITS (the_beacon->the_path.back ());
  auto &beacon_container =
      beacons_per_dst_per_ing_if_sorted_by_pollution.at (dst_as).at (self_ingress_if);
  for (auto it = beacon_container.lower_bound (replacement_key);
       it != beacon_container.upper_bound (replacement_key); ++it)
    {
      if (it->second == the_beacon)
        {
          beacon_container.erase (it--);
          break;
        }
    }
}

void
GreenBeaconing::InsertToAlgorithmDataStructures (Beacon *the_beacon, uint16_t sender_as,
                                                 uint16_t remote_egress_if_no,
                                                 uint16_t self_ingress_if_no)
{
  uint16_t dst_as = UPPER_16_BITS (the_beacon->the_path.at (0));
  uint16_t self_ingress_if = LOWER_16_BITS (the_beacon->the_path.back ());
  ld pollution_index = the_beacon->static_info_extension.at (StaticInfoType::CO2);
  beacons_per_dst_per_ing_if_sorted_by_pollution.at (dst_as)
      .at (self_ingress_if)
      .insert (std::make_pair (pollution_index, the_beacon));
}

void
GreenBeaconing::DisseminateBeacons (NeighbourRelation relation)
{
  uint32_t neighbors_cnt = as->neighbors.size ();
  omp_set_num_threads (num_core);
#pragma omp parallel for
  for (uint32_t i = 0; i < neighbors_cnt; ++i)
    { // Per neighbor AS

      if (as->neighbors.at (i).second != relation)
        {
          continue;
        }

      uint16_t remote_as_no = as->neighbors.at (i).first;
      for (auto const &dst_as_beacons_pair : beacon_store)
        { // Per destination AS
          uint16_t dst_as_no = dst_as_beacons_pair.first;
          const beacons_with_same_dst_as &beacons_to_the_dst_as = dst_as_beacons_pair.second;
          if (remote_as_no == dst_as_no)
            {
              continue;
            }

          std::multimap<
              ld, std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, static_info_extension_t>>
              selected_beacons;
          SelectBeaconsToDisseminatePerDstPerNbr (remote_as_no, dst_as_no, beacons_to_the_dst_as,
                                                  selected_beacons);

          for (auto const &the_tuple_pair : selected_beacons)
            {
              Beacon *the_beacon;
              uint16_t remote_ingress_if_no;
              uint16_t self_egress_if_no;
              ScionAs *remote_as;
              static_info_extension_t static_info_extension;

              std::tie (the_beacon, self_egress_if_no, remote_ingress_if_no, remote_as,
                        static_info_extension) = the_tuple_pair.second;

              GenerateBeaconAndSend (the_beacon, self_egress_if_no, remote_ingress_if_no, remote_as,
                                     static_info_extension);
            }
        }
    }
}

void
GreenBeaconing::SelectBeaconsToDisseminatePerDstPerNbr (
    uint16_t remote_as_no, uint16_t dst_as_no,
    const beacons_with_same_dst_as &beacons_to_the_dst_as,
    std::multimap<ld, std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, static_info_extension_t>>
        &pollution_index_map_to_beacon_and_metadata)
{
  std::map<uint16_t, std::multimap<ld, Beacon *>> valid_candidates;

  auto const &interfaces = as->interfaces_per_neighbor_as.at (remote_as_no);
  for (auto const &self_egress_if_no : interfaces)
    {
      valid_candidates.insert (std::make_pair (self_egress_if_no, std::multimap<ld, Beacon *> ()));
    }

  for (auto const &len_beacons_pair : beacons_to_the_dst_as)
    {
      auto const &beacons = len_beacons_pair.second;
      for (auto const &the_beacon : beacons)
        {
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

          auto const &interfaces = as->interfaces_per_neighbor_as.at (remote_as_no);
          for (auto const &self_egress_if_no : interfaces)
            {
              ld pollution_index =
                  the_beacon->static_info_extension.at (StaticInfoType::CO2) +
                  CalculatePollutionBetweenBorderRouters (
                      LOWER_16_BITS (the_beacon->the_path.back ()), self_egress_if_no);
              valid_candidates.at (self_egress_if_no)
                  .insert (std::make_pair (pollution_index, the_beacon));
            }
        }
    }

  for (auto const &iface_to_pollution_beacon_pair : valid_candidates)
    {
      uint16_t self_egress_if_no = iface_to_pollution_beacon_pair.first;
      int no_beacons_per_iface = 0;
      for (auto const &pollution_beacon_pair : iface_to_pollution_beacon_pair.second)
        {
          if (no_beacons_per_iface >= MAX_BEACONS_TO_SEND_PER_IFACE)
            {
              break;
            }
          no_beacons_per_iface++;

          ld pollution_index = pollution_beacon_pair.first;
          Beacon *the_beacon = pollution_beacon_pair.second;

          uint16_t remote_ingress_if_no = as->GetRemoteAsInfo (self_egress_if_no).first;
          ScionAs *remote_as = as->GetRemoteAsInfo (self_egress_if_no).second;

          ld latency =
              the_beacon->static_info_extension.at (StaticInfoType::LATENCY) +
              as->latencies_between_interfaces.at (LOWER_16_BITS (the_beacon->the_path.back ()))
                  .at (self_egress_if_no);

          static_info_extension_t static_info_extension;
          static_info_extension.insert (std::make_pair (StaticInfoType::LATENCY, latency));
          static_info_extension.insert (std::make_pair (StaticInfoType::CO2, pollution_index));

          pollution_index_map_to_beacon_and_metadata.insert (std::make_pair (
              pollution_index,
              std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, static_info_extension_t> (
                  the_beacon, self_egress_if_no, remote_ingress_if_no, remote_as,
                  static_info_extension)));
        }
    }
}

ld
GreenBeaconing::CalculatePollutionBetweenBorderRouters (uint16_t ingress_if, uint16_t egress_if)
{
  ld energy_resource_carbon_intensity = dirty_energy_ratio * 700 / 3.6e6; // gr/Joule
  ld path_energy_intensity = intra_as_energies.at (ingress_if).at (egress_if);

  ld pollution = path_energy_intensity * energy_resource_carbon_intensity;

  return pollution;
}

void
GreenBeaconing::UpdateAlgorithmDataStructuresPeriodic (Beacon *the_beacon, bool invalidated)
{
  if (invalidated)
    {
      DeleteFromAlgorithmDataStructures (
          the_beacon, the_beacon->static_info_extension.at (StaticInfoType::CO2));
    }
}

} // namespace ns3
