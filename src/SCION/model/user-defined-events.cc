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

#include "user-defined-events.h"
#include "time-server.h"

namespace ns3 {
void
UserDefinedEvents::RunUserSpecifiedEvent (const std::string &func_name,
                                             std::vector<std::string> vec)
{
  switch (vec.size ())
    {
    case 0:
      function_name_to_function[func_name].operator()<0> (vec);
      break;
    case 1:
      function_name_to_function[func_name].operator()<1> (vec);
      break;
    case 2:
      function_name_to_function[func_name].operator()<2> (vec);
      break;
    case 3:
      function_name_to_function[func_name].operator()<3> (vec);
      break;
    case 4:
      function_name_to_function[func_name].operator()<4> (vec);
      break;
    case 5:
      function_name_to_function[func_name].operator()<5> (vec);
      break;
    case 6:
      function_name_to_function[func_name].operator()<6> (vec);
      break;
    case 7:
      function_name_to_function[func_name].operator()<7> (vec);
      break;
    case 8:
      function_name_to_function[func_name].operator()<8> (vec);
      break;
    case 9:
      function_name_to_function[func_name].operator()<9> (vec);
      break;
    case 10:
      function_name_to_function[func_name].operator()<10> (vec);
      break;
    default:
      break;
    }
}

void
UserDefinedEvents::ConstructFuncMap ()
{
  function_name_to_function["add_host"] = FunctionFactory (&UserDefinedEvents::AddAHost, this);
  function_name_to_function["link_down"] = FunctionFactory (&UserDefinedEvents::LinkDown, this);
  function_name_to_function["link_up"] = FunctionFactory (&UserDefinedEvents::LinkUp, this);
  function_name_to_function["send_packet"] =
      FunctionFactory (&UserDefinedEvents::SendAPacket, this);
  function_name_to_function["send_packet_batch"] =
      FunctionFactory (&UserDefinedEvents::SendPacketBatch, this);
  function_name_to_function["time_references_down"] =
      FunctionFactory (&UserDefinedEvents::TimeReferencesDown, this);
  function_name_to_function["time_references_up"] =
      FunctionFactory (&UserDefinedEvents::TimeReferencesUp, this);
}

void
UserDefinedEvents::ReadAndScheduleUserDefinedEvents (const std::string &events_file_str)
{
  nlohmann::json events_json;
  std::ifstream events_file (events_file_str);
  events_file >> events_json;
  events_file.close ();

  for (auto const &event : events_json.at ("events"))
    {
      Time time = Time ((std::string) event["time"]);
      std::string func_name = (std::string) event["type"];
      std::vector<std::string> args_v;

      for (uint32_t i = 0; i < event["args"].size (); ++i)
        {
          args_v.push_back ((std::string) event["args"][i]);
        }

      Simulator::Schedule (time, &UserDefinedEvents::RunUserSpecifiedEvent, this, func_name,
                           args_v);
    }
}

void
UserDefinedEvents::AddAHost (std::string isd_number, std::string real_as_no,
                               std::string local_address)
{
}

void
UserDefinedEvents::LinkDown (std::string isd_number, std::string real_as_no, std::string if_id)
{
}
void
UserDefinedEvents::LinkUp (std::string isd_number, std::string real_as_no, std::string if_id)
{
}

void
UserDefinedEvents::SendAPacket (std::string src_isd_number, std::string real_src_as_no,
                                  std::string src_local_address, std::string dst_isd_number,
                                  std::string real_dst_as_no, std::string dst_local_address,
                                  std::string pyload_size)
{
}

void
UserDefinedEvents::SendPacketBatch (std::string src_isd_number, std::string real_src_as_no,
                                      std::string src_local_address, std::string dst_isd_number,
                                      std::string real_dst_as_no, std::string dst_local_address,
                                      std::string pyload_size, std::string no_pkts)
{
}

void
UserDefinedEvents::TimeReferencesDown ()
{
  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      ScionAs *scion_as = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
      TimeServer *time_server = dynamic_cast<TimeServer *> (scion_as->GetHost (2));

      time_server->reference_time_type = ReferenceTimeType::OFF;
    }
}

void
UserDefinedEvents::TimeReferencesUp ()
{
  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      ScionAs *scion_as = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
      TimeServer *time_server = dynamic_cast<TimeServer *> (scion_as->GetHost (2));

      time_server->reference_time_type = ReferenceTimeType::ON;
    }
}
} // namespace ns3
