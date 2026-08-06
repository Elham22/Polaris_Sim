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

#ifndef SCION_SIMULATOR_BEACON_SERVER_H
#define SCION_SIMULATOR_BEACON_SERVER_H

#include <map>
#include <unordered_map>
#include <unordered_set>

#include "ns3/nstime.h"
#include "ns3/rapidxml.hpp"

#include "beacon.h"
#include "src/SCION/model/externs.h"
#include "src/SCION/model/scion-as.h"

namespace ns3 {

#define MAX_BEACONS_TO_STORE 20
#define MAX_BEACONS_TO_SEND 5

class ScionAs;

typedef std::unordered_set<Beacon *> beacons_with_equal_length;
typedef std::map<uint16_t, beacons_with_equal_length> beacons_with_same_dst_as;
typedef std::pair<Time, uint16_t> beaconing_timing_params;

class BeaconServer
{
public:
  BeaconServer (ScionAs *as, bool parallel_scheduler, rapidxml::xml_node<> *xml_node,
                const YAML::Node &config)
      : as (as),
        parallel_scheduler (parallel_scheduler),
        beaconing_period (Time (config["beacon_service"]["period"].as<std::string> ())),
        expiration_period (Time (config["beacon_service"]["expiration_period"].as<std::string> ())
                               .ToInteger (Time::MIN)),
        last_beaconing_event_time (
            Time (config["beacon_service"]["last_beaconing"].as<std::string> ()))
  {
    non_requested_pull_based_beacon_container.resize (2);
    PropertyContainer p = ParseProperties (xml_node);
    if (p.HasProperty ("dirty_energy_ratio"))
      {
        dirty_energy_ratio = std::stod (p.GetProperty ("dirty_energy_ratio"));
      }

    if (p.HasProperty ("sun_energy_ratio"))
      {
        sun_energy_ratio = std::stod (p.GetProperty ("sun_energy_ratio"));
      }

    if (config["beacon_service"]["read_beacons_directory"])
      {
        file_to_read_beacons =
            config["beacon_service"]["read_beacons_directory"].as<std::string> () + "beacons_" +
            std::to_string (as->as_number) + ".json";
      }
    else
      {
        file_to_read_beacons = "none";
      }

    if (config["beacon_service"]["write_beacons_directory"])
      {
        file_to_write_beacons =
            config["beacon_service"]["write_beacons_directory"].as<std::string> () + "beacons_" +
            std::to_string (as->as_number) + ".json";
      }
    else
      {
        file_to_write_beacons = "none";
      }
  }

  virtual void DoInitializations (uint32_t num_ases, rapidxml::xml_node<> *xml_node,
                                  const YAML::Node &config);

  virtual void PerLinkInitializations (rapidxml::xml_node<> *xml_node, const YAML::Node &config);

  void SetAs (ScionAs *as);

  void ReceiveBeacon (Beacon &received_beacon, uint16_t sender_as, uint16_t remote_if,
                      uint16_t local_if);

  void ScheduleBeaconing (Time last_beaconing_event_time);

  void InsertPulledBeaconsToBeaconStore ();

  const uint16_t GetCurrentTime () const;

  const std::vector<std::vector<ld>> &GetIntraAsEnergies () const;

  float GetDirtyEnergyRatio () const;

  float GetSunEnergyRatio () const;

  const std::unordered_map<uint16_t, beacons_with_same_dst_as> &GetBeaconStore () const;

  const std::unordered_map<std::string, Beacon> &GetPathMapToBeacon () const;

  const std::unordered_map<uint16_t, uint32_t> &GetValidBeaconsCountPerDstAs () const;

  const std::unordered_map<uint16_t, uint32_t> &GetNextRoundValidBeaconsCountPerDstAs () const;

  const std::unordered_map<uint16_t, std::vector<uint32_t>> &
  GetBytesSentPerInterfacePerPeriod () const;

  const std::unordered_map<uint16_t, std::vector<uint32_t>> &
  GetBeaconsSentPerInterfacePerPeriod () const;

  const std::vector<std::unordered_map<uint16_t, uint32_t>> &
  GetBeaconsSentPerDstPerInterfacePerPeriod () const;
  const std::vector<std::unordered_map<const OptimizationTarget *, uint32_t>> &
  GetPushBasedBeaconsSentPerOptPerInterfacePerPeriod () const;
  const std::vector<std::unordered_map<const OptimizationTarget *, uint32_t>> &
  GetPullBasedBeaconsSentPerOptPerInterfacePerPeriod () const;

  const std::vector<uint64_t> &GetBeaconsSentPerInterface () const;

protected:
  ScionAs *as;

  const bool parallel_scheduler;

  const Time beaconing_period;
  const uint16_t expiration_period;
  const Time last_beaconing_event_time;

  float dirty_energy_ratio;
  float sun_energy_ratio;

  std::string file_to_write_beacons;
  std::string file_to_read_beacons;

