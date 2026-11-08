#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <optional>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace pse {

// ============================================================================
// Constants & Configuration
// ============================================================================

constexpr uint32_t NET_MAGIC = 0x50534558; // 'P','S','E','X'
constexpr uint16_t NET_PROTOCOL_VERSION = 0x0113; // P23 decision 2026-11-06; no downgrade.
constexpr uint32_t MAX_PLAYERS = 8;
constexpr uint8_t HOST_SLOT_ID = 0; // P1
constexpr size_t MAX_PACKET_SIZE = 4096;
constexpr size_t MAX_STRING_LEN = 32;
constexpr size_t MAX_TOKEN_LEN = 64;
constexpr size_t MAX_PARTICIPANT_ID_LEN = 48;
constexpr size_t MAX_INBOUND_QUEUE_SIZE = 128;

// ENet Channel Mappings
enum NetChannel : uint8_t {
    CHANNEL_RELIABLE_CONTROL = 0,     // Connect, disconnect, map change, respawn, world events
    CHANNEL_UNRELIABLE_SEQUENCED = 1,  // Inputs, player snapshots
    CHANNEL_DIAGNOSTIC = 2,           // Ping, Pong, telemetry
    CHANNEL_WORLD_SNAPSHOT = 3,       // High-frequency batched world replication snapshots
    CHANNEL_COUNT = 4
};

// Message Type IDs
enum NetMessageType : uint8_t {
    MSG_NONE = 0x00,
    MSG_HELLO = 0x01,
    MSG_WELCOME = 0x02,
    MSG_REJECT = 0x03,
    MSG_PLAYER_JOIN = 0x04,
    MSG_PLAYER_LEAVE = 0x05,
    MSG_INPUT_COMMAND = 0x10,
    MSG_PLAYER_SNAPSHOT = 0x20,
    MSG_ENTITY_SPAWN = 0x21,
    MSG_ENTITY_DESPAWN = 0x22,
    MSG_WORLD_SNAPSHOT = 0x23,
    MSG_WORLD_BASELINE = 0x24,
    MSG_WORLD_EVENT = 0x30,
    MSG_INTERACTION_REQUEST = 0x31,
    MSG_WORLD_EVENT_CONFIRM = 0x32,
    MSG_MAP_CHANGE = 0x35,
    MSG_MAP_READY = 0x36,
    MSG_RESPAWN_REQUEST = 0x40,
    MSG_RESPAWN_CONFIRM = 0x41,
    MSG_PING = 0x50,
    MSG_PONG = 0x51,
    MSG_CHAT_REQUEST = 0x60,
    MSG_CHAT_BROADCAST = 0x61,
    MSG_UI_ROSTER = 0x62,
    MSG_UI_POSE = 0x63 // host-authoritative UI pose only; unreliable sequenced
};

// Rejection Reasons
enum RejectReason : uint8_t {
    REJECT_PROTOCOL_MISMATCH = 1,
    REJECT_SERVER_FULL = 2,
    REJECT_INVALID_TOKEN = 3,
    REJECT_BANNED = 4,
    REJECT_RATE_LIMITED = 5,
    REJECT_MALFORMED = 6,
    REJECT_ALREADY_CONNECTED = 7,
    REJECT_HIJACK_ATTEMPT = 8
};

// Disconnect Reasons
enum LeaveReason : uint8_t {
    LEAVE_USER_QUIT = 0,
    LEAVE_TIMEOUT = 1,
    LEAVE_KICKED = 2,
    LEAVE_PROTOCOL_ERROR = 3,
    LEAVE_SERVER_SHUTDOWN = 4
};

const char* getProtocolVersionString();
const char* getRejectReasonString(RejectReason reason);
const char* getLeaveReasonString(LeaveReason reason);

// Player Life State
enum LifeState : uint8_t {
    LIFE_ALIVE = 0,
    LIFE_DYING = 1,
    LIFE_DEAD = 2,
    LIFE_RESPAWNING = 3
};

