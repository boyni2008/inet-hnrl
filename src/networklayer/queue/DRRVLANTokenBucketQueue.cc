//
// Copyright (C) 2013 Kyeong Soo (Joseph) Kim
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


#include "DRRVLANTokenBucketQueue.h"

Define_Module(DRRVLANTokenBucketQueue);

DRRVLANTokenBucketQueue::DRRVLANTokenBucketQueue()
{
//    queues = NULL;
//    numFlows = NULL;
}

DRRVLANTokenBucketQueue::~DRRVLANTokenBucketQueue()
{
    for (int i=0; i<numFlows; i++)
    {
        delete queues[i];
//        cancelAndDelete(conformityTimer[i]);
    }
}

void DRRVLANTokenBucketQueue::initialize()
{
    PassiveQueueBase::initialize();

    outGate = gate("out");

    // general
    queueSize = par("queueSize");
    numFlows = par("numFlows");

    // VLAN classifier
    const char *classifierClass = par("classifierClass");
    classifier = check_and_cast<IQoSClassifier *>(createOne(classifierClass));
    classifier->setMaxNumQueues(numFlows);
    const char *vids = par("vids");
    classifier->initialize(vids);

    // FIFO queue for conformant packets
    fifo.setName("FIFO queue");

    // Per-subscriber queues with DRR scheduler for non-conformant packets
    queues.assign(numFlows, (cQueue *)NULL);
    meanBucketLength.assign(numFlows, bucketSize);
    peakBucketLength.assign(numFlows, mtu);
    lastTime.assign(numFlows, simTime());
    conformityFlag.assign(numFlows, false);
    conformityTimer.assign(numFlows, (cMessage *)NULL);
    for (int i=0; i<numFlows; i++)
    {
        char buf[32];
        sprintf(buf, "queue-%d", i);
        queues[i] = new cQueue(buf);
    }

    // state: RR scheduler
    currentQueueIndex = 0;

    // statistic
    warmupFinished = false;
    numBitsSent.assign(numFlows, 0.0);
    numQueueReceived.assign(numFlows, 0);
    numQueueDropped.assign(numFlows, 0);
    numQueueUnshaped.assign(numFlows, 0);
    numQueueSent.assign(numFlows, 0);
}

void DRRVLANTokenBucketQueue::handleMessage(cMessage *msg)
{
    if (warmupFinished == false)
    {   // start statistics gathering once the warm-up period has passed.
        if (simTime() >= simulation.getWarmupPeriod()) {
            warmupFinished = true;
            for (int i = 0; i < numFlows; i++)
            {
                numQueueReceived[i] = queues[i]->getLength();   // take into account the frames/packets already in queues
            }
        }
    }

    if (msg->isSelfMessage())
    {   // Conformity Timer expires
        int queueIndex = msg->getKind();    // message kind carries a queue index
        conformityFlag[queueIndex] = true;

        // update TBF status
        int pktLength = (check_and_cast<cPacket *>(queues[queueIndex]->front()))->getBitLength();
        bool conformance = isConformed(queueIndex, pktLength);
// DEBUG
        ASSERT(conformance == true);
// DEBUG
        if (packetRequested > 0)
        {
            cPacket *pkt = check_and_cast<cPacket *>(dequeue());
            if (pkt != NULL)
            {
                packetRequested--;
                if (warmupFinished == true)
                {
                    numBitsSent[currentQueueIndex] += pkt->getBitLength();
                    numQueueSent[currentQueueIndex]++;
                }
                sendOut(pkt);
            }
            else
            {
                error("%s::handleMessage: Error in round-robin scheduling", getFullPath().c_str());
            }
        }
    }
    else
    {   // a frame arrives
        int queueIndex = classifier->classifyPacket(msg);
        cQueue *queue = queues[queueIndex];
        if (warmupFinished == true)
        {
            numQueueReceived[queueIndex]++;
        }
        int pktLength = (check_and_cast<cPacket *>(msg))->getBitLength();
// DEBUG
        ASSERT(pktLength > 0);
// DEBUG
        if (queue->isEmpty())
        {
            if (isConformed(queueIndex, pktLength))
            {
                if (warmupFinished == true)
                {
                    numQueueUnshaped[queueIndex]++;
                }
                if (packetRequested > 0)
                {
                    packetRequested--;
                    if (warmupFinished == true)
                    {
                        numBitsSent[queueIndex] += pktLength;
                        numQueueSent[queueIndex]++;
                    }
                    currentQueueIndex = queueIndex;
                    sendOut(msg);
                }
                else
                {
                    bool dropped = enqueue(msg);
                    if (dropped)
                    {
                        if (warmupFinished == true)
                        {
                            numQueueDropped[queueIndex]++;
                        }
                    }
                    else
                    {
                        conformityFlag[queueIndex] = true;
                    }
                }
            }
            else
            {   // frame is not conformed
                bool dropped = enqueue(msg);
                if (dropped)
                {
                    if (warmupFinished == true)
                    {
                        numQueueDropped[queueIndex]++;
                    }
                }
                else
                {
                    triggerConformityTimer(queueIndex, pktLength);
                    conformityFlag[queueIndex] = false;
                }
            }
        }
        else
        {   // queue is not empty
            bool dropped = enqueue(msg);
            if (dropped)
            {
                if (warmupFinished == true)
                {
                    numQueueDropped[queueIndex]++;
                }
            }
        }

        if (ev.isGUI())
        {
            char buf[40];
            sprintf(buf, "q rcvd: %d\nq dropped: %d", numQueueReceived[queueIndex], numQueueDropped[queueIndex]);
            getDisplayString().setTagArg("t", 0, buf);
        }
    }
}

