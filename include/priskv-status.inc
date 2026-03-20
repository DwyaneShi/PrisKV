/* Arguments: NAME, CODE, MESSAGE, IS_RESP_STATUS */
PRISKV_DECLARE_STATUS(OK, 0x00, "OK", 1)

/* unrecognized command, typically protocol error */
PRISKV_DECLARE_STATUS(INVALID_COMMAND, 0x100, "Invalid command", 1)

/* zero length key in use? Note that '\0' is allowed */
PRISKV_DECLARE_STATUS(KEY_EMPTY, 0x101, "Empty key", 1)

/* does the length of key exceed @max_key_length? */
PRISKV_DECLARE_STATUS(KEY_TOO_BIG, 0x102, "Key too big", 1)

/* zero length value in use? Note that '\0' is allowed */
PRISKV_DECLARE_STATUS(VALUE_EMPTY, 0x103, "Empty Value", 1)

/* the length of @priskv_sgl is smaller than the length of value */
PRISKV_DECLARE_STATUS(VALUE_TOO_BIG, 0x104, "Value too big", 1)

/* no such command */
PRISKV_DECLARE_STATUS(NO_SUCH_COMMAND, 0x105, "No such command", 1)
    
/* no such key */
PRISKV_DECLARE_STATUS(NO_SUCH_KEY, 0x106, "No such key", 1)
    
/* no such token */
PRISKV_DECLARE_STATUS(NO_SUCH_TOKEN, 0x107, "No such token", 1)

/* unpin not closed */
PRISKV_DECLARE_STATUS(UNPIN_NOT_CLOSED, 0x108, "Unpin operation not closed", 1)

/* invalid SGL. the number of SGL within a command must not exceed @max_sgl */
PRISKV_DECLARE_STATUS(INVALID_SGL, 0x109, "Invalid SGL", 1)

/* invalid regex */
PRISKV_DECLARE_STATUS(INVALID_REGEX, 0x10A, "Invalid regex", 1)

/* key is updating */
PRISKV_DECLARE_STATUS(KEY_UPDATING, 0x10B, "Key is updating", 1)

/* connect to server side failed */
PRISKV_DECLARE_STATUS(CONNECT_ERROR, 0x10C, "Connect error", 1)

/* generic server side failure */
PRISKV_DECLARE_STATUS(SERVER_ERROR, 0x10D, "Server internal error", 1)

/* operation not permitted (e.g., SEAL/RELEASE/DROP with wrong token) */
PRISKV_DECLARE_STATUS(PERMISSION_DENIED, 0x10E, "Permission denied", 1)

/* no enough memory reported by server side */
PRISKV_DECLARE_STATUS(NO_MEM, 0x200, "No memory", 1)

/* Connection disconnected from the server side */
PRISKV_DECLARE_STATUS(DISCONNECTED, 0xF00, "Disconnected", 0)

/* local transport error occurs */
PRISKV_DECLARE_STATUS(TRANSPORT_ERROR, 0xF01, "Transport error", 0)

/* does inflight requests exceed @max_inflight_command? */
PRISKV_DECLARE_STATUS(BUSY, 0xF02, "Busy", 0)

/* unexpected protocol error */
PRISKV_DECLARE_STATUS(PROTOCOL_ERROR, 0xF03, "Protocol error", 0)
