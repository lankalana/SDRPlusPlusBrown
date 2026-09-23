#pragma once

enum {
    RECORDER_IFACE_CMD_GET_MODE,
    RECORDER_IFACE_CMD_SET_MODE,
    RECORDER_IFACE_CMD_START,
    RECORDER_IFACE_CMD_STOP,

    // in: const char* audio stream name. Ignored while recording.
    RECORDER_IFACE_CMD_SET_STREAM,
    // in: const char* output folder. May contain %ROOT%.
    RECORDER_IFACE_CMD_SET_PATH,
    // in: const char* filename template, same $-escapes the UI field accepts.
    RECORDER_IFACE_CMD_SET_NAME_TEMPLATE,
    // out: bool*
    RECORDER_IFACE_CMD_IS_RECORDING,
    // out: char* (buffer), in: int* (buffer size). Path of the file being written, empty if idle.
    RECORDER_IFACE_CMD_GET_FILENAME
};

enum {
    RECORDER_MODE_BASEBAND,
    RECORDER_MODE_AUDIO
};
