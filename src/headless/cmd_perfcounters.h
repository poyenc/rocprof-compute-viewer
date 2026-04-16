// SPDX-License-Identifier: MIT
#pragma once
#include "headless_dispatcher.h"
#include "session_cache.h"

namespace Headless
{
int cmdPerfcounters(const HeadlessArgs& args, SessionCache& cache);
}
