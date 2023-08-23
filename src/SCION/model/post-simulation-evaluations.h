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

#ifndef SCION_SIMULATOR_POST_SIMULATION_EVALUATIONS_H
#define SCION_SIMULATOR_POST_SIMULATION_EVALUATIONS_H

#include <yaml-cpp/yaml.h>

#include "src/SCION/model/beaconing/beacon.h"
#include "scion-as.h"

namespace ns3 {
#define REGISTER_FUN(FUNC_NAME)      \
  function_name_to_function.insert ( \
      std::make_pair (#FUNC_NAME, &PostSimulationEvaluations::FUNC_NAME));

class PostSimulationEvaluations
{
public:
  PostSimulationEvaluations (YAML::Node &config, NodeContainer &as_nodes,
                             std::map<int32_t, uint16_t> &real_to_alias_as_no,
                             std::map<uint16_t, int32_t> &alias_to_real_as_no)
      : config (config),
        as_nodes (as_nodes),
        real_to_alias_as_no (real_to_alias_as_no),
        alias_to_real_as_no (alias_to_real_as_no)
  {
    beaconing_period = Time (config["beacon_service"]["period"].as<std::string> ());
    last_beaconing_event_time =
        Time (config["beacon_service"]["last_beaconing"].as<std::string> ());
    first_beaconing = Time (config["beacon_service"]["first_beaconing"].as<std::string> ());
    expiration_period = Time (config["beacon_service"]["expiration_period"].as<std::string> ())
                            .ToInteger (Time::MIN);
    beaconing_policy_str = config["beacon_service"]["policy"].as<std::string> ();

    REGISTER_FUN (PrintTrafficSentFromCollectorsPerDstPerPeriod)
    REGISTER_FUN (PrintAllDiscoveredPaths)
    REGISTER_FUN (PrintAllPathsAttributes)
    REGISTER_FUN (PrintDistributionOfPathsWithSpecificHopCount)
    REGISTER_FUN (PrintNoBeaconsPerInterface)
    REGISTER_FUN (PrintNoBeaconsPerInterfacePerDstOrOpt)
    REGISTER_FUN (PrintConsumedBwAtEachPeriod)
    //             REGISTER_FUN(PrintPathQualities)
    //             REGISTER_FUN(EvaluateSTConnectivity)
    REGISTER_FUN (PrintMinimumLatencyDist)
    REGISTER_FUN (PrintPathNoDistribution)
    REGISTER_FUN (FindMinLatencyToDnsRootServers)
    REGISTER_FUN (PrintPathPollutionIndex)
    REGISTER_FUN (PrintLeastPollutingPaths)
    REGISTER_FUN (PrintBestPerHopPollutionIndexes)
    //             REGISTER_FUN(PrintTransitTrafficBaseline)
    REGISTER_FUN (InvestigateAffectedTimeServers)
    REGISTER_FUN (PrintBeaconStores)
    REGISTER_FUN (PrintConsumedBwForBeaconing)

    REGISTER_FUN (PrintNumberOfValidBeaconEntriesInBeaconStore)

    REGISTER_FUN (PrintHostApps)
    REGISTER_FUN (PrintLinkStatistics)
    REGISTER_FUN (PrintAppPathInfo)
  }

  void DoFinalEvaluations ();

  void PrintTrafficSentFromCollectorsPerDstPerPeriod ();

  void PrintAllDiscoveredPaths ();

  void PrintAllPathsAttributes ();

  void PrintDistributionOfPathsWithSpecificHopCount ();

  void PrintNoBeaconsPerInterface ();

  void PrintNoBeaconsPerInterfacePerDstOrOpt ();

  void PrintConsumedBwAtEachPeriod ();

  void PrintPathQualities ();

  void EvaluateSTConnectivity ();

  void PrintMinimumLatencyDist ();

  void PrintPathNoDistribution ();

  void FindMinLatencyToDnsRootServers ();

  void PrintPathPollutionIndex ();

  void PrintLeastPollutingPaths ();

  void PrintBestPerHopPollutionIndexes ();

  void PrintTransitTrafficBaseline ();

  void InvestigateAffectedTimeServers ();

  void PrintConsumedBwForBeaconing ();

  void PrintBeaconStores ();

  void PrintNumberOfValidBeaconEntriesInBeaconStore ();

  void PrintHostApps ();

  void PrintLinkStatistics ();

  void PrintAppPathInfo ();

  friend void
  SortBeaconsByPollutionByLatency (NodeContainer &as_nodes, ScionAs *as1, ScionAs *as2,
                                        std::string beaconing_policy_str,
                                        std::map<double, std::map<double, std::set<Beacon *>>>
                                            &sorted_beacons_by_pollution_by_latency);

private:
  YAML::Node &config;
  NodeContainer &as_nodes;
  std::map<int32_t, uint16_t> &real_to_alias_as_no;
  std::map<uint16_t, int32_t> &alias_to_real_as_no;

  Time beaconing_period;
  Time last_beaconing_event_time;
  Time first_beaconing;

  uint16_t expiration_period;
  std::string beaconing_policy_str;

  std::unordered_map<std::string, void (PostSimulationEvaluations::*) ()> function_name_to_function;
};

void SortBeaconsByPollutionByLatency (
    NodeContainer &as_nodes, ScionAs *as1, ScionAs *as2, std::string beaconing_policy_str,
    std::map<double, std::map<double, std::set<Beacon *>>> &sorted_beacons_by_pollution_by_latency);

} // namespace ns3
#endif //SCION_SIMULATOR_POST_SIMULATION_EVALUATIONS_H
