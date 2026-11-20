// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "session/vr_session.h"

namespace vrhost
{
// Recreate only the headset's body tracker after a stuck/invalid tracking session.
// A sleeping headset is not a failure. Limit attempts until valid data returns.
class BodyRecovery
{
public:
	enum class Action { none, suspend, resume };
	Action update(ns now, bool head_fresh, bool body_fresh)
	{
		if (resume_at) {
			if (now < resume_at) return Action::none;
			resume_at = 0;
			return Action::resume;
		}
		if (body_fresh) { invalid_since = 0; attempts = 0; return Action::none; }
		if (!head_fresh || !invalid_since) { invalid_since = now; return Action::none; }
		if (now - invalid_since < 3'000'000'000 || attempts >= 3 ||
		    (last_attempt && now - last_attempt < 10'000'000'000)) return Action::none;
		++attempts;
		last_attempt = now;
		resume_at = now + 500'000'000;
		return Action::suspend;
	}
private:
	ns invalid_since = 0, last_attempt = 0, resume_at = 0;
	unsigned attempts = 0;
};
}
