#ifndef FBLOCK_NETBLOCK_H
#define FBLOCK_NETBLOCK_H

// NetBlock FBlockID
#define FBLOCKID_NETBLOCK 0x01

// NetBlock Function IDs (generated from XML)
typedef enum {
    NETBLOCK_FKT_FBLOCKIDS                        = 0x000,  // FBlockIDs
    NETBLOCK_FKT_DEVICEINFO                       = 0x001,  // DeviceInfo
    NETBLOCK_FKT_NODEPOSITIONADDRESS              = 0x002,  // NodePositionAddress
    NETBLOCK_FKT_NODEADDRESS                      = 0x003,  // NodeAddress
    NETBLOCK_FKT_GROUPADDRESS                     = 0x004,  // GroupAddress
    NETBLOCK_FKT_PERMISSIONTOWAKE                 = 0x005,  // PermissionToWake
    NETBLOCK_FKT_SHUTDOWN                         = 0x006,  // ShutDown
    NETBLOCK_FKT_RETRYPARAMETERS                  = 0x007,  // RetryParameters
    NETBLOCK_FKT_SAMPLING_FREQUENCY               = 0x008,  // Sampling Frequency
    NETBLOCK_FKT_SOURCEHANDLES                    = 0x100,  // SourceHandles
    NETBLOCK_FKT_NOTIFICATION                     = 0x401,  // Notification
    NETBLOCK_FKT_NOTIFICATIONCHECK                = 0x402,  // NotificationCheck
    NETBLOCK_FKT_BOUNDARY                         = 0xA03,  // Boundary
} NetBlock_FktID;

#endif // FBLOCK_NETBLOCK_H
