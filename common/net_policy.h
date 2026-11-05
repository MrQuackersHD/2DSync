#pragma once

enum {
    NET_OK = 0,
    NET_OFFLINE = 1,
    NET_NO_SERVER = 2,
    NET_NOT_PAIRED = 3
};

/* ac_status: libctru ACU_GetStatus, 3 means connected. */
int cloud_op_allowed(int ac_status, int has_server, int has_token);
const char *net_policy_message(int code);
