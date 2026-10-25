// Exact previous-prefix reuse. The existing Prefill output arena is the state.
// IDs alone are insufficient: all images use identical placeholder IDs.
#pragma once
inline bool PrefixPrefixMatches(size_t cached_count, size_t requested_count,
    size_t old_sequence, const int64_t* old_ids, const float* old_hidden,
    const float* old_cos, const float* old_sin,
    size_t new_sequence, const int64_t* new_ids, const float* new_hidden,
    const float* new_cos, const float* new_sin) {
  return cached_count >= 16 && cached_count % 16 == 0 && cached_count <= requested_count &&
    cached_count <= old_sequence && cached_count <= new_sequence &&
    std::memcmp(old_ids, new_ids, cached_count * sizeof(int64_t)) == 0 &&
    std::memcmp(old_hidden, new_hidden, cached_count * 1024 * sizeof(float)) == 0 &&
    std::memcmp(old_cos, new_cos, cached_count * 64 * sizeof(float)) == 0 &&
    std::memcmp(old_sin, new_sin, cached_count * 64 * sizeof(float)) == 0;
}
