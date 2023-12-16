#include <map>
#include <omnetpp.h>
#include <string>
#include "packet_m.h"
#include "ack_m.h"

using namespace omnetpp;

class tsn_switch: public cSimpleModule {
private:
    long packetSent;
    long packetReceived;
    int streamId;
    int sequenceNum;
    int k;
    int m;
    int sendMode; // 0-编码模式 1-副本模式

    // 部分统计数据
    long streamSent;
    long streamReceived;

    std::map<int, std::map<int, Packet*>> sendBuffer;          // 发送方缓存
    std::map<int, std::map<int, Packet*>> receiveDataBuffer;    // 接收方缓存
    std::map<int, std::map<int, Packet*>> receiveParityBuffer;  // 接收方编码缓存
    std::unordered_set<int> received;                           // 已接受的streamId
protected:
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void refreshDisplay() const override;
    virtual void finish() override;
    Packet* generatePacket(int streamId, int idx, bool isParity);
    void processMessage(Packet *p);
    void processACK(ACK *p);
    void sendPacket(Packet *p, const char *gatename, int gateindex);
    simtime_t getGateFinishTime(const char *gatename, int gateindex);
    simtime_t getAllGateFinishTime(const char *gatename);
    void clearReceiveDataBuffer(int streamId);
    void clearReceiveParityBuffer(int streamId);
    void showPacketInfo(Packet *p);
};

Define_Module(tsn_switch);

void tsn_switch::initialize() {
    packetSent = 0;
    packetReceived = 0;
    streamId = 0;
    sequenceNum = par("sequenceNum");
    this->sendMode = par("sendMode");
    this->k = par("k");         // 数据块的个数   (4,4) 每4个数据块发送4个编码块
    this->m = par("m");         // 编码块的个数
    this->streamSent = 0;       // 发送的流数目
    this->streamReceived = 0;   // 成功接受的流数目
    WATCH(packetSent);
    WATCH(packetReceived);
    WATCH(streamSent);
    WATCH(streamReceived);

    if (par("isTalker").boolValue() == true) {
        cMessage *selfMessage = new cMessage("self");
        scheduleAt(0.0, selfMessage);
    }
}

void tsn_switch::finish() {
    if (par("isTalker").boolValue() == true) {
        EV << "streamSent: " << this->streamSent << "\n";
        EV << "streamReceived: " << this->streamReceived << "\n";
        EV << "accuracy: " << double(this->streamReceived) / double(this->streamSent) << "\n";
    }
}

Packet* tsn_switch::generatePacket(int streamId, int idx, bool isParity) {
    std::string packetName = "stream" + std::to_string(streamId) + "_"
            + std::to_string(idx);
    Packet *packet = new Packet(packetName.c_str());
    packet->setStreamId(streamId);
    packet->setIdx(idx);
    packet->setIsParity(isParity);
    packet->setByteLength(12800000);        // 12.8MB 数据
//    packet->setSendTime(simTime().dbl());   // 发送时间
    sendBuffer[packet->getStreamId()][packet->getIdx()] = packet;
    return packet;
}

void tsn_switch::clearReceiveDataBuffer(int streamId) {
    for (auto it = this->receiveDataBuffer[streamId].begin();
            it != this->receiveDataBuffer[streamId].end(); it++) {
        delete (it->second);
        it->second = nullptr;
    }
    this->receiveDataBuffer[streamId].clear();
}

void tsn_switch::clearReceiveParityBuffer(int streamId) {
    for (auto it = this->receiveParityBuffer[streamId].begin();
            it != this->receiveParityBuffer[streamId].end(); it++) {
        delete (it->second);
        it->second = nullptr;
    }
    this->receiveParityBuffer[streamId].clear();
}

void tsn_switch::showPacketInfo(Packet *p) {
    EV << getName() << " receive stream: " << p->getStreamId() << " idx: "
            << p->getIdx() << " isParity: " << p->isParity() << "\n";
    simtime_t sendTime = SimTime(p->getSendTime());
    EV << "packet transmission time" << simTime() - sendTime << "\n";
}