// Starter Weapons & Bitmasks (aligned with KEX 2022 KPF definitions)
enum WeaponType : uint8_t {
    WEAPON_MACHETE = 0,
    WEAPON_PISTOL = 1,
    WEAPON_M60 = 2,
    WEAPON_BOMBS = 3,         // KEX Slot 3 (was FLAMETHROWER)
    WEAPON_FLAMETHROWER = 4,  // KEX Slot 4 (was GRENADES)
    WEAPON_COBRA = 5,
    WEAPON_RING = 6,
    WEAPON_MANACLES = 7,      // KEX Slot 7 (was missing)
    WEAPON_COUNT = 8
};
constexpr WeaponType WEAPON_MANACLE = WEAPON_MANACLES; // Convenience alias

constexpr uint32_t DEFAULT_WEAPON_MASK = (1u << WEAPON_MACHETE) | (1u << WEAPON_PISTOL);
constexpr uint32_t ALL_WEAPONS_MASK = (1u << WEAPON_COUNT) - 1u; // 0xFF (bits 0..7)
constexpr uint16_t DEFAULT_PISTOL_AMMO = 60; // KEX C19 verified starting pistol ammo
constexpr int32_t DEFAULT_HEALTH = 200;      // KEX C19 verified starting health
constexpr int32_t DEFAULT_ARMOR = 0;

const char* getWeaponTypeName(WeaponType type);

// ============================================================================
// Angle Conversion Utilities (Degrees <-> Radians)
// PowerSlave Exhumed / KEX 2022 uses radians internally for camera & physics.
// The ENet wire protocol uses degrees (-180..+180 or 0..360) for network replication.
// ============================================================================
constexpr float NET_PI = 3.14159265358979323846f;
constexpr float RAD2DEG = 180.0f / NET_PI;
constexpr float DEG2RAD = NET_PI / 180.0f;

inline float radToDeg(float radians) {
    return radians * RAD2DEG;
}

inline float degToRad(float degrees) {
    return degrees * DEG2RAD;
}

// Normalize angle in degrees to [-180.0, +180.0)
inline float normalizeAngleDeg(float degrees) {
    float a = std::fmod(degrees + 180.0f, 360.0f);
    if (a < 0.0f) a += 360.0f;
    return a - 180.0f;
}

// Normalize angle in radians to [-PI, +PI)
inline float normalizeAngleRad(float radians) {
    float a = std::fmod(radians + NET_PI, 2.0f * NET_PI);
    if (a < 0.0f) a += 2.0f * NET_PI;
    return a - NET_PI;
}

// Input Button Flags
enum ButtonFlags : uint32_t {
    BUTTON_ATTACK = 1u << 0,
    BUTTON_JUMP   = 1u << 1,
    BUTTON_USE    = 1u << 2, // 'E' key
    BUTTON_CROUCH = 1u << 3,
    BUTTON_SPRINT = 1u << 4
};

// ============================================================================
// Endian-safe, bounds-checked Byte Stream
// ============================================================================

class ByteWriter {
public:
    explicit ByteWriter(std::vector<uint8_t>& buffer) : m_buf(buffer) {}

    void writeUint8(uint8_t val) {
        m_buf.push_back(val);
    }

    void writeUint16(uint16_t val) {
        m_buf.push_back(static_cast<uint8_t>(val & 0xFF));
        m_buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    }

    void writeUint32(uint32_t val) {
        m_buf.push_back(static_cast<uint8_t>(val & 0xFF));
        m_buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
        m_buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
        m_buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    }

    void writeUint64(uint64_t val) {
        for (int i = 0; i < 8; ++i) {
            m_buf.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
        }
    }

    void writeInt32(int32_t val) {
        writeUint32(static_cast<uint32_t>(val));
    }

    void writeFloat(float val) {
        static_assert(sizeof(float) == sizeof(uint32_t), "float size mismatch");
        if (!std::isfinite(val)) {
            val = 0.0f; // Sanitize NaN and Infinity to zero
        }
        uint32_t u;
        std::memcpy(&u, &val, sizeof(float));
        writeUint32(u);
    }

    void writeString(const std::string& str, size_t maxLen) {
        size_t len = std::min({ str.size(), maxLen, size_t(0xFFFF) });
        writeUint16(static_cast<uint16_t>(len));
        for (size_t i = 0; i < len; ++i) {
            m_buf.push_back(static_cast<uint8_t>(str[i]));
        }
    }

