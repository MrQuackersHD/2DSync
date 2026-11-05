#include "net_policy.h"

int cloud_op_allowed(int ac_status, int has_server, int has_token) {
    if (ac_status != 3) return NET_OFFLINE;
    if (!has_server) return NET_NO_SERVER;
    if (!has_token) return NET_NOT_PAIRED;
    return NET_OK;
}

const char *net_policy_message(int code) {
    switch (code) {
        case NET_OK: return "Ready";
        case NET_OFFLINE: return "Internet connection required";
        case NET_NO_SERVER: return "Set a server address in Settings";
        case NET_NOT_PAIRED: return "Pair this console first";
        default: return "Network unavailable";
    }
}