void tsn_switch::processMessage(Packet *p) {
    if (p->hasBitError()) {
        EV << "[processMessage] receive error packet, streamId: "
                  << p->getStreamId() << " idx: " << p->getIdx()
                  << " isParity: " << p->isParity() << "\n";
        return;
    }
    this->showPacketInfo(p);
    if (this->received.find(p->getStreamId()) != this->received.end()) {
        return ;
    }
    if (!p->isParity()) {
        receiveDataBuffer[p->getStreamId()][p->getIdx()] = p;
    } else {
        receiveParityBuffer[p->getStreamId()][p->getIdx()] = p;
    }
    // k + m 编码方式的判断逻辑
    if (this->sendMode == 0
            && receiveDataBuffer[p->getStreamId()].size()
                    + receiveParityBuffer[p->getStreamId()].size()
                    >= k) {
        received.insert(p->getStreamId());
        // 加入数据恢复逻辑

    }
    // 副本方案的判断逻辑 是否能进行数据恢复
    if (this->sendMode == 1
            && this->receiveDataBuffer[p->getStreamId()].size()
                    == this->k) {
        received.insert(p->getStreamId());
    }
    if (received.find(p->getStreamId()) != received.end()) {
        EV << getName() << " already receive stream: " << p->getStreamId()
                  << "\n";
        ACK *ack = new ACK(std::to_string(p->getStreamId()).c_str());
        int streamId = p->getStreamId();
        ack->setStreamId(streamId);
        send(ack, "gate$o", 0);
        this->clearReceiveDataBuffer(streamId);
        if (this->sendMode == 0) {
            this->clearReceiveParityBuffer(streamId);
        }
        return;
    }
}

void tsn_switch::processACK(ACK *p) {
    if (this->received.find(p->getStreamId()) != this->received.end()) {
        return;
    }
    this->received.insert(p->getStreamId());
    this->streamReceived++;
    if (sendBuffer[p->getStreamId()].size()) {
        for (auto it = sendBuffer[p->getStreamId()].begin();
                it != sendBuffer[p->getStreamId()].end(); it++) {
            delete (it->second);
            it->second = nullptr;
        }
        sendBuffer[p->getStreamId()].clear();
    }
    EV << getName() << " receive ack from stream: " << p->getStreamId() << "\n";
}

void tsn_switch::sendPacket(Packet *p, const char *gatename, int gateindex) {
    simtime_t finishTime = this->getGateFinishTime(gatename, gateindex);
    if (finishTime <= simTime()) {
        p->setSendTime(simTime().dbl());
        send(p, gatename, gateindex);
    } else {
        p->setSendTime(finishTime.dbl());
        sendDelayed(p, finishTime - simTime(), gatename, gateindex);
    }
}

// 获取特定gate
simtime_t tsn_switch::getGateFinishTime(const char *gatename, int gateindex) {
    cGate *g = gate(gatename, gateindex);
    cChannel *c = g->getTransmissionChannel();
    simtime_t finishTime = c->getTransmissionFinishTime();
    return finishTime;
}

simtime_t tsn_switch::getAllGateFinishTime(const char *gatename) {
    simtime_t allGateFinishTime = simTime();
    for (int i = 0; i < gateSize(gatename); i++) {
        simtime_t gateFinishTime = this->getGateFinishTime(gatename, i);
        if (gateFinishTime > allGateFinishTime) {
            allGateFinishTime = gateFinishTime;
        }
    }
    return allGateFinishTime;
}

void tsn_switch::handleMessage(cMessage *msg) {
    this->packetReceived++;
    if (msg->isSelfMessage()) {
        if (this->streamSent >= 10000) {
            return; // 停止模拟
        }
        EV << getName() << " ready to send stream: " << streamId << "\n";
        this->streamSent++;
        // 流分割+发送Packet

        for (int i = 0;i < this->k; i++) {
            Packet *packet = generatePacket(streamId, i, false);
            sendPacket(packet->dup(), "gate$o", 0);
            packetSent++;
            if (this->sendMode == 1) {
                sendPacket(packet->dup(), "gate$o", 1);
                packetSent++;
            }
        }
        if (this->sendMode == 0) { // 编码发送模式
            for (int i = 0;i < this->m; i++) {
                Packet *parity = generatePacket(streamId, i, true);
                sendPacket(parity->dup(), "gate$o", 1);
                packetSent++;
            }
        }
        streamId++;
        cMessage *selfMessage = new cMessage("self");
        simtime_t allgateFinishTime = this->getAllGateFinishTime("gate$o");
        scheduleAt(allgateFinishTime, selfMessage);
    } else if (par("isTalker").boolValue() == true) {
        ACK *ack = check_and_cast<ACK*>(msg);
        processACK(ack);
    } else {
        Packet *packet = check_and_cast<Packet*>(msg);
        processMessage(packet);
    }
}

void tsn_switch::refreshDisplay() const {
    char buf[40];
    sprintf(buf, "rcvd: %ld sent: %ld", packetReceived, packetSent);
    getDisplayString().setTagArg("t", 0, buf);
}
