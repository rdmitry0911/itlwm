#ifndef IWM_COMMAND_SLOT_HPP
#define IWM_COMMAND_SLOT_HPP

/* Command slots retain completion until their synchronous owner consumes it.
 * A timed out command is not reusable until the real ACK or device reset. */
enum iwm_cmd_slot_state {
    IWM_CMD_SLOT_FREE = 0,
    IWM_CMD_SLOT_SUBMITTED,
    IWM_CMD_SLOT_COMPLETED,
    IWM_CMD_SLOT_TIMED_OUT,
    IWM_CMD_SLOT_ABORTED,
};

struct iwm_cmd_slot {
    uint64_t serial;
    uint32_t epoch;
    uint32_t code;
    uint8_t state;
    bool async;
};

#endif
