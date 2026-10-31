#ifndef FBLOCK_MICROPHONEINPUT_H
#define FBLOCK_MICROPHONEINPUT_H

// MicrophoneInput FBlockID
#define FBLOCKID_MICROPHONEINPUT 0x26

// MicrophoneInput Function IDs (generated from XML)
typedef enum {
    MICROPHONEINPUT_FKT_FKTIDS                           = 0x000,  // FktIDs
    MICROPHONEINPUT_FKT_NOTIFICATION                     = 0x001,  // Notification
    MICROPHONEINPUT_FKT_NOTIFICATIONCHECK                = 0x002,  // NotificationCheck
    MICROPHONEINPUT_FKT_SOURCEINFO                       = 0x100,  // SourceInfo
    MICROPHONEINPUT_FKT_ALLOCATE                         = 0x101,  // Allocate
    MICROPHONEINPUT_FKT_DEALLOCATE                       = 0x102,  // DeAllocate
    MICROPHONEINPUT_FKT_SOURCEACTIVITY                   = 0x103,  // SourceActivity
    MICROPHONEINPUT_FKT_SOURCENAME                       = 0x104,  // SourceName
    MICROPHONEINPUT_FKT_SOURCECONNECT                    = 0x105,  // SourceConnect
    MICROPHONEINPUT_FKT_SOURCEDISCONNECT                 = 0x106,  // SourceDisConnect
    MICROPHONEINPUT_FKT_SOURCEROUTING                    = 0x107,  // SourceRouting
    MICROPHONEINPUT_FKT_SYNCDATAINFO                     = 0x116,  // SyncDataInfo
} MicrophoneInput_FktID;

#endif // FBLOCK_MICROPHONEINPUT_H
