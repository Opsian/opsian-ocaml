
// Used on platforms where runtime_events isn't available, eg: OCaml < 5.0.0

bool has_runtime_events() {
    return false;
}
bool enable_runtime_events(const bool wasEnabled, const bool enable) {
    return false;
}
uint32_t read_runtime_events(MetricDataListener& listener) {
}
void disable_runtime_events() {
}