    void writeBytes(const uint8_t* data, size_t len) {
        m_buf.insert(m_buf.end(), data, data + len);
    }

private:
    std::vector<uint8_t>& m_buf;
};

class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size)
        : m_data(data), m_size(size), m_pos(0) {}

    size_t remaining() const {
        return m_pos < m_size ? m_size - m_pos : 0;
    }

    size_t position() const { return m_pos; }

    bool readUint8(uint8_t& out) {
        if (remaining() < 1) return false;
        out = m_data[m_pos++];
        return true;
    }

    bool readUint16(uint16_t& out) {
        if (remaining() < 2) return false;
        out = static_cast<uint16_t>(m_data[m_pos]) |
              (static_cast<uint16_t>(m_data[m_pos + 1]) << 8);
        m_pos += 2;
        return true;
    }

    bool readUint32(uint32_t& out) {
        if (remaining() < 4) return false;
        out = static_cast<uint32_t>(m_data[m_pos]) |
              (static_cast<uint32_t>(m_data[m_pos + 1]) << 8) |
              (static_cast<uint32_t>(m_data[m_pos + 2]) << 16) |
              (static_cast<uint32_t>(m_data[m_pos + 3]) << 24);
        m_pos += 4;
        return true;
    }

    bool readUint64(uint64_t& out) {
        if (remaining() < 8) return false;
        out = 0;
        for (int i = 0; i < 8; ++i) {
            out |= (static_cast<uint64_t>(m_data[m_pos + i]) << (i * 8));
        }
        m_pos += 8;
        return true;
    }

    bool readInt32(int32_t& out) {
        uint32_t u;
        if (!readUint32(u)) return false;
        out = static_cast<int32_t>(u);
        return true;
    }

    bool readFloat(float& out) {
        uint32_t u;
        if (!readUint32(u)) return false;
        std::memcpy(&out, &u, sizeof(float));
        if (!std::isfinite(out)) {
            out = 0.0f;
            return false; // Reject NaN and Infinity payloads
        }
        return true;
    }

    bool readString(std::string& out, size_t maxLen) {
        uint16_t len;
        if (!readUint16(len)) return false;
        if (len > maxLen || remaining() < len) return false;
        out.assign(reinterpret_cast<const char*>(m_data + m_pos), len);
        m_pos += len;
        return true;
    }

    bool readBytes(uint8_t* out, size_t len) {
        if (remaining() < len) return false;
        std::memcpy(out, m_data + m_pos, len);
        m_pos += len;
        return true;
    }

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_pos;
};

// ============================================================================
// Message Payload Structures
// ============================================================================

struct MsgHeader {
    static constexpr size_t SERIALIZED_SIZE = 11;

    uint32_t magic = NET_MAGIC;
    uint16_t version = NET_PROTOCOL_VERSION;
    uint8_t  msgType = MSG_NONE;
    uint32_t sequence = 0;

    void serialize(ByteWriter& w) const {
        w.writeUint32(magic);
        w.writeUint16(version);
        w.writeUint8(msgType);
        w.writeUint32(sequence);
    }

    bool deserialize(ByteReader& r) {
        if (!r.readUint32(magic)) return false;
        if (!r.readUint16(version)) return false;
        if (!r.readUint8(msgType)) return false;
        if (!r.readUint32(sequence)) return false;
        return true;
    }
};

struct MsgHello {
    uint16_t clientProtocolVersion = NET_PROTOCOL_VERSION;
    std::string clientBuildVersion;
    std::string joinToken;
    std::string participantId;
    std::string displayName;
    uint64_t    reconnectKey = 0; // Secret key for authorized session reconnection

    void serialize(ByteWriter& w) const {
        w.writeUint16(clientProtocolVersion);
        w.writeString(clientBuildVersion, MAX_STRING_LEN);
        w.writeString(joinToken, MAX_TOKEN_LEN);
        w.writeString(participantId, MAX_PARTICIPANT_ID_LEN);
        w.writeString(displayName, MAX_STRING_LEN);
        w.writeUint64(reconnectKey);
    }

