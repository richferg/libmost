#ifndef FBLOCK_TOOL_H
#define FBLOCK_TOOL_H

// Tool FBlockID
#define FBLOCKID_TOOL 0x0E

// Tool Function IDs (generated from XML)
typedef enum {
    TOOL_FKT_FKTIDS                           = 0x000,  // FktIDs
    TOOL_FKT_NOTIFICATION                     = 0x001,  // Notification
    TOOL_FKT_NOTIFICATIONCHECK                = 0x002,  // NotificationCheck
    TOOL_FKT_VERSION                          = 0x010,  // Version
} Tool_FktID;

#endif // FBLOCK_TOOL_H
