///
/// @file   UDPVideoStreamCliWithSCFR.h
/// @author Kyeong Soo (Joseph) Kim <kyeongsoo.kim@gmail.com>
/// @date   2012-12-24
///
/// @brief  Declares UDPVideoStreamCliWithSCFR class, modeling a video
///         streaming client with source clock frequency recovery (SCFR) capability.
///
/// @par References:
/// <ol>
/// <li><a href="http://trace.eas.asu.edu/">Video trace library, Arizona State University</a>
/// </li>
/// </ol>
///
/// @remarks Copyright (C) 2012 Kyeong Soo (Joseph) Kim. All rights reserved.
///
/// @remarks This software is written and distributed under the GNU General
///          Public License Version 2 (http://www.gnu.org/licenses/gpl-2.0.html).
///          You must not remove this notice, or any other, from this software.
///
//
// Copyright (C) 2005 Andras Varga
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, see <http://www.gnu.org/licenses/>.
//


//
// based on the video streaming app of the similar name by Johnny Lai
//

#ifndef __INET_UDPVIDEOSTREAM_WITH_SCFR_H
#define __INET_UDPVIDEOSTREAM_WITH_SCFR_H

#include "UDPVideoStreamCli.h"
#include "UDPVideoStreamPacket_m.h"


///
/// @class UDPVideoStreamCliWithSCFR
/// @brief Implementation of a video streaming client with SCFR capability
///        to work with UDPVideoStreamSvrWithTrace2
///
class INET_API UDPVideoStreamCliWithSCFR : public UDPVideoStreamCli
{
  protected:
    // module parameters
    double clockFrequency;  ///< frequency of a local clock

    // status
    bool prevTimestampReceived;     ///< if true, start clock ratio measurement
    uint32_t prevArrivalTime;       ///< arrival time of a previous packet
    uint32_t prevTimestamp;         ///< timestamp of a previous packet

    // statistics
    simsignal_t fragmentStartSignal;        ///< indicators for start of fragment (used by SCFR for periodic streams)
    simsignal_t interArrivalTimeSignal;     ///< interarrival times (based on receiver clock)
    simsignal_t interDepartureTimeSignal;   ///< interdeparture times (based on source clock)
    simsignal_t measuredClockRatioSignal;   ///< measured ratio of clock frequencies between receiver and source

  protected:
    ///@name Overridden cSimpleModule functions
    //@{
    virtual void initialize();
    virtual void handleMessage(cMessage *msg);
    //@}

//  protected:
    virtual void receiveStream(cPacket *msg);
};


#endif