    bool deserialize(ByteReader& r) {
        if (!r.readUint16(clientProtocolVersion)) return false;
        if (!r.readString(clientBuildVersion, MAX_STRING_LEN)) return false;
        if (!r.readString(joinToken, MAX_TOKEN_LEN)) return false;
        if (!r.readString(participantId, MAX_PARTICIPANT_ID_LEN)) return false;
        if (!r.readString(displayName, MAX_STRING_LEN)) return false;
        if (!r.readUint64(reconnectKey)) return false;
        return true;
    }
};

// Native identity has no pointers. Counts are u64, matching the native witness;
// name remains the existing <=32 byte field. This is not a content hash.
struct NativeMapStructure {
    uint8_t psmVersion=0;
    uint64_t countA=0,countB=0;
    bool operator==(const NativeMapStructure&) const = default;
    void serialize(ByteWriter& w) const { w.writeUint8(psmVersion);w.writeUint64(countA);w.writeUint64(countB); }
    bool deserialize(ByteReader& r) { return r.readUint8(psmVersion)&&r.readUint64(countA)&&r.readUint64(countB)&&countA&&countB; }
};
inline bool validNativeMapName(const std::string& name) {
    if(name.empty()||name.size()>MAX_STRING_LEN)return false;
    return std::all_of(name.begin(),name.end(),[](unsigned char c) { return c>=0x21&&c<=0x7e&&c!='"'&&c!='\\'; });
}
inline void writeNativeMapName(ByteWriter& w,const std::string& name) {
    if(!validNativeMapName(name))throw std::invalid_argument("v1.11 native map name");
    w.writeString(name,MAX_STRING_LEN);
}
struct MsgWelcome {
    uint16_t protocolVersion = NET_PROTOCOL_VERSION;
    uint32_t sessionId = 0;
    uint8_t  assignedSlot = 0; // 0..7
    uint32_t connectionGeneration = 1;
    uint32_t mapEpoch = 1;
    uint16_t serverTickRate = 60;
    uint16_t serverSnapshotRate = 30;
    std::string mapName;
    NativeMapStructure mapStructure;
    uint64_t reconnectKey = 0; // Assigned secret reconnect key

    bool valid() const {
        return protocolVersion == NET_PROTOCOL_VERSION && sessionId && assignedSlot > 0 && assignedSlot < MAX_PLAYERS &&
            connectionGeneration && mapEpoch && serverTickRate && serverSnapshotRate && reconnectKey &&
            validNativeMapName(mapName) && ((mapStructure.countA && mapStructure.countB) ||
                (mapName == "LOBBY" && mapStructure == NativeMapStructure{}));
    }

    void serialize(ByteWriter& w) const {
        if (!valid()) throw std::invalid_argument("v1.13 invalid welcome");
        w.writeUint16(protocolVersion);
        w.writeUint32(sessionId);
        w.writeUint8(assignedSlot);
        w.writeUint32(connectionGeneration);
        w.writeUint32(mapEpoch);
        w.writeUint16(serverTickRate);
        w.writeUint16(serverSnapshotRate);
        writeNativeMapName(w,mapName);
        // v1.13 explicit lobby stage: no fabricated native map identity.
        const bool native = mapStructure.countA && mapStructure.countB;
        if (!native && mapName != "LOBBY") throw std::invalid_argument("v1.13 missing native map identity");
        w.writeUint8(native ? 1 : 0);
        if (native) mapStructure.serialize(w);
        w.writeUint64(reconnectKey);
    }

    bool deserialize(ByteReader& r) {
        if (!r.readUint16(protocolVersion)) return false;
        if (!r.readUint32(sessionId)) return false;
        if (!r.readUint8(assignedSlot)) return false;
        if (!r.readUint32(connectionGeneration)) return false;
        if (!r.readUint32(mapEpoch)) return false;
        if (!r.readUint16(serverTickRate)) return false;
        if (!r.readUint16(serverSnapshotRate)) return false;
        if (!r.readString(mapName, MAX_STRING_LEN) || !validNativeMapName(mapName)) return false;
        uint8_t native{};
        if (!r.readUint8(native) || native > 1) return false;
        if (native) { if (!mapStructure.deserialize(r)) return false; }
        else { if (mapName != "LOBBY") return false; mapStructure = {}; }
        if (!r.readUint64(reconnectKey)) return false;
        return valid();
    }
};