bool DRRVLANTokenBucketQueue::enqueue(cMessage *msg)
{
    int queueIndex = classifier->classifyPacket(msg);
    cQueue *queue = queues[queueIndex];

    if (frameCapacity && queue->length() >= frameCapacity)
    {
        EV << "Queue " << queueIndex << " full, dropping packet.\n";
        delete msg;
        return true;
    }
    else
    {
        queue->insert(msg);
        return false;
    }
}

cMessage *DRRVLANTokenBucketQueue::dequeue()
{
    bool found = false;
    int startQueueIndex = (currentQueueIndex + 1) % numFlows;  // search from the next queue for a frame to transmit
    for (int i = 0; i < numFlows; i++)
    {
       if (conformityFlag[(i+startQueueIndex)%numFlows])
       {
           currentQueueIndex = (i+startQueueIndex)%numFlows;
           found = true;
           break;
       }
    }

    if (found == false)
    {
        // TO DO: further processing?
        return NULL;
    }

    cMessage *msg = (cMessage *)queues[currentQueueIndex]->pop();

    // TO DO: update statistics

    // conformity processing for the HOL frame
    if (queues[currentQueueIndex]->isEmpty() == false)
    {
        int pktLength = (check_and_cast<cPacket *>(queues[currentQueueIndex]->front()))->getBitLength();
        if (isConformed(currentQueueIndex, pktLength))
        {
            conformityFlag[currentQueueIndex] = true;
        }
        else
        {
            conformityFlag[currentQueueIndex] = false;
            triggerConformityTimer(currentQueueIndex, pktLength);
        }
    }
    else
    {
        conformityFlag[currentQueueIndex] = false;
    }

    // return a packet from the scheduled queue for transmission
    return (msg);
}

void DRRVLANTokenBucketQueue::sendOut(cMessage *msg)
{
    send(msg, outGate);
}

void DRRVLANTokenBucketQueue::requestPacket()
{
    Enter_Method("requestPacket()");

    cMessage *msg = dequeue();
    if (msg==NULL)
    {
        packetRequested++;
    }
    else
    {
        if (warmupFinished == true)
        {
            numBitsSent[currentQueueIndex] += (check_and_cast<cPacket *>(msg))->getBitLength();
            numQueueSent[currentQueueIndex]++;
        }
        sendOut(msg);
    }
}

