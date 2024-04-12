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

#ifndef SCION_SIMULATOR_BEACON_H
#define SCION_SIMULATOR_BEACON_H

#include <set>
#include <string>
#include <utility>
#include <vector>
#include <unordered_set>

#include "src/SCION/model/path-segment.h"

namespace ns3 {

#define BEACON_HEADER_SIZE 86
#define BEACON_HOP_SIZE 132
#define ORIGINATOR(beacon) (UPPER_16_BITS (beacon.the_path.front ()))
#define ORIGINATOR_PTR(beacon) (UPPER_16_BITS (beacon->the_path.front ()))
#define DST_AS(beacon)                                     \
  (beacon.beacon_direction == BeaconDirectionT::PULL_BASED \
       ? beacon.optimization_target->target_as             \
       : UPPER_16_BITS (beacon.the_path.front ()))

#define DST_AS_PTR(beacon)                                  \
  (beacon->beacon_direction == BeaconDirectionT::PULL_BASED \
       ? beacon->optimization_target->target_as             \
       : UPPER_16_BITS (beacon->the_path.front ()))

typedef long double ld;

typedef uint64_t link_information; // sender_as   eg_if   receiver_as   ing_if
    // <-16bit-> <-16bit-> <--16bit-->  <-16bit->

typedef std::vector<link_information> path;
typedef std::vector<uint16_t> isd_path;

enum StaticInfoType { LATENCY = 0, BW = 1, CO2 = 2, FORBIDDEN_EDGES = 3 };
typedef std::map<StaticInfoType, float> static_info_extension_t;

typedef std::map<StaticInfoType, float> optimization_criteria_t;
enum OptimizationDirection { FORWARD = 0, BACKWARD = 1, SYMMETRIC = 2 };
typedef uint16_t target_as_t;
typedef uint16_t target_id_t;
typedef uint16_t target_if_group_t;

struct OptimizationTarget
{
  const target_id_t target_id;
  const optimization_criteria_t criteria;
  const OptimizationDirection direction;
  const target_as_t target_as;
  const target_if_group_t target_if_group;
  const uint16_t no_beacons_per_optimization_target;
  const std::unordered_map<uint16_t, std::unordered_set<uint16_t> *> *const set_of_forbidden_edges;

  OptimizationTarget (
      target_id_t target_id, optimization_criteria_t criteria, OptimizationDirection direction,
      target_as_t target_as, target_if_group_t target_if_group,
      uint16_t no_beacons_per_optimization_target,
      const std::unordered_map<uint16_t, std::unordered_set<uint16_t> *> *set_of_forbidden_edges)
      : target_id (target_id),
        criteria (std::move (criteria)),
        direction (direction),
        target_as (target_as),
        target_if_group (target_if_group),
        no_beacons_per_optimization_target (no_beacons_per_optimization_target),
        set_of_forbidden_edges (set_of_forbidden_edges)
  {
  }
};

enum BeaconDirectionT { PUSH_BASED = 0, PULL_BASED = 1 };

struct Beacon
{
  static_info_extension_t static_info_extension;

  const OptimizationTarget *optimization_target;

  BeaconDirectionT beacon_direction;

  uint16_t initiation_time;
  uint16_t expiration_time;

  uint16_t next_initiation_time;
  uint16_t next_expiration_time;

  bool is_new;
  bool is_valid;

  path the_path;

  std::string key;

  isd_path the_isd_path;

  Beacon (static_info_extension_t &static_info_extension,
          const OptimizationTarget *optimization_target, BeaconDirectionT beacon_direction,
          uint16_t i, uint16_t e, uint16_t nxt_i, uint16_t nxt_e, bool n, bool v, path &p,
          std::string &k, isd_path &isdp)
      : static_info_extension (static_info_extension),
        optimization_target (optimization_target),
        beacon_direction (beacon_direction),
        initiation_time (i),
        expiration_time (e),
        next_initiation_time (nxt_i),
        next_expiration_time (nxt_e),
        is_new (n),
        is_valid (v),
        the_path (p),
        key (k),
        the_isd_path (isdp)

  {
  }

  void ExtractPathSegmentFromPushBasedBeacon (PathSegment &path_segment) const;

  void ExtractPathSegmentFromPullBasedBeacon (PathSegment &path_segment) const;
};
} // namespace ns3
#endif //SCION_SIMULATOR_BEACON_H
