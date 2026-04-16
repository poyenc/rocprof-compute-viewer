// SPDX-License-Identifier: MIT
#pragma once
#include "headless_dispatcher.h"
#include "session_cache.h"

namespace Headless
{
int cmdLatency(const HeadlessArgs& args, SessionCache& cache);
}
