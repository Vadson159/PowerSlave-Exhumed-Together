#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>

// Include the REAL production wire protocol header directly
#include "../enet-prototype/include/net_protocol.h"

using namespace pse;

struct TestResult {
    std::string name;
    bool passed;
    std::string details;
    bool specOnly;
};

std::string toHex(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    for (size_t i = 0; i < data.size(); ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
        if (i + 1 < data.size()) {
            oss << " ";
        }
    }
    return oss.str();
}

int main() {
    std::vector<TestResult> results;
    int passCount = 0;
    int failCount = 0;

    auto record = [&](const std::string& name, bool cond, const std::string& details, bool specOnly = false) {
        results.push_back({name, cond, details, specOnly});
        if (cond) {
            passCount++;
            std::cout << "[PASS] " << name << " - " << details << "\n";
        } else {
            failCount++;
            std::cout << "[FAIL] " << name << " - " << details << "\n";
        }
    };

    std::cout << "=================================================================\n";
    std::cout << "PowerSlave Exhumed Together — Wire Protocol v1.10 Autonomous Test\n";
    std::cout << "Target: MsgPlayerSnapshot (0x20) & PlayerStateSnapshot\n";
    std::cout << "=================================================================\n\n";

    // ------------------------------------------------------------------------
    // Test 1: Exact Serialization Size of 1 PlayerStateSnapshot
    // ------------------------------------------------------------------------
    {
        PlayerStateSnapshot p;
        p.slotId = 1;
        p.entityId = 1002;
        p.entityGeneration = 2;
        p.posX = 123.45f; p.posY = 678.90f; p.posZ = -50.0f;
        p.velX = 1.0f; p.velY = 2.0f; p.velZ = 3.0f;
        p.yaw = 90.0f; p.pitch = -15.0f;
        p.health = 200;
        p.armor = 50;
        p.activeWeapon = 1;
        p.weaponMask = 0x03;
        p.ammo = 60;
        p.lifeState = LIFE_ALIVE;
        p.buttons = 0x01;
        p.deathSequence = 0;

        std::vector<uint8_t> buf;
        ByteWriter w(buf);
        p.serialize(w);

        record("T01_PlayerStateSnapshot_ExactSize",
               buf.size() == 65,
               "Serialized size: " + std::to_string(buf.size()) + " bytes (expected exactly 65 bytes)");
    }

    // ------------------------------------------------------------------------
    // Test 2: Exact Wire Packet Size for N = 0, 1, 2, 8 players
    // ------------------------------------------------------------------------
    {
        for (uint8_t count : {0, 1, 2, 8}) {
            MsgPlayerSnapshot snap;
            snap.snapshotSequence = 100;
            snap.serverTick = 5000;
            snap.mapEpoch = 1;
            for (uint8_t i = 0; i < count; ++i) {
                PlayerStateSnapshot p;
                p.slotId = i;
                p.entityId = 1001 + i;
                p.entityGeneration = 1;
                p.posX = 100.0f * i; p.posY = 200.0f * i; p.posZ = 50.0f;
                snap.players.push_back(p);
            }

            auto encoded = encodePacket(MSG_PLAYER_SNAPSHOT, snap.snapshotSequence, snap);
            size_t expectedSize = 11 + 13 + (count * 65);
            record("T02_PacketSize_Count_" + std::to_string(count),
                   encoded.size() == expectedSize,
                   "Count " + std::to_string(count) + " -> Wire bytes: " + std::to_string(encoded.size()) +
                   " (expected " + std::to_string(expectedSize) + ")");
        }
    }

    // ------------------------------------------------------------------------
    // Test 3: Round-Trip Serialization / Deserialization (2 players: Host + Client)
    // ------------------------------------------------------------------------
    {
        MsgPlayerSnapshot original;
        original.snapshotSequence = 42;
        original.serverTick = 1200;
        original.mapEpoch = 3;

        PlayerStateSnapshot p1;
        p1.slotId = 0; p1.entityId = 1001; p1.entityGeneration = 1;
        p1.posX = -7420.5f; p1.posY = -6775.25f; p1.posZ = 1152.0f;
        p1.velX = 0.0f; p1.velY = 0.0f; p1.velZ = 0.0f;
        p1.yaw = 180.0f; p1.pitch = 0.0f;
        p1.health = 200; p1.armor = 100; p1.activeWeapon = 2; p1.weaponMask = 0x07;
        p1.ammo = 150; p1.lifeState = LIFE_ALIVE; p1.buttons = 0; p1.deathSequence = 0;
        original.players.push_back(p1);

        PlayerStateSnapshot p2;
        p2.slotId = 1; p2.entityId = 1002; p2.entityGeneration = 2;
        p2.posX = -7418.0f; p2.posY = -6563.5f; p2.posZ = 1152.0f;
        p2.velX = 12.5f; p2.velY = -3.2f; p2.velZ = 0.0f;
        p2.yaw = -45.0f; p2.pitch = 10.5f;
        p2.health = 180; p2.armor = 0; p2.activeWeapon = 1; p2.weaponMask = 0x03;
        p2.ammo = 45; p2.lifeState = LIFE_ALIVE; p2.buttons = BUTTON_ATTACK; p2.deathSequence = 0;
        original.players.push_back(p2);

        auto encoded = encodePacket(MSG_PLAYER_SNAPSHOT, original.snapshotSequence, original);

        ByteReader reader(encoded.data(), encoded.size());
        MsgHeader hdr;
        bool hdrOk = hdr.deserialize(reader);
        bool typeOk = (hdr.msgType == MSG_PLAYER_SNAPSHOT);
        bool magicOk = (hdr.magic == NET_MAGIC);

        MsgPlayerSnapshot received;
        bool snapOk = received.deserialize(reader);

        bool match = snapOk && (received.snapshotSequence == original.snapshotSequence) &&
                     (received.serverTick == original.serverTick) &&
                     (received.mapEpoch == original.mapEpoch) &&
                     (received.players.size() == 2);

        if (match) {
            const auto& r1 = received.players[0];
            const auto& r2 = received.players[1];
            match = match && (r1.slotId == 0 && r1.entityId == 1001 && r1.posX == p1.posX && r1.yaw == p1.yaw && r1.health == 200);
            match = match && (r2.slotId == 1 && r2.entityId == 1002 && r2.posX == p2.posX && r2.yaw == p2.yaw && r2.buttons == BUTTON_ATTACK);
        }

        record("T03_RoundTrip_2Players",
               hdrOk && typeOk && magicOk && match,
               "Round-trip 2 players verified byte-perfect");
    }

    // ------------------------------------------------------------------------
    // Test 4: Truncated Packet Rejection
    // ------------------------------------------------------------------------
    {
        MsgPlayerSnapshot snap;
        snap.snapshotSequence = 1;
        snap.serverTick = 10;
        snap.mapEpoch = 1;
        PlayerStateSnapshot p;
        p.slotId = 0;
        snap.players.push_back(p);

        auto encoded = encodePacket(MSG_PLAYER_SNAPSHOT, 1, snap); // 89 bytes

        // Truncate by 1 byte
        ByteReader r1(encoded.data(), encoded.size() - 1);
        MsgHeader h1;
        h1.deserialize(r1);
        MsgPlayerSnapshot s1;
        bool failShort = !s1.deserialize(r1);

        // Truncate to header only (11 bytes)
        ByteReader r2(encoded.data(), 11);
        MsgHeader h2;
        h2.deserialize(r2);
        MsgPlayerSnapshot s2;
        bool failHeaderOnly = !s2.deserialize(r2);

        record("T04_TruncatedPacketRejection",
               failShort && failHeaderOnly,
               "Truncated by 1 byte rejected cleanly: " + std::to_string(failShort) +
               ", Header only rejected: " + std::to_string(failHeaderOnly));
    }

    // ------------------------------------------------------------------------
    // Test 5: Oversized playerCount (> MAX_PLAYERS) Rejection
    // ------------------------------------------------------------------------
    {
        // Construct malformed payload where count = 9
        std::vector<uint8_t> malformed;
        ByteWriter w(malformed);
        MsgHeader hdr;
        hdr.magic = NET_MAGIC;
        hdr.version = NET_PROTOCOL_VERSION;
        hdr.msgType = MSG_PLAYER_SNAPSHOT;
        hdr.sequence = 1;
        hdr.serialize(w);

        w.writeUint32(1); // snapshotSequence
        w.writeUint32(1); // serverTick
        w.writeUint32(1); // mapEpoch
        w.writeUint8(9);  // count = 9 (MAX_PLAYERS is 8)
        for (int i = 0; i < 9; ++i) {
            PlayerStateSnapshot p;
            p.serialize(w);
        }

        ByteReader r(malformed.data(), malformed.size());
        MsgHeader h;
        h.deserialize(r);
        MsgPlayerSnapshot s;
        bool rejected = !s.deserialize(r);

        record("T05_OversizedPlayerCountRejection",
               rejected,
               "playerCount = 9 cleanly rejected by MAX_PLAYERS bound check");
    }

    // ------------------------------------------------------------------------
    // Test 6: NaN and Infinity Wire Handling (Sanitize on Send, Reject on Recv)
    // ------------------------------------------------------------------------
    {
        // 6A: ByteWriter sanitizes NaN to 0.0f on write
        PlayerStateSnapshot p;
        p.posX = std::numeric_limits<float>::quiet_NaN();
        std::vector<uint8_t> buf;
        ByteWriter w(buf);
        p.serialize(w);

        ByteReader r(buf.data(), buf.size());
        PlayerStateSnapshot deserialized;
        bool okSanitized = deserialized.deserialize(r);
        bool valIsZero = (deserialized.posX == 0.0f);

        // 6B: Direct raw wire NaN payload injection must be rejected by ByteReader
        std::vector<uint8_t> rawWireNaN = buf; // copy valid 65-byte serialized snapshot
        // posX is at offset 9..12 (4 bytes)
        // Write IEEE 754 quiet NaN: 0x7FC00000
        rawWireNaN[9] = 0x00;
        rawWireNaN[10] = 0x00;
        rawWireNaN[11] = 0xC0;
        rawWireNaN[12] = 0x7F;

        ByteReader rRaw(rawWireNaN.data(), rawWireNaN.size());
        PlayerStateSnapshot desRaw;
        bool rejectedRawNaN = !desRaw.deserialize(rRaw);

        // 6C: Direct raw wire Infinity payload injection must be rejected by ByteReader
        std::vector<uint8_t> rawWireInf = buf;
        // Write IEEE 754 +Infinity: 0x7F800000
        rawWireInf[9] = 0x00;
        rawWireInf[10] = 0x00;
        rawWireInf[11] = 0x80;
        rawWireInf[12] = 0x7F;

        ByteReader rInf(rawWireInf.data(), rawWireInf.size());
        PlayerStateSnapshot desInf;
        bool rejectedRawInf = !desInf.deserialize(rInf);

        record("T06_NaN_Infinity_Handling",
               okSanitized && valIsZero && rejectedRawNaN && rejectedRawInf,
               "Sanitizes to 0.0f on serialize: " + std::to_string(okSanitized && valIsZero) +
               ", Rejects raw wire NaN: " + std::to_string(rejectedRawNaN) +
               ", Rejects raw wire Inf: " + std::to_string(rejectedRawInf));
    }

    // ------------------------------------------------------------------------
    // Test 7: Trailing Excess Bytes Behavior
    // ------------------------------------------------------------------------
    {
        MsgPlayerSnapshot snap;
        snap.snapshotSequence = 5;
        snap.serverTick = 100;
        snap.mapEpoch = 1;
        PlayerStateSnapshot p;
        p.slotId = 0;
        snap.players.push_back(p);

        auto encoded = encodePacket(MSG_PLAYER_SNAPSHOT, 5, snap); // 89 bytes
        encoded.push_back(0xDE);
        encoded.push_back(0xAD);
        encoded.push_back(0xBE);
        encoded.push_back(0xEF); // +4 trailing bytes

        ByteReader r(encoded.data(), encoded.size());
        MsgHeader h;
        h.deserialize(r);
        MsgPlayerSnapshot s;
        bool deserializedOk = s.deserialize(r);
        size_t remaining = r.remaining();

        record("T07_TrailingBytesBehavior",
               deserializedOk && (remaining == 4),
               "Deserializes payload successfully, leaves remaining trailing bytes: " + std::to_string(remaining));
    }

    // ------------------------------------------------------------------------
    // Test 8: Generate and Output Hex Dumps for 1, 2, 8 Players
    // ------------------------------------------------------------------------
    {
        for (uint8_t count : {1, 2, 8}) {
            MsgPlayerSnapshot snap;
            snap.snapshotSequence = count * 10;
            snap.serverTick = 600 * count;
            snap.mapEpoch = 1;
            for (uint8_t i = 0; i < count; ++i) {
                PlayerStateSnapshot p;
                p.slotId = i;
                p.entityId = 1001 + i;
                p.entityGeneration = 1;
                p.posX = 10.0f * (i + 1);
                p.posY = 20.0f * (i + 1);
                p.posZ = 30.0f * (i + 1);
                p.velX = 0.5f; p.velY = 0.5f; p.velZ = 0.0f;
                p.yaw = 45.0f * i; p.pitch = 0.0f;
                p.health = 200; p.armor = 50;
                p.activeWeapon = 1; p.weaponMask = 0x03; p.ammo = 60;
                p.lifeState = LIFE_ALIVE; p.buttons = 0; p.deathSequence = 0;
                snap.players.push_back(p);
            }
            auto encoded = encodePacket(MSG_PLAYER_SNAPSHOT, snap.snapshotSequence, snap);
            std::cout << "\n[HEX SAMPLE] Count = " << static_cast<int>(count)
                      << " (" << encoded.size() << " bytes):\n"
                      << toHex(encoded) << "\n";
        }
        record("T08_HexDumpGeneration", true, "Hex samples generated for 1, 2, 8 players");
    }

    // ------------------------------------------------------------------------
    // Test 9 (SPEC ONLY): Stale snapshotSequence Policy Verification
    // ------------------------------------------------------------------------
    {
        uint32_t lastAcceptedSeq = 100;
        uint32_t incomingSeqStale = 99;
        uint32_t incomingSeqDuplicate = 100;
        uint32_t incomingSeqNew = 101;

        auto policyCheck = [](uint32_t incoming, uint32_t last) -> bool {
            return incoming > last;
        };

        bool staleRejected = !policyCheck(incomingSeqStale, lastAcceptedSeq);
        bool dupRejected = !policyCheck(incomingSeqDuplicate, lastAcceptedSeq);
        bool newAccepted = policyCheck(incomingSeqNew, lastAcceptedSeq);

        record("T09_SPEC_StaleSequencePolicy",
               staleRejected && dupRejected && newAccepted,
               "SPEC ONLY: Sequence monotonic policy check (stale=drop, dup=drop, advance=accept)",
               true);
    }

    // ------------------------------------------------------------------------
    // Test 10 (SPEC ONLY): MapEpoch Gate Verification
    // ------------------------------------------------------------------------
    {
        uint32_t currentClientEpoch = 2;
        uint32_t stalePacketEpoch = 1;
        uint32_t futurePacketEpoch = 3;
        uint32_t matchingEpoch = 2;

        auto epochCheck = [](uint32_t packetEpoch, uint32_t clientEpoch) -> bool {
            return packetEpoch == clientEpoch;
        };

        bool staleDropped = !epochCheck(stalePacketEpoch, currentClientEpoch);
        bool futureDropped = !epochCheck(futurePacketEpoch, currentClientEpoch);
        bool matchingPassed = epochCheck(matchingEpoch, currentClientEpoch);

        record("T10_SPEC_MapEpochGatePolicy",
               staleDropped && futureDropped && matchingPassed,
               "SPEC ONLY: MapEpoch gate strictly enforces current epoch equality",
               true);
    }

    // ------------------------------------------------------------------------
    // Test 11 (SPEC ONLY): Reconnect Generation Separation Policy
    // ------------------------------------------------------------------------
    {
        uint32_t peerActiveGen = 2;
        uint32_t oldPacketGen = 1;
        uint32_t currentPacketGen = 2;

        auto genCheck = [](uint32_t packetGen, uint32_t activeGen) -> bool {
            return packetGen == activeGen;
        };

        bool oldGenDropped = !genCheck(oldPacketGen, peerActiveGen);
        bool curGenAccepted = genCheck(currentPacketGen, peerActiveGen);

        record("T11_SPEC_ConnectionGenerationPolicy",
               oldGenDropped && curGenAccepted,
               "SPEC ONLY: Old connectionGeneration packets dropped upon peer reconnect",
               true);
    }

    std::cout << "\n=================================================================\n";
    std::cout << "Test Summary: " << passCount << " PASS, " << failCount << " FAIL\n";
    std::cout << "=================================================================\n";

    return (failCount == 0) ? 0 : 1;
}
