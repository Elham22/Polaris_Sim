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
#include "general-traffic-app.h"

namespace ns3 {
void
GeneralTrafficApp::GenerateAppTraffic ()
{
  PPBP ();
}

double
GeneralTrafficApp::ComputeScore (PathInfo path_info)
{
  // TODO
  return App::ComputeScore (path_info);
}

void
GeneralTrafficApp::PrintResults ()
{
  // TODO
  std::cout << "General traffic print results not implemented" << std::endl;
}

void
GeneralTrafficApp::PPBP() // Poisson Pareto Burst 
{		
  double inter_burst_intervals;
  inter_burst_intervals = 1/m_burstArrivals->GetValue();

  Ptr<ExponentialRandomVariable> exp = CreateObjectWithAttributes<ExponentialRandomVariable> ("Mean", DoubleValue (inter_burst_intervals));
  Time t_poisson_arrival = Seconds (exp->GetValue());
  Simulator::Schedule(t_poisson_arrival,&GeneralTrafficApp::PoissonArrival, this);
  
  // Pareto
  m_shape = 3 - 2 * m_h;
  double scale = m_burstLength->GetValue() * (m_shape - 1.0) / m_shape;
  m_timeSlot = Seconds(scale);
  
  Ptr<ParetoRandomVariable> pareto = CreateObjectWithAttributes<ParetoRandomVariable> ("Scale", DoubleValue (scale), "Shape", DoubleValue (m_shape));
  
  Simulator::Schedule(t_poisson_arrival + Seconds (pareto->GetValue()),&GeneralTrafficApp::ParetoDeparture, this);
  
  Simulator::Schedule(t_poisson_arrival,&GeneralTrafficApp::PPBP, this);
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
  uint32_t bits = (m_pktSize + 30) * 8;
  Time nextTime(Seconds (bits / 
                static_cast<double>(m_cbrRate.GetBitRate())));
  
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
  SendData (m_pktSize, GetPath ());
  ScheduleNextTx();
}
	
} // namespace ns3