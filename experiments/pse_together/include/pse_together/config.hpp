// pse_together build configuration (S0.3). One vocabulary for the roles and feature flags that the former
// laboratory copies (M1.1, M12A, DOC55, DOC56, DOC58B) selected with ad-hoc macros. Compile-time only:
// nothing here emits code, so a target's machine code depends solely on the flags it is built with.
//
// Roles (a DLL target defines at most one; none = plain DOC-41 bootstrap with no network):
//   PSE_ROLE_HOST    host.cpp   - listen-server side: ENet session adapter, P2 input, snapshots, background simulation
//   PSE_ROLE_CLIENT  client.cpp - second game instance: sends input, receives snapshots (and, with the proxy flag, drives the proxy)
// Features:
//   PSE_FEATURE_SNAPSHOT      DOC-55 player-snapshot contract (host sends, client validates)
//   PSE_FEATURE_MAPREADY      DOC-56 native map identity + MapReady gate (requires SNAPSHOT)
//   PSE_FEATURE_VISUAL_PROXY  DOC-58B/58B.2 visual proxy of the remote player on the client (requires MAPREADY, client only)
#pragma once

#if defined(PSE_FEATURE_P8) && !defined(PSE_FEATURE_MAPREADY)
#error "PSE_FEATURE_P8 requires MAPREADY"
#endif

#if defined(PSE_ROLE_HOST) && defined(PSE_ROLE_CLIENT)
#error "pse_together: PSE_ROLE_HOST and PSE_ROLE_CLIENT are mutually exclusive"
#endif

// P14 host candidate: new AI/pickup detours, mode V review required.
#if defined(PSE_FEATURE_P14) && !defined(PSE_ROLE_HOST)
#error "PSE_FEATURE_P14 is host-only"
#endif
#if defined(PSE_FEATURE_MAPREADY) && !defined(PSE_FEATURE_SNAPSHOT)
#error "pse_together: PSE_FEATURE_MAPREADY requires PSE_FEATURE_SNAPSHOT"
#endif
#if defined(PSE_FEATURE_VISUAL_PROXY) && !defined(PSE_FEATURE_MAPREADY)
#error "pse_together: PSE_FEATURE_VISUAL_PROXY requires PSE_FEATURE_MAPREADY"
#endif
#if defined(PSE_FEATURE_VISUAL_PROXY) && !defined(PSE_ROLE_CLIENT)
#error "pse_together: PSE_FEATURE_VISUAL_PROXY is a client-only feature"
#endif
#if defined(PSE_FEATURE_SNAPSHOT) && !defined(PSE_ROLE_HOST) && !defined(PSE_ROLE_CLIENT)
#error "pse_together: PSE_FEATURE_SNAPSHOT needs a role (PSE_ROLE_HOST or PSE_ROLE_CLIENT)"
#endif

// Experimental P4 targets only, pending native runtime acceptance (DOC-65).
#if defined(PSE_FEATURE_P4) && !defined(PSE_FEATURE_MAPREADY)
#error "PSE_FEATURE_P4 requires MAPREADY"
#endif

// P3 rearm/reconnect candidate (DOC-71): isolated client fork (client_p3.cpp, native_proxy_p3.cpp); no P4/P8/P14.
#if defined(PSE_FEATURE_P3) && (!defined(PSE_ROLE_CLIENT) || !defined(PSE_FEATURE_VISUAL_PROXY) || defined(PSE_FEATURE_P4) || defined(PSE_FEATURE_P8) || defined(PSE_FEATURE_P14))
#error "PSE_FEATURE_P3 is an isolated client candidate: CLIENT + VISUAL_PROXY, without P4/P8/P14"
#endif
