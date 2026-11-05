#pragma once

typedef enum ConflictKind {
    CONFLICT_NO_LOCAL = 0,
    CONFLICT_NO_CLOUD = 1,
    CONFLICT_IN_SYNC = 2,
    CONFLICT_DIFFER = 3,
    CONFLICT_CLOUD_UNKNOWN = 4
} ConflictKind;

/* Newest timestamp is intentionally ignored. Differing checksums are a conflict. */
ConflictKind classify_conflict(int local_exists, int cloud_known, int checksums_equal);

const char *conflict_kind_name(ConflictKind kind);
