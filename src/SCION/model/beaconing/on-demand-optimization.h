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

#ifndef SCION_SIMULATOR_ON_DEMAND_OPTIMIZATION_H
#define SCION_SIMULATOR_ON_DEMAND_OPTIMIZATION_H

#include "beacon-server.h"

namespace ns3 {
typedef std::multimap<ld, Beacon *, std::greater<ld>>
    beacons_with_the_same_opt_target_and_ingress_if_group_t;
typedef std::map<uint16_t, beacons_with_the_same_opt_target_and_ingress_if_group_t>
    beacons_with_the_same_opt_target_t;
typedef std::unordered_map<const OptimizationTarget *, beacons_with_the_same_opt_target_t>
    beacons_grouped_by_optimization_targets_and_ingress_if_group_t;

class OnDemandOptimization : public BeaconServer
{
public:
  OnDemandOptimization (ScionAs *as, bool parallel_scheduler, rapidxml::xml_node<> *xml_node,
                        const YAML::Node &config)
      : BeaconServer (as, parallel_scheduler, xml_node, config)
  {
    pull_based_beacons_grouped_by_optimization_targets_and_ingress_if_group.resize (2);
    first_pull_based_interval =
        Time (config["beacon_service"]["first_pull_based_interval"].as<std::string> ())
            .ToInteger (Time::MIN);
    pull_based_dissemination_to_initiation_frequency =
        stoi (config["beacon_service"]["pull_based_dissemination_to_initiation_frequency"]
                  .as<std::string> ());
    desired_max_tolerable_link_failures =
        stoi (config["beacon_service"]["desired_max_tolerable_link_failures"].as<std::string> ());
    rapidxml::xml_node<> *cur_target = xml_node->first_node ("target");
    while (cur_target)
      {
        uint16_t target_id = std::stoi (cur_target->first_node ("target_id")->value ());

        optimization_criteria_t optimization_criteria;
        PropertyContainer p = ParseProperties (cur_target);
        if (p.HasProperty ("bw") && std::stof (p.GetProperty ("bw")) > 0.001)
          {
            optimization_criteria.insert (
                std::make_pair (StaticInfoType::BW, std::stof (p.GetProperty ("bw"))));
          }

        if (p.HasProperty ("latency") && std::stof (p.GetProperty ("latency")) > 0.001)
          {
            optimization_criteria.insert (std::make_pair (StaticInfoType::LATENCY,
                                                          std::stof (p.GetProperty ("latency"))));
          }

        std::string direction = cur_target->first_node ("direction")->value ();
        OptimizationDirection optimization_direction = OptimizationDirection::SYMMETRIC;
        if (direction == "forward")
          {
            optimization_direction = OptimizationDirection::FORWARD;
          }
        else if (direction == "backward")
          {
            optimization_direction = OptimizationDirection::BACKWARD;
          }

        uint16_t group_id = std::stoi (cur_target->first_node ("group_id")->value ());
        uint16_t no_beacons = std::stoi (cur_target->first_node ("no_beacons")->value ());

        set_of_optimization_targets_originated_from_this_as.insert (
            std::make_pair (target_id, OptimizationTarget (target_id, optimization_criteria,
                                                              optimization_direction,
                                           as->as_number,
                                                              group_id, no_beacons, NULL)));
        cur_target = cur_target->next_sibling ("target");
      }
  }

  void DoInitializations (uint32_t num_ases, rapidxml::xml_node<> *xml_node,
                          const YAML::Node &config) override;

  void PerLinkInitializations (rapidxml::xml_node<> *xml_node, const YAML::Node &config) override;

private:
  beacons_grouped_by_optimization_targets_and_ingress_if_group_t
      push_based_beacons_grouped_by_optimization_targets_and_ingress_if_group; // permanent until beacons expiration

  std::vector<beacons_grouped_by_optimization_targets_and_ingress_if_group_t>
      pull_based_beacons_grouped_by_optimization_targets_and_ingress_if_group;

  std::unordered_map<uint16_t, const OptimizationTarget>
      set_of_optimization_targets_originated_from_this_as;
  std::multimap<uint16_t, const OptimizationTarget *> if_to_push_based_optimization_targets_map;

