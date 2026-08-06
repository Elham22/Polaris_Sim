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

#ifndef SCION_SIMULATOR_TIME_SERVER_H
#define SCION_SIMULATOR_TIME_SERVER_H

#include <algorithm>
#include <random>
#include <set>

#include "ns3/nstime.h"

#include "scion-host.h"
#include "user-defined-events.h"

namespace ns3 {

#define PATH_RQ_TIME_SYNC_DIFF "1s"
#define NTP_REQ_GLOBAL_SYNC_DIFF "2s"

enum ReadOrWriteDisjointPaths { R = 0, W = 2, NO_R_NO_W = 3 };
enum TimeServerType { NORMAL = 0, MALICIOUS_SERVER = 1 };
enum ReferenceTimeType { OFF = 0, ON = 1, MALICIOUS_REF = 2 };

enum SnapshotType {
  LOCAL_SNAPSHOT = 0,
  ASSERT_OFFSET_DIFF = 1,
  PRINT_OFFSET_DIFF = 2,
  SNAPSHOT_OFF = 3
};
enum AlgV { V1 = 0, V2 = 1, V3 = 2, V4 = 3, LOCAL_SYNC = 4 };

class TimeServer : public ScionHost
{
public:
  TimeServer (uint32_t system_id, uint16_t isd_number, uint16_t as_number,
              host_addr_t local_address, double latitude, double longitude, ScionAs *as,
              bool parallel_scheduler, Time max_initial_drift, Time max_drift_per_day,
              bool jitter_in_drift, uint32_t max_drift_coefficient, Time global_cut_off,
              Time first_event, Time last_event, Time snapshot_period, Time list_of_ases_req_period,
              Time time_sync_period, uint32_t g, uint32_t number_of_paths_to_use_for_global_sync,
              std::string read_disjoint_paths, std::string time_service_output_path,
              std::string reference_time_type, std::string server_type, std::string snapshot_type,
              std::string alg_v, Time minimum_malicious_offset, std::string path_selection)
      : ScionHost (system_id, isd_number, as_number, local_address, latitude, longitude, as),
        parallel_scheduler (parallel_scheduler),
        max_initial_drift (max_initial_drift),
        max_drift_per_day (max_drift_per_day),
        jitter_in_drift (jitter_in_drift),
        max_drift_coefficient (max_drift_coefficient),
        global_cut_off (global_cut_off),
        first_event (first_event),
        last_event (last_event),
        snapshot_period (snapshot_period),
        list_of_ases_req_period (list_of_ases_req_period),
        time_sync_period (time_sync_period),
        g (g),
        number_of_paths_to_use_for_global_sync (number_of_paths_to_use_for_global_sync),
        read_disjoint_paths (read_or_write_disjoint_paths_map[read_disjoint_paths]),
        reference_time_type (reference_time_type_map[reference_time_type]),
        server_type (time_server_type_map[server_type]),
        snapshot_type (snapshot_type_map[snapshot_type]),
        alg_v (alg_v_map[alg_v]),
        minimum_malicious_offset (minimum_malicious_offset),
        path_selection (path_selection)
  {
    synchronization_round = 0;

    local_time = TimeStep (0);
    real_time_of_last_time_advance = TimeStep (0);
    set_of_all_core_ases.insert (ia_addr);

    set_of_disjoint_paths_file =
        time_service_output_path + "set_of_disjoint_path_TS_" + std::to_string (ia_addr) + ".json";

    std::random_device rd;
    std::uniform_int_distribution<int64_t> dist (-std::abs (max_drift_per_day.GetTimeStep ()),
                                                 std::abs (max_drift_per_day.GetTimeStep ()));
    constant_drift_per_day = dist (rd);

    if (reference_time_type_map[reference_time_type] == ReferenceTimeType::MALICIOUS_REF ||
        time_server_type_map[server_type] == TimeServerType::MALICIOUS_SERVER)
      {
        std::uniform_int_distribution<uint32_t> neg_or_pos_dist (0, 1);
        if (neg_or_pos_dist (rd) == 0)
          {
            std::uniform_int_distribution<int64_t> neg_dist (
                -10 * std::abs (minimum_malicious_offset.GetTimeStep ()),
                -std::abs (minimum_malicious_offset.GetTimeStep ()));
            malicious_offset_in_ps = neg_dist (rd);
          }
        else
          {
            std::uniform_int_distribution<int64_t> pos_dist (
                std::abs (minimum_malicious_offset.GetTimeStep ()),
                10 * std::abs (minimum_malicious_offset.GetTimeStep ()));
            malicious_offset_in_ps = pos_dist (rd);
          }
      }
  }