bool DRRVLANTokenBucketQueue::isConformed(int queueIndex, int pktLength)
{
    Enter_Method("isConformed()");

// DEBUG
    EV << "Last Time = " << lastTime[queueIndex] << endl;
    EV << "Current Time = " << simTime() << endl;
    EV << "Packet Length = " << pktLength << endl;
// DEBUG

    // update states
    simtime_t now = simTime();
    //unsigned long long meanTemp = meanBucketLength[queueIndex] + (unsigned long long)(meanRate*(now - lastTime[queueIndex]).dbl() + 0.5);
    unsigned long long meanTemp = meanBucketLength[queueIndex] + (unsigned long long)ceil(meanRate*(now - lastTime[queueIndex]).dbl());
    meanBucketLength[queueIndex] = (long long)((meanTemp > bucketSize) ? bucketSize : meanTemp);
    //unsigned long long peakTemp = peakBucketLength[queueIndex] + (unsigned long long)(peakRate*(now - lastTime[queueIndex]).dbl() + 0.5);
    unsigned long long peakTemp = peakBucketLength[queueIndex] + (unsigned long long)ceil(peakRate*(now - lastTime[queueIndex]).dbl());
    peakBucketLength[queueIndex] = int((peakTemp > mtu) ? mtu : peakTemp);
    lastTime[queueIndex] = now;

    if (pktLength <= meanBucketLength[queueIndex])
    {
        if  (pktLength <= peakBucketLength[queueIndex])
        {
            meanBucketLength[queueIndex] -= pktLength;
            peakBucketLength[queueIndex] -= pktLength;
            return true;
        }
    }
    return false;
}

// trigger TBF conformity timer for the HOL frame in the queue,
// indicating that enough tokens will be available for its transmission
void DRRVLANTokenBucketQueue::triggerConformityTimer(int queueIndex, int pktLength)
{
    Enter_Method("triggerConformityCounter()");

    double meanDelay = (pktLength - meanBucketLength[queueIndex]) / meanRate;
    double peakDelay = (pktLength - peakBucketLength[queueIndex]) / peakRate;

// DEBUG
    EV << "Packet Length = " << pktLength << endl;
    dumpTbfStatus(queueIndex);
    EV << "Delay for Mean TBF = " << meanDelay << endl;
    EV << "Delay for Peak TBF = " << peakDelay << endl;
    EV << "Current Time = " << simTime() << endl;
    EV << "Counter Expiration Time = " << simTime() + max(meanDelay, peakDelay) << endl;
// DEBUG

    scheduleAt(simTime() + max(meanDelay, peakDelay), conformityTimer[queueIndex]);
}

void DRRVLANTokenBucketQueue::dumpTbfStatus(int queueIndex)
{
    EV << "Last Time = " << lastTime[queueIndex] << endl;
    EV << "Current Time = " << simTime() << endl;
    EV << "Token bucket for mean rate/burst control " << endl;
    EV << "- Bucket size [bit]: " << bucketSize << endl;
    EV << "- Mean rate [bps]: " << meanRate << endl;
    EV << "- Bucket length [bit]: " << meanBucketLength[queueIndex] << endl;
    EV << "Token bucket for peak rate/MTU control " << endl;
    EV << "- MTU [bit]: " << mtu << endl;
    EV << "- Peak rate [bps]: " << peakRate << endl;
    EV << "- Bucket length [bit]: " << peakBucketLength[queueIndex] << endl;
}

void DRRVLANTokenBucketQueue::finish()
{
    unsigned long sumQueueReceived = 0;
    unsigned long sumQueueDropped = 0;
    unsigned long sumQueueShaped = 0;
    unsigned long sumQueueUnshaped = 0;

    for (int i=0; i < numFlows; i++)
    {
        std::stringstream ss_received, ss_dropped, ss_shaped, ss_sent, ss_throughput;
        ss_received << "packets received by per-VLAN queue[" << i << "]";
        ss_dropped << "packets dropped by per-VLAN queue[" << i << "]";
        ss_shaped << "packets shaped by per-VLAN queue[" << i << "]";
        ss_sent << "packets sent by per-VLAN queue[" << i << "]";
        ss_throughput << "bits/sec sent per-VLAN queue[" << i << "]";
        recordScalar((ss_received.str()).c_str(), numQueueReceived[i]);
        recordScalar((ss_dropped.str()).c_str(), numQueueDropped[i]);
        recordScalar((ss_shaped.str()).c_str(), numQueueReceived[i]-numQueueUnshaped[i]);
        recordScalar((ss_sent.str()).c_str(), numQueueSent[i]);
        recordScalar((ss_throughput.str()).c_str(), numBitsSent[i]/(simTime()-simulation.getWarmupPeriod()).dbl());
        sumQueueReceived += numQueueReceived[i];
        sumQueueDropped += numQueueDropped[i];
        sumQueueShaped += numQueueReceived[i] - numQueueUnshaped[i];
    }
    recordScalar("overall packet loss rate of per-VLAN queues", sumQueueDropped/double(sumQueueReceived));
    recordScalar("overall packet shaped rate of per-VLAN queues", sumQueueShaped/double(sumQueueReceived));
}