  uint16_t now;
  uint16_t next_period;

  std::vector<std::vector<ld>> intra_as_energies;

  std::unordered_map<uint16_t, beacons_with_same_dst_as> beacon_store;

  // All beacon instances are stored in either of the containers, with no overlap
  uint16_t pull_based_write = 0;
  uint16_t pull_based_read = 1;
  std::unordered_map<std::string, Beacon> push_based_beacon_container;
  std::vector<std::unordered_map<std::string, Beacon>> non_requested_pull_based_beacon_container;
  std::unordered_map<std::string, Beacon>
      requested_pull_based_beacon_container; // the result of pull based beaconing returned to the source AS

  std::unordered_map<uint16_t, uint32_t> valid_beacons_count_per_dst_as;
  std::unordered_map<uint16_t, uint32_t> next_round_valid_beacons_count_per_dst_as;

  std::unordered_map<uint16_t, std::vector<uint32_t>> bytes_sent_per_interface_per_period;
  std::unordered_map<uint16_t, std::vector<uint32_t>> beacons_sent_per_interface_per_period;

  std::vector<std::unordered_map<uint16_t, uint32_t>> beacons_sent_per_dst_per_interface;
  std::vector<std::unordered_map<const OptimizationTarget *, uint32_t>>
      push_based_beacons_sent_per_opt_per_interface;
  std::vector<std::unordered_map<const OptimizationTarget *, uint32_t>>
      pull_based_beacons_sent_per_opt_per_interface;

  std::vector<uint64_t> beacons_sent_per_interface;

  void InitiateBeacons (NeighbourRelation relation);

  virtual void InitiateBeaconsPerInterface (uint16_t self_egress_if_no, ScionAs *remote_as,
                                               uint16_t remote_ingress_if_no);

  virtual void CreateInitialStaticInfoExtension (static_info_extension_t &static_info_extension,
                                        uint16_t self_egress_if_no,
                                        const OptimizationTarget *optimization_target);

  virtual void DisseminateBeacons (NeighbourRelation relation) = 0;

  void GenerateBeaconAndSend (Beacon *selected_beacon, uint16_t self_egress_if_no,
                            uint16_t remote_ingress_if_no, ScionAs *remote_as,
                            static_info_extension_t &static_info_extension,
                            const OptimizationTarget *optimization_target = NULL,
                              BeaconDirectionT beacon_direction = BeaconDirectionT::PUSH_BASED);

  std::tuple<bool, bool, bool, Beacon *, ld> ImportPolicy (Beacon &the_beacon, uint16_t sender_as,
                                                            uint16_t remote_egress_if_no,
                                                            uint16_t self_ingress_if_no,
                                                            uint16_t now);

  void InsertBeacon (Beacon &the_beacon, uint16_t dst_as, uint16_t sender_as,
                      uint16_t remote_egress_if, uint16_t local_ingress_if, bool path_exists,
                      bool existing_path_valid, Beacon *beacon_to_replace);

  void IncrementValidBeaconsCount (uint16_t dst_as);

  void IncrementNextRoundValidBeaconsCount (uint16_t dst_as);

  void DeleteBeacon (Beacon *to_be_removed_beacon, ld replacement_key, uint16_t dst_as);

  void DecrementValidBeaconsCount (uint16_t dst_as);

  void DecrementNextRoundValidBeaconsCount (uint16_t dst_as);

  virtual std::tuple<bool, bool, bool, Beacon *, ld>
  AlgSpecificImportPolicy (Beacon &the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no,
                              uint16_t self_ingress_if_no, uint16_t now) = 0;

  virtual void InsertToAlgorithmDataStructures (Beacon *the_beacon, uint16_t sender_as,
                                                    uint16_t remote_egress_if_no,
                                                    uint16_t self_ingress_if_no) = 0;

  virtual void DeleteFromAlgorithmDataStructures (Beacon *the_beacon, ld replacement_key) = 0;

  void UpdateStatePeriodic ();

  virtual void UpdateStateBeforeBeaconing ();

  void UpdateBeaconState (Beacon *the_beacon);

  virtual void UpdateAlgorithmDataStructuresPeriodic (Beacon *the_beacon, bool invalidated) = 0;

  void RegisterToLocalPathServer ();

  void IncrementControlPlaneBytesSent (Beacon &the_beacon, uint16_t interface);

  std::pair<ld, ld> CalculateFinalDiversityScores (Beacon *the_beacon);

  friend void ReadBr2BrEnergy (ns3::NodeContainer as_nodes,
                               std::map<int32_t, uint16_t> real_to_alias_as_no,
                               const YAML::Node &config);

  void ReadBeacons ();

  void WriteBeacons ();
};

void ReadBr2BrEnergy (NodeContainer as_nodes, std::map<int32_t, uint16_t> real_to_alias_as_no,
                      const YAML::Node &config);

} // namespace ns3
#endif //SCION_SIMULATOR_BEACON_SERVER_H
