#include "log/log.h"

#ifndef NXCAST_EXPECTED_LOG_LEVEL
#error NXCAST_EXPECTED_LOG_LEVEL must be defined by the test target
#endif

_Static_assert(NXCAST_LOG_LEVEL_DEFAULT == NXCAST_EXPECTED_LOG_LEVEL,
               "unexpected default log level");

int nxcast_log_policy_compile_test(void)
{
    return 0;
}