  std::unordered_map<uint16_t, uint16_t> if_to_if_group;

  std::unordered_map<uint16_t, std::unordered_map<uint16_t, std::vector<uint16_t>>>
      interface_groups_connected_per_neighbor; // key1: neighbor AS, key2: interface_group, values in the vector: interface ids

  uint16_t first_pull_based_interval;
  uint16_t pull_based_dissemination_to_initiation_frequency;
  uint16_t desired_max_tolerable_link_failures;
  std::set<std::pair<uint16_t, uint16_t>> visited_pull_based_src_dst_pair;

  std::multimap<uint16_t, const OptimizationTarget *> if_to_pull_based_optimization_targets_map;
  std::unordered_map<uint16_t, std::unordered_map<uint32_t, uint16_t> *> repetition_of_edges;
  std::unordered_map<uint16_t, std::unordered_map<uint32_t, std::unordered_set<const Beacon *>> *>
      edge_to_beacon;
  std::unordered_map<uint16_t, std::unordered_map<uint16_t, std::unordered_set<uint16_t> *> *>
      set_of_forbidden_edges_per_destination_as;
  std::unordered_set<Beacon *> new_requested_pull_based_beacons;

  void InitiateBeaconsPerInterface (uint16_t self_egress_if_no, ScionAs *remote_as,
                                       uint16_t remote_ingress_if_no) override;

  void CreateInitialStaticInfoExtension (static_info_extension_t &static_info_extension,
                                        uint16_t self_egress_if_no,
                                        const OptimizationTarget *optimization_target) override;

  void DisseminateBeacons (NeighbourRelation relation) override;

  std::tuple<bool, bool, bool, Beacon *, ld>
  AlgSpecificImportPolicy (Beacon &the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no,
                              uint16_t self_ingress_if_no, uint16_t now) override;

  void InsertToAlgorithmDataStructures (Beacon *the_beacon, uint16_t sender_as,
                                            uint16_t remote_egress_if_no,
                                            uint16_t self_ingress_if_no) override;

  void DeleteFromAlgorithmDataStructures (Beacon *the_beacon, ld replacement_key) override;

  static ld CalculateScore (const OptimizationTarget *optimization_target,
                             const static_info_extension_t &static_info_extension);

  void SelectBeaconsToDisseminatePerTargetPerNbr (
      uint16_t remote_as_no,
      const beacons_with_the_same_opt_target_t &beacons_with_the_same_opt_target,
      const OptimizationTarget *optimization_target,
      std::unordered_map<uint16_t, std::multimap<ld,
                                                 std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, static_info_extension_t>,
                                                 std::greater<ld>>> &selected_beacons);

  void SendSelectedBeaconsPerTargetPerNbr (
      const std::unordered_map<
          uint16_t,
          std::multimap<
              ld, std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, static_info_extension_t>,
              std::greater<ld>>> &selected_beacons);

  void ExtendStaticInfoExtension (const Beacon *the_beacon, uint16_t beacon_ingress_if_no,
                                     uint16_t candidate_egress_if_no,
                                     static_info_extension_t &propagation_static_info);

  void UpdateStateBeforeBeaconing () override;

  void UpdateAlgorithmDataStructuresPeriodic (Beacon *the_beacon, bool invalidated) override;

  void DeleteFromForbiddenEdges (Beacon *the_beacon);

  void InsertToForbiddenEdges (Beacon *the_beacon);

  void CheckMaxTolerableLinkFailures ();

  uint16_t CheckMaxTolerableLinkFailuresPerDst (
      std::unordered_map<uint32_t, std::unordered_set<const Beacon *>> &per_dst_edge_to_beacon,
      uint16_t max_tolerable_link_failure);

  void CreateOptimizationTargetsForForbiddenEdges (uint16_t dst_as);

  void RemoveOptimizationTargetsForForbiddenEdges (uint16_t dst_as);
};
} // namespace ns3
#endif //SCION_SIMULATOR_ON_DEMAND_OPTIMIZATION_H
