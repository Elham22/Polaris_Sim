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

#include "on-demand-optimization.h"
#include "src/SCION/model/utils.h"
#include <omp.h>

namespace ns3 {

void
OnDemandOptimization::DoInitializations (uint32_t num_ases, rapidxml::xml_node<> *xml_node,
                                         const YAML::Node &config)
{
  BeaconServer::DoInitializations (num_ases, xml_node, config);
}

void
OnDemandOptimization::PerLinkInitializations (rapidxml::xml_node<> *cur_xml_link,
                                              const YAML::Node &config)
{
  uint32_t to = std::stoi (cur_xml_link->first_node ("to")->value ());
  uint32_t from = std::stoi (cur_xml_link->first_node ("from")->value ());

  NS_ASSERT (real_to_alias_as_no.at (to) == as->as_number ||
             real_to_alias_as_no.at (from) == as->as_number);

  std::string target_element_str = "_target";
  uint16_t interface_id = 0;
  uint16_t neighbor_as = 0;

  PropertyContainer p = ParseProperties (cur_xml_link);
  if (real_to_alias_as_no.at (to) == as->as_number)
    {
      target_element_str = "to" + target_element_str;
      interface_id = std::stoi (p.GetProperty ("to_if_id"));
      neighbor_as = real_to_alias_as_no.at (from);
    }
  else
    {
      target_element_str = "from" + target_element_str;
      interface_id = std::stoi (p.GetProperty ("from_if_id"));
      neighbor_as = real_to_alias_as_no.at (to);
    }

  ld latitude = std::stod (p.GetProperty ("latitude"));
  ld longitude = std::stod (p.GetProperty ("longitude"));
  std::pair<ld, ld> coordinates = std::make_pair (latitude, longitude);

  NS_ASSERT (coordinates == as->interfaces_coordinates.at (interface_id));

  rapidxml::xml_node<> *cur_xml_target = cur_xml_link->first_node (target_element_str.c_str ());
  while (cur_xml_target)
    {
      uint16_t target_id = std::stoi (cur_xml_target->value ());
      const OptimizationTarget *optimization_target =
          &set_of_optimization_targets_originated_from_this_as.at (target_id);
      uint16_t interface_group = optimization_target->target_if_group;

      if_to_push_based_optimization_targets_map.insert (
          std::make_pair (interface_id, optimization_target));
      if_to_if_group.insert (std::make_pair (interface_id, interface_group));

      if (interface_groups_connected_per_neighbor.find (neighbor_as) ==
          interface_groups_connected_per_neighbor.end ())
        {
          interface_groups_connected_per_neighbor.insert (
              std::make_pair (neighbor_as, std::unordered_map<uint16_t, std::vector<uint16_t>> ()));
        }
      if (interface_groups_connected_per_neighbor.at (neighbor_as).find (interface_group) ==
          interface_groups_connected_per_neighbor.at (neighbor_as).end ())
        {
          interface_groups_connected_per_neighbor.at (neighbor_as)
              .insert (std::make_pair (interface_group, std::vector<uint16_t> ()));
        }
      if (std::find (
              std::begin (
                  interface_groups_connected_per_neighbor.at (neighbor_as).at (interface_group)),
              std::end (
                  interface_groups_connected_per_neighbor.at (neighbor_as).at (interface_group)),
              interface_id) ==
          std::end (interface_groups_connected_per_neighbor.at (neighbor_as).at (interface_group)))
        {
          interface_groups_connected_per_neighbor.at (neighbor_as)
              .at (interface_group)
              .push_back (interface_id);
        }

      cur_xml_target = cur_xml_target->next_sibling (target_element_str.c_str ());
    }
#if NS3_ASSERT_ENABLE
  for (uint32_t i = 0; i < AS->interfaces_coordinates.size (); ++i)
    {
      NS_ASSERT (if_to_if_group.find (i) != if_to_if_group.end ());
      NS_ASSERT (if_to_push_based_optimization_targets_map.find (i) !=
                 if_to_push_based_optimization_targets_map.end ());
    }
#endif
}

void
OnDemandOptimization::InitiateBeaconsPerInterface (uint16_t self_egress_if_no, ScionAs *remote_as,
                                                   uint16_t remote_ingress_if_no)
{
  // push_based
  if (now < first_pull_based_interval)
    {
      for (auto it = if_to_push_based_optimization_targets_map.lower_bound (self_egress_if_no);
           it != if_to_push_based_optimization_targets_map.upper_bound (self_egress_if_no); ++it)
        {
          static_info_extension_t static_info_extension;
          CreateInitialStaticInfoExtension (static_info_extension, self_egress_if_no, it->second);
          GenerateBeaconAndSend (NULL, self_egress_if_no, remote_ingress_if_no, remote_as,
                                 static_info_extension, it->second, BeaconDirectionT::PUSH_BASED);
        }
    }

  // pull-based
  if (now >= first_pull_based_interval &&
      (now / beaconing_period.ToInteger (Time::MIN)) %
              pull_based_dissemination_to_initiation_frequency ==
          0)
    {
      for (auto it = if_to_pull_based_optimization_targets_map.lower_bound (self_egress_if_no);
           it != if_to_pull_based_optimization_targets_map.upper_bound (self_egress_if_no); ++it)
        {
          NS_ASSERT (it->second->set_of_forbidden_edges->find (as->as_number) !=
                     it->second->set_of_forbidden_edges->end ());
          if (it->second->set_of_forbidden_edges->at (as->as_number)->find (self_egress_if_no) !=
              it->second->set_of_forbidden_edges->at (as->as_number)->end ())
            {
              continue;
            }
          static_info_extension_t static_info_extension;
          CreateInitialStaticInfoExtension (static_info_extension, self_egress_if_no, it->second);
          GenerateBeaconAndSend (NULL, self_egress_if_no, remote_ingress_if_no, remote_as,
                                 static_info_extension, it->second, BeaconDirectionT::PULL_BASED);
        }
    }
}

void
OnDemandOptimization::CreateInitialStaticInfoExtension (
    static_info_extension_t &static_info_extension, uint16_t self_egress_if_no,
    const OptimizationTarget *optimization_target)
{
  for (auto const &criteria : optimization_target->criteria)
    {
      if (criteria.first == LATENCY)
        {
          static_info_extension.insert (std::make_pair (StaticInfoType::LATENCY, 0));
        }
      else if (criteria.first == BW)
        {
          static_info_extension.insert (
              std::make_pair (StaticInfoType::BW, as->inter_as_bwds.at (self_egress_if_no)));
        }
      else if (criteria.first == CO2)
        {
          static_info_extension.insert (std::make_pair (StaticInfoType::CO2, 0));
        }
    }
}

void
OnDemandOptimization::ExtendStaticInfoExtension (const Beacon *the_beacon,
                                                 uint16_t beacon_ingress_if_no,
                                                 uint16_t candidate_egress_if_no,
                                                 static_info_extension_t &propagation_static_info)
{
  for (auto const &criteria : the_beacon->optimization_target->criteria)
    {
      if (criteria.first == LATENCY)
        {
          ld latency = the_beacon->static_info_extension.at (StaticInfoType::LATENCY) +
                       as->latencies_between_interfaces.at (beacon_ingress_if_no)
                           .at (candidate_egress_if_no);
          propagation_static_info.insert (std::make_pair (StaticInfoType::LATENCY, latency));
        }
      else if (criteria.first == BW)
        {
          ld bw = the_beacon->static_info_extension.at (StaticInfoType::BW) >
                          (ld) as->inter_as_bwds.at (candidate_egress_if_no)
                      ? (ld) as->inter_as_bwds.at (candidate_egress_if_no)
                      : the_beacon->static_info_extension.at (StaticInfoType::BW);
          propagation_static_info.insert (std::make_pair (StaticInfoType::BW, bw));
        }
      else if (criteria.first == CO2)
        {
          propagation_static_info.insert (std::make_pair (StaticInfoType::CO2, 0));
        }
    }
}

void
OnDemandOptimization::DisseminateBeacons (NeighbourRelation relation)
{
  NS_ASSERT (now == (uint16_t) Simulator::Now ().ToInteger (Time::MIN));
  bool pull_based_dissemination = now >= first_pull_based_interval;
  bool push_based_dissemination = now < first_pull_based_interval;

  auto &beacons_grouped_by_optimization_targets_and_ingress_if =
      (push_based_dissemination)
          ? push_based_beacons_grouped_by_optimization_targets_and_ingress_if_group
          : pull_based_beacons_grouped_by_optimization_targets_and_ingress_if_group.at (
                pull_based_read);

  uint32_t neighbors_cnt = as->neighbors.size ();
  omp_set_num_threads (num_core);
#pragma omp parallel for schedule(dynamic)

  for (uint32_t i = 0; i < neighbors_cnt; ++i)
    { // Per neighbor AS
      if (as->neighbors.at (i).second != relation)
        {
          continue;
        }

      uint16_t remote_as_no = as->neighbors.at (i).first;

      for (auto const &[optimization_target, beacons_with_the_same_opt_target] :
           beacons_grouped_by_optimization_targets_and_ingress_if)
        {
          if (optimization_target->target_as == as->as_number)
            { // pull-based request to this AS
              continue;
            }

          std::unordered_map<uint16_t, std::multimap<ld,
                                                     std::tuple<Beacon *, uint16_t, uint16_t,
                                                                ScionAs *, static_info_extension_t>,
                                                     std::greater<ld>>>
              selected_beacons;

          SelectBeaconsToDisseminatePerTargetPerNbr (remote_as_no, beacons_with_the_same_opt_target,
                                                     optimization_target, selected_beacons);
          SendSelectedBeaconsPerTargetPerNbr (selected_beacons);
        }
    }

  if (pull_based_dissemination)
    {
      // returning pull-based beacons
      for (auto const &[optimization_target, beacons_with_the_same_opt_target] :
           pull_based_beacons_grouped_by_optimization_targets_and_ingress_if_group.at (
               pull_based_read))
        {
          if (optimization_target->target_as != as->as_number)
            {
              continue;
            }

          for (auto const &[beacon_ingress_if_group, score_beacons_map] :
               beacons_with_the_same_opt_target)
            {
              for (auto &[score, the_beacon] : score_beacons_map)
                {
                  if (the_beacon->expiration_time > now)
                    {
                      uint64_t first_hop = the_beacon->the_path.front ();
                      ScionAs *requesting_as = dynamic_cast<ScionAs *> (
                          PeekPointer (nodes.Get (UPPER_16_BITS (first_hop))));
                      requesting_as->ReceiveBeacon (*the_beacon, SECOND_LOWER_16_BITS (first_hop),
                                                    LOWER_16_BITS (first_hop),
                                                    SECOND_UPPER_16_BITS (first_hop));
                    }
                }
            }
        }
      pull_based_beacons_grouped_by_optimization_targets_and_ingress_if_group.at (pull_based_read)
          .clear ();
      non_requested_pull_based_beacon_container.at (pull_based_read).clear ();
    }
}

void
OnDemandOptimization::SelectBeaconsToDisseminatePerTargetPerNbr (
    uint16_t remote_as_no,
    const beacons_with_the_same_opt_target_t &beacons_with_the_same_opt_target,
    const OptimizationTarget *optimization_target,
    std::unordered_map<
        uint16_t,
        std::multimap<ld,
                      std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, static_info_extension_t>,
                      std::greater<ld>>> &selected_beacons)
{
  for (auto const &[beacon_ingress_if_group, score_beacons_map] : beacons_with_the_same_opt_target)
    {
      for (auto const &[score, the_beacon] : score_beacons_map)
        {
          if ((the_beacon->beacon_direction == BeaconDirectionT::PUSH_BASED &&
               !the_beacon->is_valid) ||
              (the_beacon->beacon_direction == BeaconDirectionT::PULL_BASED &&
               the_beacon->expiration_time < now))
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

          NS_ASSERT (!interface_groups_connected_per_neighbor.at (remote_as_no).empty ());
          for (auto const &[group, ifaces] :
               interface_groups_connected_per_neighbor.at (remote_as_no))
            {
              NS_ASSERT (!ifaces.empty ());
              for (auto const &candidate_egress_if_no : ifaces)
                {
                  if (the_beacon->optimization_target->set_of_forbidden_edges != NULL)
                    {
                      if (the_beacon->optimization_target->set_of_forbidden_edges->find (
                              as->as_number) !=
                              the_beacon->optimization_target->set_of_forbidden_edges->end () &&
                          the_beacon->optimization_target->set_of_forbidden_edges
                                  ->at (as->as_number)
                                  ->find (candidate_egress_if_no) !=
                              the_beacon->optimization_target->set_of_forbidden_edges
                                  ->at (as->as_number)
                                  ->end ())
                        {
                          continue;
                        }
                    }
                  if (selected_beacons.find (group) == selected_beacons.end ())
                    {
                      selected_beacons.insert (std::make_pair (
                          group, std::multimap<ld,
                                               std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *,
                                                          static_info_extension_t>,
                                               std::greater<ld>> ()));
                    }

                  static_info_extension_t propagation_static_info;
                  ExtendStaticInfoExtension (the_beacon,
                                             LOWER_16_BITS (the_beacon->the_path.back ()),
                                             candidate_egress_if_no, propagation_static_info);

                  ld dissemination_score =
                      CalculateScore (the_beacon->optimization_target, propagation_static_info);

                  auto [remote_ingress_if_no, remote_as] =
                      as->GetRemoteAsInfo (candidate_egress_if_no);

                  selected_beacons.at (group).insert (std::make_pair (
                      dissemination_score,
                      std::make_tuple (the_beacon, candidate_egress_if_no, remote_ingress_if_no,
                                       remote_as, propagation_static_info)));
                }
            }
        }
    }

  for (auto &subgroup_selected_beacons_per_subgroup_pair : selected_beacons)
    {
      auto &selected_beacons_per_subgroup = subgroup_selected_beacons_per_subgroup_pair.second;
      if (selected_beacons_per_subgroup.size () >
          optimization_target->no_beacons_per_optimization_target)
        {
          auto it = selected_beacons_per_subgroup.begin ();
          std::advance (it, optimization_target->no_beacons_per_optimization_target);
          selected_beacons_per_subgroup.erase (it, selected_beacons_per_subgroup.end ());
        }
      NS_ASSERT (selected_beacons_per_subgroup.size () ==
                 optimization_target->no_beacons_per_optimization_target);
    }
}

void
OnDemandOptimization::SendSelectedBeaconsPerTargetPerNbr (
    const std::unordered_map<
        uint16_t,
        std::multimap<ld,
                      std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, static_info_extension_t>,
                      std::greater<ld>>> &selected_beacons)
{
  for (auto const &group_selected_beacons_pair : selected_beacons)
    {
      for (auto const &score_selected_beacons_pair : group_selected_beacons_pair.second)
        {
          Beacon *the_beacon;
          uint16_t remote_ingress_if_no;
          uint16_t self_egress_if_no;
          ScionAs *remote_as;
          static_info_extension_t static_info_extension;

          std::tie (the_beacon, self_egress_if_no, remote_ingress_if_no, remote_as,
                    static_info_extension) = score_selected_beacons_pair.second;

          GenerateBeaconAndSend (the_beacon, self_egress_if_no, remote_ingress_if_no, remote_as,
                                 static_info_extension, the_beacon->optimization_target,
                                 the_beacon->beacon_direction);
        }
    }
}

std::tuple<bool, bool, bool, Beacon *, ld>
OnDemandOptimization::AlgSpecificImportPolicy (Beacon &the_beacon, uint16_t sender_as,
                                               uint16_t remote_egress_if_no,
                                               uint16_t self_ingress_if_no, uint16_t now)
{
  if (the_beacon.beacon_direction == BeaconDirectionT::PULL_BASED)
    {
      if (ORIGINATOR (the_beacon) == as->as_number)
        {
          if (now - the_beacon.next_initiation_time >=
              pull_based_dissemination_to_initiation_frequency *
                  beaconing_period.ToInteger (Time::MIN))
            {
              return std::tuple<bool, bool, bool, Beacon *, ld> (false, false, false, NULL, 0);
            }
          return std::tuple<bool, bool, bool, Beacon *, ld> (true, false, false, NULL, 0);
        }
      else
        {
          if (visited_pull_based_src_dst_pair.find (
                  std::make_pair (ORIGINATOR (the_beacon), DST_AS (the_beacon))) !=
              visited_pull_based_src_dst_pair.end ())
            {
              return std::tuple<bool, bool, bool, Beacon *, ld> (false, false, false, NULL, 0);
            }
        }
    }

  const auto &grouped_beacons =
      (the_beacon.beacon_direction == BeaconDirectionT::PUSH_BASED)
          ? push_based_beacons_grouped_by_optimization_targets_and_ingress_if_group
          : pull_based_beacons_grouped_by_optimization_targets_and_ingress_if_group.at (
                pull_based_write);

  uint16_t access_index = if_to_if_group.at (self_ingress_if_no);

  if (grouped_beacons.find (the_beacon.optimization_target) == grouped_beacons.end ())
    {
      return std::tuple<bool, bool, bool, Beacon *, ld> (true, false, false, NULL, 0);
    }

  if (grouped_beacons.at (the_beacon.optimization_target).find (access_index) ==
      grouped_beacons.at (the_beacon.optimization_target).end ())
    {
      return std::tuple<bool, bool, bool, Beacon *, ld> (true, false, false, NULL, 0);
    }

  if (grouped_beacons.at (the_beacon.optimization_target).at (access_index).size () <
      the_beacon.optimization_target->no_beacons_per_optimization_target)
    {
      return std::tuple<bool, bool, bool, Beacon *, ld> (true, false, false, NULL, 0);
    }

  std::multimap<ld, Beacon *>::const_reverse_iterator worst_beacon_score =
      grouped_beacons.at (the_beacon.optimization_target).at (access_index).rbegin ();
  ld incoming_beacon_score =
      CalculateScore (the_beacon.optimization_target, the_beacon.static_info_extension);
  ld lowest_previous_score = worst_beacon_score->first;
  if (lowest_previous_score < incoming_beacon_score)
    {
      Beacon *to_be_removed_beacon = worst_beacon_score->second;
      NS_ASSERT (to_be_removed_beacon->beacon_direction == the_beacon.beacon_direction);
      NS_ASSERT (DST_AS_PTR (to_be_removed_beacon) == DST_AS (the_beacon));
      return std::tuple<bool, bool, bool, Beacon *, ld> (true, false, false, to_be_removed_beacon,
                                                         lowest_previous_score);
    }

  NS_ASSERT (the_beacon.beacon_direction != BeaconDirectionT::PUSH_BASED ||
             next_round_valid_beacons_count_per_dst_as.find (DST_AS (the_beacon)) !=
                 next_round_valid_beacons_count_per_dst_as.end ());
  return std::tuple<bool, bool, bool, Beacon *, ld> (false, false, false, NULL, 0);
}

void
OnDemandOptimization::InsertToAlgorithmDataStructures (Beacon *the_beacon, uint16_t sender_as,
                                                       uint16_t remote_egress_if_no,
                                                       uint16_t self_ingress_if_no)
{
  if (the_beacon->beacon_direction == BeaconDirectionT::PULL_BASED)
    {
      if (ORIGINATOR_PTR (the_beacon) == as->as_number)
        {
          InsertToForbiddenEdges (the_beacon);
          return;
        }
      else
        {
          visited_pull_based_src_dst_pair.insert (
              std::make_pair (ORIGINATOR_PTR (the_beacon), DST_AS_PTR (the_beacon)));
        }
    }

  auto &grouped_beacons =
      (the_beacon->beacon_direction == BeaconDirectionT::PUSH_BASED)
          ? push_based_beacons_grouped_by_optimization_targets_and_ingress_if_group
          : pull_based_beacons_grouped_by_optimization_targets_and_ingress_if_group.at (
                pull_based_write);

  uint16_t access_index = if_to_if_group.at (self_ingress_if_no);

  if (grouped_beacons.find (the_beacon->optimization_target) == grouped_beacons.end ())
    {
      grouped_beacons.insert (
          std::make_pair (the_beacon->optimization_target, beacons_with_the_same_opt_target_t ()));
    }

  if (grouped_beacons.at (the_beacon->optimization_target).find (access_index) ==
      grouped_beacons.at (the_beacon->optimization_target).end ())
    {
      grouped_beacons.at (the_beacon->optimization_target)
          .insert (std::make_pair (access_index,
                                   beacons_with_the_same_opt_target_and_ingress_if_group_t ()));
    }

  ld incoming_beacon_score =
      CalculateScore (the_beacon->optimization_target, the_beacon->static_info_extension);

  grouped_beacons.at (the_beacon->optimization_target)
      .at (access_index)
      .insert (std::make_pair (incoming_beacon_score, the_beacon));

  if (the_beacon->beacon_direction == BeaconDirectionT::PUSH_BASED)
    {
      InsertToForbiddenEdges (the_beacon);
    }
}

void
OnDemandOptimization::DeleteFromAlgorithmDataStructures (Beacon *the_beacon, ld replacement_key)
{
  if (the_beacon->beacon_direction == BeaconDirectionT::PULL_BASED &&
      ORIGINATOR_PTR (the_beacon) == as->as_number)
    {
      DeleteFromForbiddenEdges (the_beacon);
      return;
    }

  uint16_t self_ingress_if = (the_beacon->beacon_direction == BeaconDirectionT::PUSH_BASED)
                                 ? LOWER_16_BITS (the_beacon->the_path.back ())
                                 : SECOND_UPPER_16_BITS (the_beacon->the_path.front ());

  auto &grouped_beacons =
      (the_beacon->beacon_direction == BeaconDirectionT::PUSH_BASED)
          ? push_based_beacons_grouped_by_optimization_targets_and_ingress_if_group
                .at (the_beacon->optimization_target)
                .at (if_to_if_group.at (self_ingress_if))
          : pull_based_beacons_grouped_by_optimization_targets_and_ingress_if_group
                .at (pull_based_write)
                .at (the_beacon->optimization_target)
                .at (if_to_if_group.at (self_ingress_if));

  for (auto it = grouped_beacons.lower_bound (replacement_key);
       it != grouped_beacons.upper_bound (replacement_key); ++it)
    {
      if (it->second == the_beacon)
        {
          grouped_beacons.erase (it--);
          break;
        }
    }

  if (the_beacon->beacon_direction == BeaconDirectionT::PUSH_BASED)
    {
      DeleteFromForbiddenEdges (the_beacon);
    }
}

ld
OnDemandOptimization::CalculateScore (const OptimizationTarget *optimization_target,
                                      const static_info_extension_t &static_info_extension)
{
  ld score = 0;
  ld weights_sum = 0;
  for (auto const &criteria : optimization_target->criteria)
    {
      weights_sum += criteria.second;
      if (criteria.first == StaticInfoType::LATENCY)
        { // in ms, assuming max latency is 1000 ms
          score +=
              (1 - static_info_extension.at (StaticInfoType::LATENCY) / 1000.0) * criteria.second;
        }
      else if (criteria.first == StaticInfoType::BW)
        { // in Gbps, assuming max BW is 400 Gbps
          score += static_info_extension.at (StaticInfoType::BW) / 400.0 * criteria.second;
        }
      else if (criteria.first == StaticInfoType::CO2)
        { // in g/Gbps, assuming max is 10 g/Gbps
          score += (1 - static_info_extension.at (StaticInfoType::CO2) / 10.0) * criteria.second;
        }
      else if (criteria.first == StaticInfoType::FORBIDDEN_EDGES)
        {
        }
    }

  if (weights_sum == 0)
    {
      return score;
    }

  return (score / weights_sum);
}

void
OnDemandOptimization::UpdateAlgorithmDataStructuresPeriodic (Beacon *the_beacon, bool invalidated)
{
  if (invalidated)
    {
      DeleteFromForbiddenEdges (the_beacon);
    }
}

void
OnDemandOptimization::UpdateStateBeforeBeaconing ()
{
  BeaconServer::UpdateStateBeforeBeaconing ();
  if (now == first_pull_based_interval)
    {
      if (file_to_read_beacons != "none")
        {
          NS_ASSERT (!beacon_store.empty ());
          for (auto const &dst_beacons : beacon_store)
            {
              for (auto const &len_beacons : dst_beacons.second)
                {
                  if (len_beacons.first == 1)
                    {
                      break;
                    }

                  for (auto const &the_beacon : len_beacons.second)
                    {
                      InsertToForbiddenEdges (the_beacon);
                    }
                }
            }
        }

      CheckMaxTolerableLinkFailures ();
    }

  NS_ASSERT (now == Simulator::Now ().ToInteger (Time::MIN));
  if (now >= first_pull_based_interval &&
      (now / beaconing_period.ToInteger (Time::MIN)) %
              pull_based_dissemination_to_initiation_frequency ==
          0)
    {
      if (!new_requested_pull_based_beacons.empty ())
        {
          for (auto const &the_beacon : new_requested_pull_based_beacons)
            {
              NS_ASSERT (now - the_beacon->next_initiation_time >=
                         pull_based_dissemination_to_initiation_frequency *
                             beaconing_period.ToInteger (Time::MIN));
              InsertToForbiddenEdges (the_beacon);
            }
          new_requested_pull_based_beacons.clear ();
        }
      visited_pull_based_src_dst_pair.clear ();
      if (now > first_pull_based_interval)
        {
          CheckMaxTolerableLinkFailures ();
        }
    }
}

void
OnDemandOptimization::DeleteFromForbiddenEdges (Beacon *the_beacon)
{
  if (the_beacon->beacon_direction == BeaconDirectionT::PULL_BASED &&
      ORIGINATOR_PTR (the_beacon) == as->as_number &&
      now - the_beacon->next_initiation_time <
          pull_based_dissemination_to_initiation_frequency * beaconing_period.ToInteger (Time::MIN))
    {
      new_requested_pull_based_beacons.erase (the_beacon);
      return;
    }

  uint16_t dst_as = DST_AS_PTR (the_beacon);

  NS_ASSERT (the_beacon->beacon_direction == BeaconDirectionT::PUSH_BASED ||
             ORIGINATOR_PTR (the_beacon) == as->as_number);
  NS_ASSERT (repetition_of_edges.find (dst_as) != repetition_of_edges.end ());

  std::vector<link_information>::reverse_iterator hop = the_beacon->the_path.rbegin ();
  for (; hop != the_beacon->the_path.rend (); ++hop)
    {
      uint32_t observed_edge;
      uint16_t observed_as;
      uint16_t observed_iface;

      if (the_beacon->beacon_direction == BeaconDirectionT::PULL_BASED)
        {
          observed_edge = UPPER_32_BITS (*hop);
          observed_as = UPPER_16_BITS (*hop);
          observed_iface = SECOND_UPPER_16_BITS (*hop);
        }
      else
        {
          observed_edge = LOWER_32_BITS (*hop);
          observed_as = SECOND_LOWER_16_BITS (*hop);
          observed_iface = LOWER_16_BITS (*hop);
        }
      NS_ASSERT (repetition_of_edges.at (dst_as)->find (observed_edge) !=
                 repetition_of_edges.at (dst_as)->end ());
      repetition_of_edges.at (dst_as)->at (observed_edge)--;
      edge_to_beacon.at (dst_as)->at (observed_edge).erase (the_beacon);

      if (repetition_of_edges.at (dst_as)->at (observed_edge) == 0)
        {
          repetition_of_edges.at (dst_as)->erase (observed_edge);
          edge_to_beacon.at (dst_as)->erase (observed_edge);
          NS_ASSERT (set_of_forbidden_edges_per_destination_as.at (dst_as)->find (observed_as) !=
                     set_of_forbidden_edges_per_destination_as.at (dst_as)->end ());
          NS_ASSERT (
              set_of_forbidden_edges_per_destination_as.at (dst_as)
                  ->at (observed_as)
                  ->find (observed_iface) !=
              set_of_forbidden_edges_per_destination_as.at (dst_as)->at (observed_as)->end ());
          set_of_forbidden_edges_per_destination_as.at (dst_as)
              ->at (observed_as)
              ->erase (observed_iface);
          // We do not remove the observed AS as it is a set object in the heap
        }
    }
}

void
OnDemandOptimization::InsertToForbiddenEdges (Beacon *the_beacon)
{
  if (the_beacon->beacon_direction == BeaconDirectionT::PULL_BASED &&
      ORIGINATOR_PTR (the_beacon) == as->as_number &&
      now - the_beacon->next_initiation_time <
          pull_based_dissemination_to_initiation_frequency * beaconing_period.ToInteger (Time::MIN))
    {
      new_requested_pull_based_beacons.insert (the_beacon);
      return;
    }

  uint16_t dst_as = DST_AS_PTR (the_beacon);
  NS_ASSERT (the_beacon->beacon_direction == BeaconDirectionT::PUSH_BASED ||
             ORIGINATOR_PTR (the_beacon) == as->as_number);

  if (repetition_of_edges.find (dst_as) == repetition_of_edges.end ())
    {
      repetition_of_edges.insert (
          std::make_pair (dst_as, new std::unordered_map<uint32_t, uint16_t> ()));
      edge_to_beacon.insert (std::make_pair (
          dst_as, new std::unordered_map<uint32_t, std::unordered_set<const Beacon *>> ()));
      NS_ASSERT (set_of_forbidden_edges_per_destination_as.find (dst_as) ==
                 set_of_forbidden_edges_per_destination_as.end ());
      set_of_forbidden_edges_per_destination_as.insert (std::make_pair (
          dst_as, new std::unordered_map<uint16_t, std::unordered_set<uint16_t> *> ()));
    }

  std::vector<link_information>::const_reverse_iterator hop = the_beacon->the_path.rbegin ();
  for (; hop != the_beacon->the_path.rend (); ++hop)
    {
      uint32_t observed_edge;
      uint16_t observed_as;
      uint16_t observed_iface;

      if (the_beacon->beacon_direction == BeaconDirectionT::PULL_BASED)
        {
          observed_edge = UPPER_32_BITS (*hop);
          observed_as = UPPER_16_BITS (*hop);
          observed_iface = SECOND_UPPER_16_BITS (*hop);
        }
      else
        {
          observed_edge = LOWER_32_BITS (*hop);
          observed_as = SECOND_LOWER_16_BITS (*hop);
          observed_iface = LOWER_16_BITS (*hop);
        }

      NS_ASSERT (repetition_of_edges.find (dst_as) != repetition_of_edges.end ());
      NS_ASSERT (repetition_of_edges.at (dst_as) != NULL);
      if (repetition_of_edges.at (dst_as)->find (observed_edge) ==
          repetition_of_edges.at (dst_as)->end ())
        {
          repetition_of_edges.at (dst_as)->insert (std::make_pair (observed_edge, 0));
          edge_to_beacon.at (dst_as)->insert (
              std::make_pair (observed_edge, std::unordered_set<const Beacon *> ()));
          NS_ASSERT (
              set_of_forbidden_edges_per_destination_as.at (dst_as)->find (observed_as) ==
                  set_of_forbidden_edges_per_destination_as.at (dst_as)->end () ||
              set_of_forbidden_edges_per_destination_as.at (dst_as)
                      ->at (observed_as)
                      ->find (observed_iface) ==
                  set_of_forbidden_edges_per_destination_as.at (dst_as)->at (observed_as)->end ());
        }

      edge_to_beacon.at (dst_as)->at (observed_edge).insert (the_beacon);
      repetition_of_edges.at (dst_as)->at (observed_edge)++;
      if (set_of_forbidden_edges_per_destination_as.at (dst_as)->find (observed_as) ==
          set_of_forbidden_edges_per_destination_as.at (dst_as)->end ())
        {
          set_of_forbidden_edges_per_destination_as.at (dst_as)->insert (
              std::make_pair (observed_as, new std::unordered_set<uint16_t> ()));
        }
      if (set_of_forbidden_edges_per_destination_as.at (dst_as)
              ->at (observed_as)
              ->find (observed_iface) ==
          set_of_forbidden_edges_per_destination_as.at (dst_as)->at (observed_as)->end ())
        {
          set_of_forbidden_edges_per_destination_as.at (dst_as)
              ->at (observed_as)
              ->insert (observed_iface);
        }
    }
}

void
OnDemandOptimization::CheckMaxTolerableLinkFailures ()
{
  for (auto const &[dst_as, per_dst_edge_to_beacon] : edge_to_beacon)
    {
      auto per_dst_edge_to_beacon_copy = *per_dst_edge_to_beacon;

      uint16_t max_tolerable_link_failure =
          CheckMaxTolerableLinkFailuresPerDst (per_dst_edge_to_beacon_copy, 0);

      if (max_tolerable_link_failure < desired_max_tolerable_link_failures)
        {
          if (now == first_pull_based_interval)
            {
              CreateOptimizationTargetsForForbiddenEdges (dst_as);
            }
        }
      else
        {
          RemoveOptimizationTargetsForForbiddenEdges (dst_as);
        }
    }
}

uint16_t
OnDemandOptimization::CheckMaxTolerableLinkFailuresPerDst (
    std::unordered_map<uint32_t, std::unordered_set<const Beacon *>> &per_dst_edge_to_beacon,
    uint16_t max_tolerable_link_failure)
{
  if (per_dst_edge_to_beacon.empty ())
    {
      return max_tolerable_link_failure;
    }

  uint32_t max_edge = 0;
  uint16_t max_repetition = 0;
  for (auto const &[edge, beacons] : per_dst_edge_to_beacon)
    {
      if (beacons.size () > max_repetition)
        {
          max_edge = edge;
          max_repetition = beacons.size ();
        }
    }

  for (auto const &the_beacon : per_dst_edge_to_beacon.at (max_edge))
    {
      std::vector<link_information>::const_reverse_iterator hop = the_beacon->the_path.rbegin ();
      for (; hop != the_beacon->the_path.rend (); ++hop)
        {
          uint32_t observed_edge;

          if (the_beacon->beacon_direction == BeaconDirectionT::PULL_BASED)
            {
              observed_edge = UPPER_32_BITS (*hop);
            }
          else
            {
              observed_edge = LOWER_32_BITS (*hop);
            }

          if (observed_edge == max_edge)
            {
              continue;
            }

          per_dst_edge_to_beacon.at (observed_edge).erase (the_beacon);
        }
    }

  per_dst_edge_to_beacon.erase (max_edge);

  for (auto it = per_dst_edge_to_beacon.begin (); it != per_dst_edge_to_beacon.end ();)
    {
      if (it->second.empty ())
        {
          it = per_dst_edge_to_beacon.erase (it);
        }
      else
        {
          ++it;
        }
    }

  return CheckMaxTolerableLinkFailuresPerDst (per_dst_edge_to_beacon,
                                              max_tolerable_link_failure + 1);
}

void
OnDemandOptimization::CreateOptimizationTargetsForForbiddenEdges (uint16_t dst_as)
{
  OptimizationTarget *optimization_target =
      new OptimizationTarget (0xFFFF - as->as_number, {{StaticInfoType::FORBIDDEN_EDGES, 1}},
                              OptimizationDirection::SYMMETRIC, dst_as, 0xFFFF, 1,
                              set_of_forbidden_edges_per_destination_as.at (dst_as));

  for (uint16_t iface = 0; iface < as->GetNDevices (); ++iface)
    {
      if_to_pull_based_optimization_targets_map.insert (
          std::make_pair (iface, optimization_target));
    }
}

void
OnDemandOptimization::RemoveOptimizationTargetsForForbiddenEdges (uint16_t dst_as)
{
  for (auto it = if_to_pull_based_optimization_targets_map.begin ();
       it != if_to_pull_based_optimization_targets_map.end ();)
    {
      if (it->second->target_as == dst_as)
        {
          it = if_to_pull_based_optimization_targets_map.erase (it);
        }
      else
        {
          ++it;
        }
    }
}

} // namespace ns3