  void ScheduleListOfAllASesRequest ();
  void ScheduleTimeSync ();
  void ScheduleSnapShots ();
  void AdvanceLocalTime () override;
  int64_t GetConstantDriftPerDay ();
  int64_t GetDrift (Time duration);

  friend class PostSimulationEvaluations;
  friend class UserDefinedEvents;

private:
  bool affected_by_malicious_ases = false;

  std::map<std::string, ReadOrWriteDisjointPaths> read_or_write_disjoint_paths_map = {
      {"R", ReadOrWriteDisjointPaths::R},
      {"W", ReadOrWriteDisjointPaths::W},
      {"NO_R_NO_W", ReadOrWriteDisjointPaths::NO_R_NO_W}};

  std::map<std::string, ReferenceTimeType> reference_time_type_map = {
      {"OFF", ReferenceTimeType::OFF},
      {"ON", ReferenceTimeType::ON},
      {"MALICIOUS", ReferenceTimeType::MALICIOUS_REF}};

  std::map<std::string, TimeServerType> time_server_type_map = {
      {"NORMAL", TimeServerType::NORMAL}, {"MALICIOUS", TimeServerType::MALICIOUS_SERVER}};

  std::map<std::string, SnapshotType> snapshot_type_map = {
      {"LOCAL_SNAPSHOT", SnapshotType::LOCAL_SNAPSHOT},
      {"ASSERT_OFFSET_DIFF", SnapshotType::ASSERT_OFFSET_DIFF},
      {"PRINT_OFFSET_DIFF", SnapshotType::PRINT_OFFSET_DIFF},
      {"OFF", SnapshotType::SNAPSHOT_OFF}};

  std::map<std::string, AlgV> alg_v_map = {{"V1", AlgV::V1},
                                            {"V2", AlgV::V2},
                                            {"V3", AlgV::V3},
                                            {"V4", AlgV::V4},
                                            {"LOCAL_SYNC", AlgV::LOCAL_SYNC}};

  bool parallel_scheduler;
  Time max_initial_drift;
  Time max_drift_per_day;
  bool jitter_in_drift;
  uint32_t max_drift_coefficient;
  Time global_cut_off;
  Time first_event, last_event, snapshot_period;
  Time list_of_ases_req_period;
  Time time_sync_period;

  uint32_t g;
  uint32_t number_of_paths_to_use_for_global_sync;
  ReadOrWriteDisjointPaths read_disjoint_paths;
  uint32_t synchronization_round; // i in the Listing 2

  Time real_time_of_last_time_advance;

  std::set<ia_t> set_of_all_core_ases;

  std::string set_of_disjoint_paths_file;

  ReferenceTimeType reference_time_type;
  TimeServerType server_type;
  SnapshotType snapshot_type;
  AlgV alg_v;

  Time minimum_malicious_offset;
  std::string path_selection;
  int64_t constant_drift_per_day;
  int64_t malicious_offset_in_ps;

  std::unordered_map<ia_t, std::multiset<int64_t>> poff;

  std::unordered_map<ia_t, std::unordered_set<const PathSegment *>> set_of_selected_paths;

  Time GetReferenceTime ();

  Time GetMaxDrift (Time duration);

  void RequestSetOfAllCoreAsesFromPathServer ();

  void RequestForPathsToAllCoreAses ();

  void SendSetOfAllCoreAsesToNeighbors ();

  void SendNtpReqToPeers ();

  void ModifyPktUponSend (ScionPacket *packet) override;

  void ReceiveSetOfAllCoreAsesFromPathServer (ScionPacket *packet);

  void ConstructSetOfSelectedPaths ();

  void ConstructSetOfShortestPaths ();

  void ConstructSetOfRandomPaths ();

  void ConstructSetOfMostDisjointPaths ();

  void ReadSetOfDisjointPaths ();

  void WriteSetOfDisjointPaths ();

  void ReceiveSetOfAllCoreAsesFromOtherTimeServer (ScionPacket *packet);

  void ReceiveNtpReqFromPeer (ScionPacket *packet, Time receive_time);

  void ReceiveNtpResFromPeer (ScionPacket *packet, Time receive_time);

  void TriggerCoreTimeSyncAlgo ();

  void ContinueGlobalTimeSync ();

  void CorrectLocalTime (int64_t corr, Time duration, double coefficient);

  void ProcessReceivedPacket (uint16_t local_if, ScionPacket *packet, Time receive_time) override;

  void CaptureLocalSnapshot ();

  void CaptureOffsetDiff ();

  void CompareOffsWithRealOffs ();

  void ResetTime ();
};
} // namespace ns3

#endif //SCION_SIMULATOR_TIME_SERVER_H
