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

#include "pre-simulation-setup.h"

namespace ns3 {
void
SetTimeResolution (const std::string &time_res_str)
{
  if (time_res_str == "FS")
    {
      Time::SetResolution (Time::FS);
    }
  else if (time_res_str == "PS")
    {
      Time::SetResolution (Time::PS);
    }
  else if (time_res_str == "NS")
    {
      Time::SetResolution (Time::NS);
    }
  else if (time_res_str == "US")
    {
      Time::SetResolution (Time::US);
    }
  else if (time_res_str == "MS")
    {
      Time::SetResolution (Time::MS);
    }
  else if (time_res_str == "S")
    {
      Time::SetResolution (Time::S);
    }
  else if (time_res_str == "MIN")
    {
      Time::SetResolution (Time::MIN);
    }
}

void
InstantiateASesFromTopo (rapidxml::xml_node<> *xml_root,
                         std::map<int32_t, uint16_t> &real_to_alias_as_no,
                         std::map<uint16_t, int32_t> &alias_to_real_as_no, NodeContainer &as_nodes,
                         const YAML::Node &config)
{
  uint16_t alias_as_no = 0;
  rapidxml::xml_node<> *cur_xml_node = xml_root->first_node ("node");

  while (cur_xml_node)
    {
      PropertyContainer p = ParseProperties (cur_xml_node);
      std::string type;

      if (p.HasProperty ("type"))
        {
          type = p.GetProperty ("type");
        }
      else
        {
          type = "core";
        }

      bool malicious_border_routers = false;
      if (config["border_router"] && config["border_router"]["malicious_action"])
        {
          assert (p.HasProperty ("malicious"));
          if (p.GetProperty ("malicious") == "True")
            {
              malicious_border_routers = true;
            }
        }

      Ptr<ScionAs> as_node;
      if (type == "core")
        {
          as_node = CreateObject<ScionCoreAs> (0, (alias_as_no == 0), alias_as_no, cur_xml_node,
                                                 config, malicious_border_routers, Time (0));
        }
      else if (type == "non-core")
        {
          as_node = CreateObject<ScionAs> (0, (alias_as_no == 0), alias_as_no, cur_xml_node,
                                            config, malicious_border_routers, Time (0));
        }
      else
        {
          std::cerr << "Incompatible AS_node type!" << std::endl;
          exit (1);
        }
      as_nodes.Add (as_node);

      int32_t real_as_no = std::stoi (GetAttribute (cur_xml_node, "id"));
      uint16_t isd_number = 0;
      if (p.HasProperty ("isd"))
        {
          isd_number = std::stoi (p.GetProperty ("isd"));
        }

      as_to_isd_map.insert (std::make_pair (alias_as_no, isd_number));

      real_to_alias_as_no.insert (std::make_pair (real_as_no, alias_as_no));
      alias_to_real_as_no.insert (std::make_pair (alias_as_no, real_as_no));

      alias_as_no++;

      cur_xml_node = cur_xml_node->next_sibling ("node");
    }
}

void
InstantiatePathServers (const YAML::Node &config, const NodeContainer &as_nodes)
{
  bool only_propagation_delay = OnlyPropagationDelay (config);

  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      ScionAs *as_node = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
      PathServer *path_server =
          new PathServer (0, as_node->isd_number, as_node->as_number, 1, 0.0, 0.0, as_node);
      as_node->SetPathServer (path_server);

      if (only_propagation_delay)
        {
          path_server->SetProcessingDelay (Time (0), Time (0));
        }
      else
        {
          path_server->SetProcessingDelay (NanoSeconds (10), PicoSeconds (200));
        }
    }
}

