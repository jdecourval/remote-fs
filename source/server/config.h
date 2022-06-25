namespace remotefs::settings {
#ifdef REMOTEFS_DISABLE_METRICS
const constexpr auto DISABLE_METRICS = true;
#else
const constexpr auto DISABLE_METRICS = false;
#endif
}  // namespace remotefs::settings
