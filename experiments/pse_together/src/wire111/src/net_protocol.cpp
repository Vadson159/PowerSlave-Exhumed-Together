#include "net_protocol.h"

namespace pse {

const char* getProtocolVersionString() {
    return "PowerSlave Exhumed Together ENet Protocol v1.11 (native map identity)";
}

const char* getWeaponTypeName(WeaponType type) {
    switch (type) {
        case WEAPON_MACHETE:      return "Machete";
        case WEAPON_PISTOL:       return "Pistol";
        case WEAPON_M60:          return "M60";
        case WEAPON_BOMBS:        return "Bombs";
        case WEAPON_FLAMETHROWER: return "Flamethrower";
        case WEAPON_COBRA:        return "Cobra Staff";
        case WEAPON_RING:         return "Ring of Ra";
        case WEAPON_MANACLES:     return "Manacles";
        default:                  return "Unknown Weapon";
    }
}

const char* getRejectReasonString(RejectReason reason) {
    switch (reason) {
        case REJECT_PROTOCOL_MISMATCH: return "Protocol Version Mismatch";
        case REJECT_SERVER_FULL:       return "Server Session is Full (Max 8 Players)";
        case REJECT_INVALID_TOKEN:     return "Invalid Session Token / Password";
        case REJECT_BANNED:            return "Banned from Session";
        case REJECT_RATE_LIMITED:      return "Rate Limited";
        case REJECT_MALFORMED:         return "Malformed Message Payload";
        case REJECT_ALREADY_CONNECTED: return "Participant Already Connected (Duplicate Active Session)";
        case REJECT_HIJACK_ATTEMPT:    return "Session Hijack Attempt / Invalid Reconnect Key";
        default:                       return "Unknown Rejection Reason";
    }
}

const char* getLeaveReasonString(LeaveReason reason) {
    switch (reason) {
        case LEAVE_USER_QUIT:       return "User Quit";
        case LEAVE_TIMEOUT:         return "Connection Timeout";
        case LEAVE_KICKED:          return "Kicked by Host";
        case LEAVE_PROTOCOL_ERROR:  return "Protocol Violation";
        case LEAVE_SERVER_SHUTDOWN: return "Server Shutdown";
        default:                    return "Unknown Reason";
    }
}

} // namespace pse