void
GetMaliciousTimeRefAndTimeServer (const NodeContainer &as_nodes, const YAML::Node &config,
                                  std::vector<std::string> &time_reference_types,
                                  std::vector<std::string> &time_server_types)
{
  std::vector<uint16_t> indices_time_references;
  std::vector<uint16_t> indices_time_servers;

  uint16_t number_of_ases = as_nodes.GetN ();

  for (uint32_t i = 0; i < number_of_ases; ++i)
    {
      indices_time_references.push_back (i);
      indices_time_servers.push_back (i);
    }

  time_reference_types.resize (number_of_ases);
  time_server_types.resize (number_of_ases);

  if (config["time_service"]["truly_random_malicious"].as<uint16_t> () == 1)
    {
      std::shuffle (indices_time_references.begin (), indices_time_references.end (),
                    std::random_device{});
      std::shuffle (indices_time_servers.begin (), indices_time_servers.end (),
                    std::random_device{});
    }
  else
    {
      std::shuffle (indices_time_references.begin (), indices_time_references.end (),
                    std::mt19937{});
      std::shuffle (indices_time_servers.begin (), indices_time_servers.end (), std::mt19937{});
    }

  uint16_t number_of_malicious_time_references = (uint16_t) std::floor (
      ((double) config["time_service"]["percent_of_malicious_time_references"].as<uint16_t> () *
       (double) number_of_ases) /
      100.0);

  uint16_t number_of_malicious_time_servers = (uint16_t) std::floor (
      ((double) config["time_service"]["percent_of_malicious_time_servers"].as<uint16_t> () *
       (double) number_of_ases) /
      100.0);

  if (config["time_service"]["reference_clk"].as<std::string> () == "OFF")
    {
      for (uint16_t i = 0; i < number_of_ases; ++i)
        {
          time_reference_types.at (i) = "OFF";
        }
    }
  else
    {
      for (uint16_t i = 0; i < number_of_malicious_time_references; ++i)
        {
          time_reference_types.at (indices_time_references.at (i)) = "MALICIOUS";
        }

      for (uint16_t i = number_of_malicious_time_references; i < number_of_ases; ++i)
        {
          time_reference_types.at (indices_time_references.at (i)) = "ON";
        }
    }

  for (uint16_t i = 0; i < number_of_malicious_time_servers; ++i)
    {
      time_server_types.at (indices_time_servers.at (i)) = "MALICIOUS";
    }

  for (uint16_t i = number_of_malicious_time_servers; i < number_of_ases; ++i)
    {
      time_server_types.at (indices_time_servers.at (i)) = "NORMAL";
    }
}

void
GetTimeServiceSnapShotTypes (const NodeContainer &as_nodes, const YAML::Node &config,
                             std::vector<std::string> &snapshot_types,
                             uint16_t &global_scheduler_and_printer)
{
  global_scheduler_and_printer = 0;
  if (config["time_service"]["snapshot_type"].as<std::string> () == "PRINT_OFFSET_DIFF")
    {
      std::random_device rd;
      std::uniform_int_distribution<uint16_t> dist (0, as_nodes.GetN () - 1);
      global_scheduler_and_printer = dist (rd);
    }
  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      if (config["time_service"]["snapshot_type"].as<std::string> () == "PRINT_OFFSET_DIFF")
        {
          if (i == global_scheduler_and_printer)
            {
              snapshot_types.push_back ("PRINT_OFFSET_DIFF");
            }
          else
            {
              snapshot_types.push_back ("OFF");
            }
        }
      else
        {
          snapshot_types.push_back (config["time_service"]["snapshot_type"].as<std::string> ());
        }
    }
}

void
GetTimeServiceAlgVersions (const NodeContainer &as_nodes, const YAML::Node &config,
                           std::vector<std::string> &alg_versions,
                           const std::vector<std::string> &snapshot_types)
{
  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      if (snapshot_types.at (i) == "OFF")
        {
          alg_versions.push_back (
              config["time_service"]["alg_version_non_printing_instances"].as<std::string> ());
        }
      else
        {
          alg_versions.push_back (
              config["time_service"]["alg_version_printing_instances"].as<std::string> ());
        }
    }
}