struct MsgReject {
    uint8_t reasonCode = 0;
    std::string reasonMessage;

    void serialize(ByteWriter& w) const {
        w.writeUint8(reasonCode);
        w.writeString(reasonMessage, MAX_STRING_LEN);
    }

    bool deserialize(ByteReader& r) {
        if (!r.readUint8(reasonCode)) return false;
        if (!r.readString(reasonMessage, MAX_STRING_LEN)) return false;
        return true;
    }
};

struct MsgPlayerJoin {
    uint8_t slotId = 0;
    std::string participantId;
    std::string displayName;
    uint32_t connectionGeneration = 1;

    void serialize(ByteWriter& w) const {
        w.writeUint8(slotId);
        w.writeString(participantId, MAX_PARTICIPANT_ID_LEN);
        w.writeString(displayName, MAX_STRING_LEN);
        w.writeUint32(connectionGeneration);
    }

    bool deserialize(ByteReader& r) {
        if (!r.readUint8(slotId)) return false;
        if (!r.readString(participantId, MAX_PARTICIPANT_ID_LEN)) return false;
        if (!r.readString(displayName, MAX_STRING_LEN)) return false;
        if (!r.readUint32(connectionGeneration)) return false;
        return true;
    }
};

struct MsgPlayerLeave {
    uint8_t slotId = 0;
    std::string participantId;
    uint8_t reasonCode = 0;

    void serialize(ByteWriter& w) const {
        w.writeUint8(slotId);
        w.writeString(participantId, MAX_PARTICIPANT_ID_LEN);
        w.writeUint8(reasonCode);
    }

    bool deserialize(ByteReader& r) {
        if (!r.readUint8(slotId)) return false;
        if (!r.readString(participantId, MAX_PARTICIPANT_ID_LEN)) return false;
        if (!r.readUint8(reasonCode)) return false;
        return true;
    }
};

struct MsgInputCommand {
    uint8_t  slotId = 0;
    uint32_t connectionGeneration = 0;
    uint32_t commandSequence = 0;
    uint32_t clientTick = 0;
    float    pitch = 0.0f;
    float    yaw = 0.0f;
    float    moveForward = 0.0f;
    float    moveRight = 0.0f;
    uint32_t buttons = 0;
    uint8_t  selectedWeapon = 0;

    void serialize(ByteWriter& w) const {
        w.writeUint8(slotId);
        w.writeUint32(connectionGeneration);
        w.writeUint32(commandSequence);
        w.writeUint32(clientTick);
        w.writeFloat(pitch);
        w.writeFloat(yaw);
        w.writeFloat(moveForward);
        w.writeFloat(moveRight);
        w.writeUint32(buttons);
        w.writeUint8(selectedWeapon);
    }

    bool deserialize(ByteReader& r) {
        if (!r.readUint8(slotId)) return false;
        if (!r.readUint32(connectionGeneration)) return false;
        if (!r.readUint32(commandSequence)) return false;
        if (!r.readUint32(clientTick)) return false;
        if (!r.readFloat(pitch)) return false;
        if (!r.readFloat(yaw)) return false;
        if (!r.readFloat(moveForward)) return false;
        if (!r.readFloat(moveRight)) return false;
        if (!r.readUint32(buttons)) return false;
        if (!r.readUint8(selectedWeapon)) return false;
        return true;
    }
};

