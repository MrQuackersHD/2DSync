#include "conflict.h"

ConflictKind classify_conflict(int local_exists, int cloud_known, int checksums_equal) {
    if (!cloud_known) return CONFLICT_CLOUD_UNKNOWN;
    if (!local_exists) return CONFLICT_NO_LOCAL;
    if (checksums_equal) return CONFLICT_IN_SYNC;
    return CONFLICT_DIFFER;
}

const char *conflict_kind_name(ConflictKind kind) {
    switch (kind) {
        case CONFLICT_NO_LOCAL: return "no_local";
        case CONFLICT_NO_CLOUD: return "no_cloud";
        case CONFLICT_IN_SYNC: return "in_sync";
        case CONFLICT_DIFFER: return "differ";
        case CONFLICT_CLOUD_UNKNOWN: return "cloud_unknown";
        default: return "unknown";
    }
}