void
InstantiateTimeServers (const YAML::Node &config, const NodeContainer &as_nodes)
{
  std::vector<std::string> time_reference_types;
  std::vector<std::string> time_server_types;
  std::vector<std::string> snapshot_types;
  std::vector<std::string> alg_versions;
  uint16_t global_scheduler_and_printer;

  GetMaliciousTimeRefAndTimeServer (as_nodes, config, time_reference_types, time_server_types);
  GetTimeServiceSnapShotTypes (as_nodes, config, snapshot_types, global_scheduler_and_printer);
  GetTimeServiceAlgVersions (as_nodes, config, alg_versions, snapshot_types);

  bool only_propagation_delay = OnlyPropagationDelay (config);

  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      ScionAs *as_node = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
      uint16_t alias_as_no = as_node->as_number;
      assert (alias_as_no == i);
      uint16_t isd_number = as_node->isd_number;
      bool parallel_scheduler = (alias_as_no == global_scheduler_and_printer);

      ScionHost *time_server = new TimeServer (
          0, isd_number, alias_as_no, 2, 0.0, 0.0, as_node, parallel_scheduler,
          Time (config["time_service"]["max_initial_drift"].as<std::string> ()),
          Time (config["time_service"]["max_drift_per_day"].as<std::string> ()),
          config["time_service"]["jitter_in_drift"].as<uint32_t> (),
          config["time_service"]["max_drift_coefficient"].as<uint32_t> (),
          Time (config["time_service"]["global_cut_off"].as<std::string> ()),
          Time (config["time_service"]["first_event"].as<std::string> ()),
          Time (config["time_service"]["last_event"].as<std::string> ()),
          Time (config["time_service"]["snapshot_period"].as<std::string> ()),
          Time (config["time_service"]["list_of_ases_req_period"].as<std::string> ()),
          Time (config["time_service"]["time_sync_period"].as<std::string> ()),
          config["time_service"]["G"].as<uint32_t> (),
          config["time_service"]["number_of_paths_to_use_for_global_sync"].as<uint32_t> (),
          config["time_service"]["read_disjoint_paths"].as<std::string> (),
          config["time_service"]["time_service_output_path"].as<std::string> (),
          time_reference_types.at (alias_as_no), time_server_types.at (alias_as_no),
          snapshot_types.at (alias_as_no), alg_versions.at (alias_as_no),
          Time (config["time_service"]["malcious_response_minimum_offset"].as<std::string> ()),
          config["time_service"]["path_selection"].as<std::string> ());

      as_node->AddHost (time_server);

      if (only_propagation_delay)
        {
          time_server->SetProcessingDelay (Time (0), Time (0));
        }
      else
        {
          time_server->SetProcessingDelay (NanoSeconds (10), PicoSeconds (200));
        }
    }
}