struct PlayerStateSnapshot {
    uint8_t  slotId = 0;
    uint32_t entityId = 0;
    uint32_t entityGeneration = 0;
    float    posX = 0.0f;
    float    posY = 0.0f;
    float    posZ = 0.0f;
    float    velX = 0.0f;
    float    velY = 0.0f;
    float    velZ = 0.0f;
    float    yaw = 0.0f;
    float    pitch = 0.0f;
    int32_t  health = DEFAULT_HEALTH;
    int32_t  armor = DEFAULT_ARMOR;
    uint8_t  activeWeapon = WEAPON_PISTOL;
    uint32_t weaponMask = DEFAULT_WEAPON_MASK;
    uint16_t ammo = DEFAULT_PISTOL_AMMO;
    uint8_t  lifeState = LIFE_ALIVE;
    uint32_t buttons = 0;
    uint32_t deathSequence = 0;
    // P7/v1.12 explicit scalar extension. Capture AFTER native world step.
    // `ammo` above is a compatibility mirror of ammoByWeapon[activeWeapon].
    std::array<uint16_t,8> ammoByWeapon{0,DEFAULT_PISTOL_AMMO,0,0,0,0,0,0};
    uint16_t keys = 0;
    uint32_t artifacts = 0, abilities = 0;
    uint16_t questItems = 0, ankhCount = 0, ankhMask = 0;
    uint32_t teamDolls = 0;
    int32_t pendingWeapon = -1;
    uint64_t stateTimeNs = 0;
    uint32_t resetToken = 0, discontinuitySequence = 0;

    bool valid112() const noexcept {
        if(slotId>=MAX_PLAYERS || !entityId || !entityGeneration || !stateTimeNs || !resetToken || !discontinuitySequence)return false;
        if(!std::isfinite(posX)||!std::isfinite(posY)||!std::isfinite(posZ)||!std::isfinite(velX)||!std::isfinite(velY)||!std::isfinite(velZ)||!std::isfinite(yaw)||!std::isfinite(pitch))return false;
        if(health < -32768 || health > 32767 || activeWeapon>=8 || (weaponMask&~0xffu) || !(weaponMask&(1u<<activeWeapon)))return false;
        if(pendingWeapon < -1 || pendingWeapon >= 8 || (pendingWeapon>=0 && !(weaponMask&(1u<<pendingWeapon))))return false;
        if(ammo!=ammoByWeapon[activeWeapon] || (keys&~0x0fu) || (artifacts&~0x3fu) || (abilities&~3u) || (questItems&~0xffu))return false;
        if(ankhCount>5 || (ankhMask&~0x1fu) || ankhCount!=std::popcount(ankhMask))return false;
        return (lifeState==LIFE_ALIVE && health>0) || (lifeState==LIFE_DEAD && health<=0 && deathSequence);
    }

    void serialize(ByteWriter& w) const {
        w.writeUint8(slotId);
        w.writeUint32(entityId);
        w.writeUint32(entityGeneration);
        w.writeFloat(posX);
        w.writeFloat(posY);
        w.writeFloat(posZ);
        w.writeFloat(velX);
        w.writeFloat(velY);
        w.writeFloat(velZ);
        w.writeFloat(yaw);
        w.writeFloat(pitch);
        w.writeInt32(health);
        w.writeInt32(armor);
        w.writeUint8(activeWeapon);
        w.writeUint32(weaponMask);
        w.writeUint16(ammo);
        w.writeUint8(lifeState);
        w.writeUint32(buttons);
        w.writeUint32(deathSequence);
        for(auto a:ammoByWeapon)w.writeUint16(a);
        w.writeUint16(keys);w.writeUint32(artifacts);w.writeUint32(abilities);
        w.writeUint16(questItems);w.writeUint16(ankhCount);w.writeUint16(ankhMask);
        w.writeUint32(teamDolls);w.writeInt32(pendingWeapon);w.writeUint64(stateTimeNs);
        w.writeUint32(resetToken);w.writeUint32(discontinuitySequence);
    }

