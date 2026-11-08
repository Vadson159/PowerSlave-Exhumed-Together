#pragma once
#include "self_native_p7.hpp"
namespace pse::self_native_p7 {
bool quiesceForRespawn() noexcept;
bool rearmForRespawn(const Binding& binding,std::uint32_t deathSequence) noexcept;
}
