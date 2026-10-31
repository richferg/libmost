#ifndef FBLOCK_AUDIOTAPEPLAYER_H
#define FBLOCK_AUDIOTAPEPLAYER_H

// AudioTapePlayer FBlockID
#define FBLOCKID_AUDIOTAPEPLAYER 0x30

// AudioTapePlayer Function IDs (generated from XML)
typedef enum {
    AUDIOTAPEPLAYER_FKT_FKTIDS                           = 0x000,  // FktIDs
    AUDIOTAPEPLAYER_FKT_NOTIFICATION                     = 0x001,  // Notification
    AUDIOTAPEPLAYER_FKT_NOTIFICATIONCHECK                = 0x002,  // NotificationCheck
    AUDIOTAPEPLAYER_FKT_SOURCEINFO                       = 0x100,  // SourceInfo
    AUDIOTAPEPLAYER_FKT_ALLOCATE                         = 0x101,  // Allocate
    AUDIOTAPEPLAYER_FKT_DEALLOCATE                       = 0x102,  // DeAllocate
    AUDIOTAPEPLAYER_FKT_SOURCEACTIVITY                   = 0x103,  // SourceActivity
    AUDIOTAPEPLAYER_FKT_SOURCENAME                       = 0x104,  // SourceName
    AUDIOTAPEPLAYER_FKT_SOURCECONNECT                    = 0x105,  // SourceConnect
    AUDIOTAPEPLAYER_FKT_SOURCEDISCONNECT                 = 0x106,  // SourceDisConnect
    AUDIOTAPEPLAYER_FKT_SOURCEROUTING                    = 0x107,  // SourceRouting
    AUDIOTAPEPLAYER_FKT_SYNCDATAINFO                     = 0x116,  // SyncDataInfo
    AUDIOTAPEPLAYER_FKT_DECKSTATUS                       = 0x200,  // DeckStatus
    AUDIOTAPEPLAYER_FKT_TRACKPOSITION                    = 0x202,  // TrackPosition
    AUDIOTAPEPLAYER_FKT_DECKEVENT                        = 0x430,  // DeckEvent
    AUDIOTAPEPLAYER_FKT_MEDIAEVENT                       = 0x431,  // MediaEvent
    AUDIOTAPEPLAYER_FKT_BOOKTAPESTATUS                   = 0x440,  // BookTapeStatus
    AUDIOTAPEPLAYER_FKT_SKIPBLANK                        = 0x441,  // SkipBlank
    AUDIOTAPEPLAYER_FKT_DOLBY                            = 0x442,  // Dolby
    AUDIOTAPEPLAYER_FKT_TAPETYPE                         = 0x443,  // TapeType
    AUDIOTAPEPLAYER_FKT_TITLESTEP                        = 0x444,  // TitleStep
} AudioTapePlayer_FktID;

#endif // FBLOCK_AUDIOTAPEPLAYER_H