    bool deserialize(ByteReader& r) {
        if (!r.readUint8(slotId)) return false;
        if (!r.readUint32(entityId)) return false;
        if (!r.readUint32(entityGeneration)) return false;
        if (!r.readFloat(posX)) return false;
        if (!r.readFloat(posY)) return false;
        if (!r.readFloat(posZ)) return false;
        if (!r.readFloat(velX)) return false;
        if (!r.readFloat(velY)) return false;
        if (!r.readFloat(velZ)) return false;
        if (!r.readFloat(yaw)) return false;
        if (!r.readFloat(pitch)) return false;
        if (!r.readInt32(health)) return false;
        if (!r.readInt32(armor)) return false;
        if (!r.readUint8(activeWeapon)) return false;
        if (!r.readUint32(weaponMask)) return false;
        if (!r.readUint16(ammo)) return false;
        if (!r.readUint8(lifeState)) return false;
        if (!r.readUint32(buttons)) return false;
        if (!r.readUint32(deathSequence)) return false;
        for(auto& a:ammoByWeapon)if(!r.readUint16(a))return false;
        if(!r.readUint16(keys)||!r.readUint32(artifacts)||!r.readUint32(abilities))return false;
        if(!r.readUint16(questItems)||!r.readUint16(ankhCount)||!r.readUint16(ankhMask))return false;
        if(!r.readUint32(teamDolls)||!r.readInt32(pendingWeapon)||!r.readUint64(stateTimeNs))return false;
        if(!r.readUint32(resetToken)||!r.readUint32(discontinuitySequence))return false;
        return valid112();
    }
};

struct MsgPlayerSnapshot {
    uint32_t snapshotSequence = 0;
    uint32_t serverTick = 0;
    uint32_t mapEpoch = 0;
    std::vector<PlayerStateSnapshot> players;

    void serialize(ByteWriter& w) const {
        w.writeUint32(snapshotSequence);
        w.writeUint32(serverTick);
        w.writeUint32(mapEpoch);
        w.writeUint8(static_cast<uint8_t>(players.size()));
        for (const auto& p : players) {
            p.serialize(w);
        }
    }

    bool deserialize(ByteReader& r) {
        if (!r.readUint32(snapshotSequence)) return false;
        if (!r.readUint32(serverTick)) return false;
        if (!r.readUint32(mapEpoch)) return false;
        uint8_t count = 0;
        if (!r.readUint8(count)) return false;
        if (count > MAX_PLAYERS) return false;
        players.clear();
        players.resize(count);
        for (uint8_t i = 0; i < count; ++i) {
            if (!players[i].deserialize(r)) return false;
        }
        return true;
    }
};

struct MsgWorldEvent {
    uint32_t eventSequence = 0;
    uint16_t eventType = 0;
    uint8_t  sourceSlot = 0;
    uint8_t  targetSlot = 0;
    int32_t  eventValue = 0;
    std::string eventPayload;

    void serialize(ByteWriter& w) const {
        w.writeUint32(eventSequence);
        w.writeUint16(eventType);
        w.writeUint8(sourceSlot);
        w.writeUint8(targetSlot);
        w.writeInt32(eventValue);
        w.writeString(eventPayload, MAX_STRING_LEN);
    }

    bool deserialize(ByteReader& r) {
        if (!r.readUint32(eventSequence)) return false;
        if (!r.readUint16(eventType)) return false;
        if (!r.readUint8(sourceSlot)) return false;
        if (!r.readUint8(targetSlot)) return false;
        if (!r.readInt32(eventValue)) return false;
        if (!r.readString(eventPayload, MAX_STRING_LEN)) return false;
        return true;
    }
};

struct MsgMapChange {
    uint32_t newMapEpoch = 0;
    std::string mapName;
    NativeMapStructure mapStructure;

    void serialize(ByteWriter& w) const {
        w.writeUint32(newMapEpoch);
        writeNativeMapName(w,mapName);
        mapStructure.serialize(w);
    }

    bool deserialize(ByteReader& r) {
        if (!r.readUint32(newMapEpoch)) return false;
        if (!r.readString(mapName, MAX_STRING_LEN) || !validNativeMapName(mapName) || !mapStructure.deserialize(r)) return false;
        return true;
    }
};

struct MsgMapReady {
    uint8_t  slotId = 0;
    uint32_t mapEpoch = 0;
    std::string mapName;
    NativeMapStructure mapStructure;

    void serialize(ByteWriter& w) const {
        w.writeUint8(slotId);
        w.writeUint32(mapEpoch);
        writeNativeMapName(w,mapName);
        mapStructure.serialize(w);
    }

    bool deserialize(ByteReader& r) {
        if (!r.readUint8(slotId)) return false;
        if (!r.readUint32(mapEpoch)) return false;
        if (!r.readString(mapName,MAX_STRING_LEN) || !validNativeMapName(mapName) || !mapStructure.deserialize(r)) return false;
        return true;
    }
};

