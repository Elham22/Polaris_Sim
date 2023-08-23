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
 * Author: Pascal Suter passuter@student.ethz.ch
 */
#include "src/SCION/model/externs.h"
#include "src/SCION/model/scion-core-as.h"
#include "general-traffic-app.h"

namespace ns3 {

void
GeneralTrafficApp::SetBwdFactor (double factor, double avail_bwd_Mbit)
{
  if (factor <= 0.) 
  {
    SetDataRate (0.);
    return;
  }
  double oldBitrate = static_cast<double>(m_cbrRate.GetBitRate());
  SetDataRate (avail_bwd_Mbit * factor / 200.);

  if (oldBitrate <= 0. && factor > 0.)
  {
    PPBP ();
  }
}

void 
GeneralTrafficApp::SetBurstArrivals (double val)
{
  m_burstArrivals = CreateObjectWithAttributes <ConstantRandomVariable> ("Constant", DoubleValue (val));
}

void 
GeneralTrafficApp::SetBurstLength (double val)
{
  m_burstLength = CreateObjectWithAttributes <ConstantRandomVariable> ("Constant", DoubleValue (val));
}

void 
GeneralTrafficApp::SetDataRate (double val_Mbit)
{
  m_cbrRate = DataRate (std::to_string (val_Mbit) + "Mb/s");
}

void
GeneralTrafficApp::GenerateAppTraffic ()
{
  PPBP ();
}

double
GeneralTrafficApp::ComputeScore (double latency, double loss, double additional_scoring, uint32_t path_id)
{
  // TODO
  return App::ComputeScore (latency, loss, additional_scoring, path_id);
}

void
GeneralTrafficApp::PrintResults ()
{
  App::PrintResults ();
  std::cout << "General traffic specific print results not implemented" << std::endl;
}

void
GeneralTrafficApp::PPBP() // Poisson Pareto Burst 
{  
  double bitrate = static_cast<double>(m_cbrRate.GetBitRate());
  if (bitrate <= 0 || stopped)
    {
      // stop sending packets and no longer schedule anything.
      m_activebursts = 1;
      return;
    }	

  double inter_burst_intervals;
  inter_burst_intervals = 1/m_burstArrivals->GetValue();

  //std::cout << "inter_burst " << inter_burst_intervals << std::endl;
  Ptr<ExponentialRandomVariable> exp = CreateObjectWithAttributes<ExponentialRandomVariable> ("Mean", DoubleValue (inter_burst_intervals));
  Time t_poisson_arrival = Seconds (exp->GetValue());
  Simulator::Schedule(t_poisson_arrival, &GeneralTrafficApp::PoissonArrival, this);
  
  // Pareto
  m_shape = 3 - 2 * m_h;
  double scale = m_burstLength->GetValue() * (m_shape - 1.0) / m_shape;
  m_timeSlot = Seconds(scale);
  
  Ptr<ParetoRandomVariable> pareto = CreateObjectWithAttributes<ParetoRandomVariable> ("Scale", DoubleValue (scale), "Shape", DoubleValue (m_shape));
  
  Simulator::Schedule(t_poisson_arrival + Seconds (pareto->GetValue()), &GeneralTrafficApp::ParetoDeparture, this);
  
  Simulator::Schedule(t_poisson_arrival, &GeneralTrafficApp::PPBP, this);
}

void
GeneralTrafficApp::PoissonArrival()
{
  ++m_activebursts;
  if (m_offPeriod) ScheduleNextTx();
}

void
GeneralTrafficApp::ParetoDeparture()
{
  --m_activebursts;
}
	
void
GeneralTrafficApp::ScheduleNextTx()
{
  double bitrate = static_cast<double>(m_cbrRate.GetBitRate());
  if (bitrate <= 0)
    {
      // stop sending packets and no longer schedule anything.
      m_offPeriod = true;
      return;
    }

  uint32_t bits = (m_pktSize + 30) * 8 * scale;
  Time nextTime(Seconds (bits / bitrate));
  
  if (m_activebursts != 0)
  {
    m_offPeriod = false;
    double data_rate = (double) nextTime.GetSeconds() / m_activebursts;
    Simulator::Schedule(Seconds(data_rate),&GeneralTrafficApp::SendPacket, this);
  }
  else
  {
    m_offPeriod = true;
  }

}	

void
GeneralTrafficApp::SendPacket()
{
  // note, pktSize doesn't have to be scaled here because scaling is made in SendData
  SendData (m_pktSize, GetPath ());
  ScheduleNextTx();
}

void
BackgroundTrafficApp::StartAppTraffic ()
{
  // as link is fixed, directly start sending.
  // 7 seconds delay to allow user defined events to overwrite bitrate
  double bitrate = static_cast<double>(m_cbrRate.GetBitRate());
  if (bitrate > 0)
    {
      Simulator::Schedule (Seconds (7), &BackgroundTrafficApp::GenerateAppTraffic, this);
    }
}

void
BackgroundTrafficApp::SendData (uint32_t size, std::vector<const ns3::PathSegment *> path)
{
  br->SendBackgroundPacket (size * scale, dst_ia, path, inf, hopf);
}

std::string
BackgroundTrafficApp::GetIfInfoAsString ()
{
  auto seg = all_paths.at (best_path_id).at (inf);
  auto hop = seg->hops.at (hopf);
  return "ing " + std::to_string (GET_HOP_ING_IF (hop)) + " eg " + std::to_string (GET_HOP_EG_IF (hop));
}

std::map<std::pair<BorderRouter *, uint16_t>, BackgroundTrafficApp *>
BackgroundTrafficApp::backgroundTrafficApps = {};

void
BackgroundTrafficApp::AddBackgroundTraffic (std::vector<std::vector<const PathSegment *>> all_paths, double bwdFactor)
{
  //std::cout << "Adding background traffic" << std::endl;

  for (uint32_t i = 0; i < all_paths.size (); ++i)
    {
      auto path = all_paths.at (i);
      //ScionHost::PrintPath (path);
      for (uint16_t inf = 0; inf < path.size (); ++inf)
        {
          auto seg = path.at (inf);
          for (uint16_t hopf = 0; hopf < seg->hops.size () - 1; ++hopf)
            {
              //std::cout << "Path " << i << ", hop " << hopf << std::endl;
              auto hop_from = seg->hops.at (hopf);
              auto hop_to = seg->hops.at (hopf+1);
              // auto isd = GET_HOP_ISD (hop_from);
              auto as_num = GET_HOP_AS (hop_from);
              ScionAs *as_node = dynamic_cast<ScionAs *> (PeekPointer (nodes.Get (as_num))); // TODO currently only supports one ISD
              //std::cout << as_node->isd_number << ":" << as_node->as_number << std::endl;
              auto ing = GET_HOP_ING_IF (hop_from);
              BorderRouter *br = as_node->GetBr (ing);
              //uint16_t local_if = br->GetLocalIfFromASIf (ing);
              uint16_t local_if = br->GetLocalIfFromASIf (GET_HOP_EG_IF (hop_from));
              double avail_bwd_Mbit = br->GetBwdGbit (local_if) * 1000.;
              // double background_bwd = avail_bwd_Mbit * bwdFactor / 10.; // divide by ten to not have to enter too small values in configs
              auto key = std::make_pair (br, local_if);
              if (backgroundTrafficApps.find (key) == backgroundTrafficApps.end ())
                {
                  BackgroundTrafficApp *app = new BackgroundTrafficApp (br, GET_HOP_IA (hop_to), all_paths, i, inf, hopf);
                  app->SetBwdFactor (bwdFactor, avail_bwd_Mbit);
                  backgroundTrafficApps[key] = app;
                  app->StartAppTraffic ();
                }
              else {
                backgroundTrafficApps.at (key)->SetBwdFactor (bwdFactor, avail_bwd_Mbit);
              }
              
              //std::cout << "BR " << br->GetLogitude () << ", " << br->GetLatitude () << ", if " << local_if << std::endl;
            }
        }


    }
}
} // namespace ns3