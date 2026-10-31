#ifndef FBLOCK_AUDIODISKPLAYER_H
#define FBLOCK_AUDIODISKPLAYER_H

// AudioDiskPlayer FBlockID
#define FBLOCKID_AUDIODISKPLAYER 0x31

// AudioDiskPlayer Function IDs (generated from XML)
typedef enum {
    AUDIODISKPLAYER_FKT_FKTIDS                           = 0x000,  // FktIDs
    AUDIODISKPLAYER_FKT_NOTIFICATION                     = 0x001,  // Notification
    AUDIODISKPLAYER_FKT_NOTIFICATIONCHECK                = 0x002,  // NotificationCheck
    AUDIODISKPLAYER_FKT_SOURCEINFO                       = 0x100,  // SourceInfo
    AUDIODISKPLAYER_FKT_ALLOCATE                         = 0x101,  // Allocate
    AUDIODISKPLAYER_FKT_DEALLOCATE                       = 0x102,  // DeAllocate
    AUDIODISKPLAYER_FKT_SOURCEACTIVITY                   = 0x103,  // SourceActivity
    AUDIODISKPLAYER_FKT_SOURCENAME                       = 0x104,  // SourceName
    AUDIODISKPLAYER_FKT_SOURCECONNECT                    = 0x105,  // SourceConnect
    AUDIODISKPLAYER_FKT_SOURCEDISCONNECT                 = 0x106,  // SourceDisConnect
    AUDIODISKPLAYER_FKT_SOURCEROUTING                    = 0x107,  // SourceRouting
    AUDIODISKPLAYER_FKT_SYNCDATAINFO                     = 0x116,  // SyncDataInfo
    AUDIODISKPLAYER_FKT_DECKSTATUS                       = 0x200,  // DeckStatus
    AUDIODISKPLAYER_FKT_TIMEPOSITION                     = 0x201,  // TimePosition
    AUDIODISKPLAYER_FKT_TRACKPOSITION                    = 0x202,  // TrackPosition
    AUDIODISKPLAYER_FKT_FRAMEPOSITION                    = 0x203,  // FramePosition
    AUDIODISKPLAYER_FKT_MAGAZINESTATUS                   = 0x410,  // MagazineStatus
    AUDIODISKPLAYER_FKT_ACTIVEMAGAZINE                   = 0x411,  // ActiveMagazine
    AUDIODISKPLAYER_FKT_ACTIVEDISK                       = 0x412,  // ActiveDisk
    AUDIODISKPLAYER_FKT_MEDIAINFO                        = 0x413,  // MediaInfo
    AUDIODISKPLAYER_FKT_NUMBEROFDISKS                    = 0x414,  // NumberOfDisks
    AUDIODISKPLAYER_FKT_AUDIODISKINFO                    = 0x420,  // AudioDiskInfo
    AUDIODISKPLAYER_FKT_LONGAUDIODISKINFO                = 0x422,  // LongAudioDiskInfo
    AUDIODISKPLAYER_FKT_DECKEVENT                        = 0x430,  // DeckEvent
    AUDIODISKPLAYER_FKT_MEDIAEVENT                       = 0x431,  // MediaEvent
    AUDIODISKPLAYER_FKT_RANDOM                           = 0x450,  // Random
    AUDIODISKPLAYER_FKT_SCAN                             = 0x451,  // Scan
    AUDIODISKPLAYER_FKT_REPEAT                           = 0x452,  // Repeat
    AUDIODISKPLAYER_FKT_NEXTTRACKTOPLAY                  = 0x453,  // NextTrackToPlay
    AUDIODISKPLAYER_FKT_DEEMPHASIS                       = 0x454,  // Deemphasis
} AudioDiskPlayer_FktID;

#endif // FBLOCK_AUDIODISKPLAYER_H