struct MsgRespawnRequest {
    uint8_t  slotId = 0;
    uint32_t mapEpoch = 0;
    uint32_t deathSequence = 0;

    void serialize(ByteWriter& w) const {
        w.writeUint8(slotId);
        w.writeUint32(mapEpoch);
        w.writeUint32(deathSequence);
    }

    bool deserialize(ByteReader& r) {
        if (!r.readUint8(slotId)) return false;
        if (!r.readUint32(mapEpoch)) return false;
        if (!r.readUint32(deathSequence)) return false;
        return true;
    }
};

struct MsgRespawnConfirm {
    uint8_t  slotId = 0;
    uint32_t mapEpoch = 0;
    uint32_t deathSequence = 0;
    float    spawnX = 0.0f;
    float    spawnY = 0.0f;
    float    spawnZ = 0.0f;
    float    spawnYaw = 0.0f;
    uint32_t newEntityId = 0;
    uint32_t entityGeneration = 0;
    uint32_t defaultWeapons = DEFAULT_WEAPON_MASK;
    uint16_t defaultAmmo = DEFAULT_PISTOL_AMMO;

    void serialize(ByteWriter& w) const {
        w.writeUint8(slotId);
        w.writeUint32(mapEpoch);
        w.writeUint32(deathSequence);
        w.writeFloat(spawnX);
        w.writeFloat(spawnY);
        w.writeFloat(spawnZ);
        w.writeFloat(spawnYaw);
        w.writeUint32(newEntityId);
        w.writeUint32(entityGeneration);
        w.writeUint32(defaultWeapons);
        w.writeUint16(defaultAmmo);
    }

    bool deserialize(ByteReader& r) {
        if (!r.readUint8(slotId)) return false;
        if (!r.readUint32(mapEpoch)) return false;
        if (!r.readUint32(deathSequence)) return false;
        if (!r.readFloat(spawnX)) return false;
        if (!r.readFloat(spawnY)) return false;
        if (!r.readFloat(spawnZ)) return false;
        if (!r.readFloat(spawnYaw)) return false;
        if (!r.readUint32(newEntityId)) return false;
        if (!r.readUint32(entityGeneration)) return false;
        if (!r.readUint32(defaultWeapons)) return false;
        if (!r.readUint16(defaultAmmo)) return false;
        return true;
    }
};

struct MsgPing {
    uint64_t timestampMs = 0;
    uint32_t pingSequence = 0;

    void serialize(ByteWriter& w) const {
        w.writeUint64(timestampMs);
        w.writeUint32(pingSequence);
    }

    bool deserialize(ByteReader& r) {
        if (!r.readUint64(timestampMs)) return false;
        if (!r.readUint32(pingSequence)) return false;
        return true;
    }
};

struct MsgPong {
    uint64_t timestampMs = 0;
    uint32_t pingSequence = 0;

    void serialize(ByteWriter& w) const {
        w.writeUint64(timestampMs);
        w.writeUint32(pingSequence);
    }

    bool deserialize(ByteReader& r) {
        if (!r.readUint64(timestampMs)) return false;
        if (!r.readUint32(pingSequence)) return false;
        return true;
    }
};

// ============================================================================
// Packet Envelope Utilities
// ============================================================================

inline std::vector<uint8_t> encodePacket(NetMessageType type, uint32_t sequence, const auto& payload) {
    std::vector<uint8_t> buffer;
    buffer.reserve(128);
    ByteWriter writer(buffer);

    MsgHeader header;
    header.magic = NET_MAGIC;
    header.version = NET_PROTOCOL_VERSION;
    header.msgType = static_cast<uint8_t>(type);
    header.sequence = sequence;

    header.serialize(writer);
    payload.serialize(writer);
    return buffer;
}

inline bool decodeHeader(const uint8_t* data, size_t size, MsgHeader& header) {
    if (size < 11) return false; // 4 + 2 + 1 + 4
    ByteReader reader(data, size);
    if (!header.deserialize(reader)) return false;
    if (header.magic != NET_MAGIC) return false;
    return true;
}

} // namespace pse
