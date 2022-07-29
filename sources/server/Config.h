#ifndef REMOTE_FS_CONFIG_H
#define REMOTE_FS_CONFIG_H

namespace remotefs::settings {
#ifdef REMOTEFS_DISABLE_METRICS
const constexpr auto DISABLE_METRICS = true;
#else
const constexpr auto DISABLE_METRICS = false;
#endif
const constexpr auto MAX_MESSAGE_SIZE = 8192;
}  // namespace remotefs::settings

#endif
