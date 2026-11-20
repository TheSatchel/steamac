// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "session/vr_session.h"
#include <cstdint>
#include <vector>
namespace vrhost {
// OSC ids: 0 head alignment, 1 hips, 2 chest, 3/4 feet, 5/6 knees, 7/8 elbows.
std::vector<uint8_t> body_osc_bundle(const BodyTracking &body);
class BodyOsc {
public:
 explicit BodyOsc(uint16_t port = 0);
 ~BodyOsc();
 BodyOsc(const BodyOsc &) = delete;
 bool send(const BodyTracking &body);
 uint64_t sent() const { return packets; }
private:
 int fd = -1;
 uint16_t port = 0;
 ns last_time = 0;
 uint64_t packets = 0;
};
}
