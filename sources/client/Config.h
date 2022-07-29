namespace remotefs::settings {
#ifdef REMOTEFS_DISABLE_METRICS
const constexpr auto DISABLE_METRICS = true;
#else
const constexpr auto DISABLE_METRICS = false;
#endif
const constexpr auto MAX_MESSAGE_SIZE = 8192;
namespace client {
const constexpr auto FUSE_READ_BUFFER_SIZE = 2048ull;
}
}  // namespace remotefs::settings