void
InstantiateLinksFromTopo (rapidxml::xml_node<> *xml_root, NodeContainer &as_nodes,
                          const std::map<int32_t, uint16_t> &real_to_alias_as_no,
                          const YAML::Node &config)
{
  bool only_propagation_delay = OnlyPropagationDelay (config);

  rapidxml::xml_node<> *curr_xml_node = xml_root->first_node ("link");
  while (curr_xml_node)
    {
      int32_t to = std::stoi (curr_xml_node->first_node ("to")->value ());
      int32_t from = std::stoi (curr_xml_node->first_node ("from")->value ());

      PropertyContainer p = ParseProperties (curr_xml_node);

      ld latitude = std::stod (p.GetProperty ("latitude"));
      ld longitude = std::stod (p.GetProperty ("longitude"));
      int32_t bwd = std::stoi (p.GetProperty ("capacity"));
      std::string rel = "core"; //p.GetProperty("rel");
      NeighbourRelation relation;

      // Check for the 3 possibilities in CAIDA topology
      if (rel == "peer")
        {
          relation = NeighbourRelation::PEER;
        }
      else if (rel == "core")
        {
          relation = NeighbourRelation::CORE;
        }
      else if (rel == "customer")
        {
          relation = NeighbourRelation::CUSTOMER;
        }
      else
        {
          relation = NeighbourRelation::CORE;
        }

      Ptr<ScionAs> from_as;
      Ptr<ScionAs> to_as;

      uint16_t to_alias_as_no = real_to_alias_as_no.at (to);
      uint16_t from_alias_as_no = real_to_alias_as_no.at (from);

      to_as = DynamicCast<ScionAs> (as_nodes.Get (to_alias_as_no));
      from_as = DynamicCast<ScionAs> (as_nodes.Get (from_alias_as_no));

      assert (to_as->as_number == to_alias_as_no);
      assert (from_as->as_number == from_alias_as_no);

      PointToPointHelper helper;
      helper.Install (from_as, to_as);

      to_as->AddToRemoteAsInfo (from_as->GetNDevices () - 1, PeekPointer (from_as));
      to_as->interfaces_coordinates.push_back (std::pair<ld, ld> (latitude, longitude));
      to_as->coordinates_to_interfaces.insert (std::make_pair (
          std::pair<ld, ld> (latitude, longitude), to_as->interfaces_coordinates.size () - 1));

      if (p.HasProperty ("to_if_id"))
        {
          assert ((uint32_t) std::stoi (p.GetProperty ("to_if_id")) == to_as->GetNDevices () - 1);
        }

      from_as->AddToRemoteAsInfo (to_as->GetNDevices () - 1, PeekPointer (to_as));
      from_as->interfaces_coordinates.push_back (std::pair<ld, ld> (latitude, longitude));
      from_as->coordinates_to_interfaces.insert (std::make_pair (
          std::pair<ld, ld> (latitude, longitude), from_as->interfaces_coordinates.size () - 1));

      if (p.HasProperty ("from_if_id"))
        {
          assert ((uint32_t) std::stoi (p.GetProperty ("from_if_id")) ==
                  from_as->GetNDevices () - 1);
        }

      if (config["border_router"])
        {
          Time to_propagation_delay, from_propagation_delay;
          Time to_transmission_delay, from_transmission_delay;
          Time to_processing_delay, from_processing_delay;
          Time to_processing_throughput_delay, from_processing_throughput_delay;

          to_propagation_delay = NanoSeconds (
              5); // Assuming 1m fiber optic between neighboring devices in the same location
          from_propagation_delay = NanoSeconds (5);

          if (only_propagation_delay)
            {
              to_transmission_delay = Time (0);
              from_transmission_delay = Time (0);

              to_processing_delay = Time (0);
              from_processing_delay = Time (0);

              to_processing_throughput_delay = Time (0);
              from_processing_throughput_delay = Time (0);
            }
          else
            {
              to_transmission_delay =
                  PicoSeconds (20); //Per byte transmission delay assuming 400 Gbps link
              from_transmission_delay = PicoSeconds (20);

              to_processing_delay = NanoSeconds (10);
              from_processing_delay = NanoSeconds (10);

              to_processing_throughput_delay = PicoSeconds (200); // 5 Giga packets per second
              from_processing_throughput_delay = PicoSeconds (200);
            }

          BorderRouter *to_br = to_as->AddBr (latitude, longitude, to_processing_delay,
                                              to_processing_throughput_delay);
          BorderRouter *from_br = from_as->AddBr (latitude, longitude, from_processing_delay,
                                                  from_processing_throughput_delay);

          to_br->AddToPropagationDelays (to_propagation_delay);
          to_br->AddToTransmissionDelays (to_transmission_delay);

          from_br->AddToPropagationDelays (from_propagation_delay);
          from_br->AddToTransmissionDelays (from_transmission_delay);

          to_br->AddToIfForwadingTable (to_as->GetNDevices () - 1, to_br->GetNDevices () - 1);
          from_br->AddToIfForwadingTable (from_as->GetNDevices () - 1, from_br->GetNDevices () - 1);

          to_br->AddToRemoteNodesInfo (from_br, from_br->GetNDevices () - 1, from_as->isd_number,
                                       from_as->as_number);
          from_br->AddToRemoteNodesInfo (to_br, to_br->GetNDevices () - 1, to_as->isd_number,
                                         to_as->as_number);
        }

      to_as->inter_as_bwds.push_back (bwd);
      from_as->inter_as_bwds.push_back (bwd);

      NeighbourRelation to_rel;
      NeighbourRelation from_rel;

      switch (relation)
        {
        case NeighbourRelation::PEER:
          to_rel = NeighbourRelation::PEER;
          from_rel = NeighbourRelation::PEER;
          break;
        case NeighbourRelation::CORE:
          to_rel = NeighbourRelation::CORE;
          from_rel = NeighbourRelation::CORE;
          break;
        case NeighbourRelation::CUSTOMER:
          to_rel = NeighbourRelation::PROVIDER;
          from_rel = NeighbourRelation::CUSTOMER;
          break;
        case NeighbourRelation::PROVIDER:
          // Should never happen, there is no "Provider" type in xml files
          to_rel = NeighbourRelation::CUSTOMER;
          from_rel = NeighbourRelation::PROVIDER;
          assert (false);
        }

      to_as->interface_to_neighbor_map.insert (
          std::make_pair (to_as->GetNDevices () - 1, from_as->as_number));
      if (to_as->interfaces_per_neighbor_as.find (from_as->as_number) !=
          to_as->interfaces_per_neighbor_as.end ())
        {
          to_as->interfaces_per_neighbor_as.at (from_as->as_number)
              .push_back ((uint16_t) to_as->GetNDevices () - 1);
        }
      else
        {
          std::vector<uint16_t> tmp;
          tmp.push_back ((uint16_t) to_as->GetNDevices () - 1);
          to_as->interfaces_per_neighbor_as.insert (std::make_pair (from_as->as_number, tmp));
          to_as->neighbors.push_back (std::make_pair (from_as->as_number, to_rel));
        }

      from_as->interface_to_neighbor_map.insert (
          std::make_pair (from_as->GetNDevices () - 1, to_as->as_number));
      if (from_as->interfaces_per_neighbor_as.find (to_as->as_number) !=
          from_as->interfaces_per_neighbor_as.end ())
        {
          from_as->interfaces_per_neighbor_as.at (to_as->as_number)
              .push_back (from_as->GetNDevices () - 1);
        }
      else
        {
          std::vector<uint16_t> tmp;
          tmp.push_back ((uint16_t) from_as->GetNDevices () - 1);
          from_as->interfaces_per_neighbor_as.insert (std::make_pair (to_as->as_number, tmp));
          from_as->neighbors.push_back (std::make_pair (to_as->as_number, from_rel));
        }

      to_as->GetBeaconServer ()->PerLinkInitializations (curr_xml_node, config);
      from_as->GetBeaconServer ()->PerLinkInitializations (curr_xml_node, config);

      curr_xml_node = curr_xml_node->next_sibling ("link");
    }
}

void
InitializeASesAttributes (const NodeContainer &as_nodes,
                          std::map<int32_t, uint16_t> &real_to_alias_as_no,
                          rapidxml::xml_node<> *xml_node, const YAML::Node &config)
{
  bool only_propagation_delay = OnlyPropagationDelay (config);

  if (config["border_router"])
    {
      for (uint64_t i = 0; i < as_nodes.GetN (); ++i)
        {
          ScionAs *as_node = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
          as_node->DoInitializations (as_nodes.GetN (), xml_node, config, only_propagation_delay);
        }
    }
  else
    {
      for (uint64_t i = 0; i < as_nodes.GetN (); ++i)
        {
          ScionAs *as_node = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
          as_node->DoInitializations (as_nodes.GetN (), xml_node, config);
        }
    }

  if (config["beacon_service"]["br_br_energy_file"])
    {
      ReadBr2BrEnergy (as_nodes, real_to_alias_as_no, config);
    }
}

bool
OnlyPropagationDelay (const YAML::Node &config)
{
  if (config["only_propagation_delay"] && config["only_propagation_delay"].as<int32_t> () != 0)
    {
      return true;
    }

  return false;
}
} // namespace ns3