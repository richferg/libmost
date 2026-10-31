#ifndef FBLOCK_DVDVIDEOPLAYER_H
#define FBLOCK_DVDVIDEOPLAYER_H

// DVDVideoPlayer FBlockID
#define FBLOCKID_DVDVIDEOPLAYER 0x34

// DVDVideoPlayer Function IDs (generated from XML)
typedef enum {
    DVDVIDEOPLAYER_FKT_FKTIDS                           = 0x000,  // FktIDs
    DVDVIDEOPLAYER_FKT_NOTIFICATION                     = 0x001,  // Notification
    DVDVIDEOPLAYER_FKT_NOTIFICATIONCHECK                = 0x002,  // NotificationCheck
    DVDVIDEOPLAYER_FKT_SOURCEINFO                       = 0x100,  // SourceInfo
    DVDVIDEOPLAYER_FKT_ALLOCATE                         = 0x101,  // Allocate
    DVDVIDEOPLAYER_FKT_DEALLOCATE                       = 0x102,  // DeAllocate
    DVDVIDEOPLAYER_FKT_SOURCEACTIVITY                   = 0x103,  // SourceActivity
    DVDVIDEOPLAYER_FKT_SOURCENAME                       = 0x104,  // SourceName
    DVDVIDEOPLAYER_FKT_SOURCECONNECT                    = 0x105,  // SourceConnect
    DVDVIDEOPLAYER_FKT_SOURCEDISCONNECT                 = 0x106,  // SourceDisConnect
    DVDVIDEOPLAYER_FKT_SOURCEROUTING                    = 0x107,  // SourceRouting
    DVDVIDEOPLAYER_FKT_SYNCDATAINFO                     = 0x116,  // SyncDataInfo
    DVDVIDEOPLAYER_FKT_SCREENFORMAT                     = 0x130,  // ScreenFormat
    DVDVIDEOPLAYER_FKT_VIDEOFREQUENCY                   = 0x131,  // VideoFrequency
    DVDVIDEOPLAYER_FKT_VIDEONORM                        = 0x132,  // VideoNorm
    DVDVIDEOPLAYER_FKT_VIDEOSIGNALFORMAT                = 0x133,  // VideoSignalFormat
    DVDVIDEOPLAYER_FKT_VIDEOFORMAT                      = 0x135,  // VideoFormat
    DVDVIDEOPLAYER_FKT_AUDIOOUTFORMAT                   = 0x136,  // AudioOutFormat
    DVDVIDEOPLAYER_FKT_DECKSTATUS                       = 0x200,  // DeckStatus
    DVDVIDEOPLAYER_FKT_TIMEPOSITION                     = 0x201,  // TimePosition
    DVDVIDEOPLAYER_FKT_FRAMEPOSITION                    = 0x203,  // FramePosition
    DVDVIDEOPLAYER_FKT_TITLEPOSITION                    = 0x205,  // TitlePosition
    DVDVIDEOPLAYER_FKT_CHAPTERPOSITION                  = 0x206,  // ChapterPosition
    DVDVIDEOPLAYER_FKT_VIDEOINTERACTION                 = 0x251,  // VideoInteraction
    DVDVIDEOPLAYER_FKT_PLAYERREGION                     = 0x270,  // PlayerRegion
    DVDVIDEOPLAYER_FKT_MAGAZINESTATUS                   = 0x410,  // MagazineStatus
    DVDVIDEOPLAYER_FKT_ACTIVEMAGAZINE                   = 0x411,  // ActiveMagazine
    DVDVIDEOPLAYER_FKT_ACTIVEDISK                       = 0x412,  // ActiveDisk
    DVDVIDEOPLAYER_FKT_MEDIAINFO                        = 0x413,  // MediaInfo
    DVDVIDEOPLAYER_FKT_COPYMANAGEMENTINFO               = 0x423,  // CopyManagementInfo
    DVDVIDEOPLAYER_FKT_DECKEVENT                        = 0x430,  // DeckEvent
    DVDVIDEOPLAYER_FKT_MEDIAEVENT                       = 0x431,  // MediaEvent
    DVDVIDEOPLAYER_FKT_RANDOM                           = 0x450,  // Random
    DVDVIDEOPLAYER_FKT_SCAN                             = 0x451,  // Scan
    DVDVIDEOPLAYER_FKT_REPEAT                           = 0x452,  // Repeat
    DVDVIDEOPLAYER_FKT_SLOWFWSPEED                      = 0x455,  // SlowFwSpeed
    DVDVIDEOPLAYER_FKT_SLOWBWSPEED                      = 0x456,  // SlowBwSpeed
    DVDVIDEOPLAYER_FKT_FASTFWSPEED                      = 0x457,  // FastFwSpeed
    DVDVIDEOPLAYER_FKT_FASTBWSPEED                      = 0x458,  // FastBwSpeed
    DVDVIDEOPLAYER_FKT_ABREPEATSETTING                  = 0x460,  // ABRepeatSetting
    DVDVIDEOPLAYER_FKT_AVAILABLETITLES                  = 0x461,  // AvailableTitles
    DVDVIDEOPLAYER_FKT_AVAILABLECHAPTERS                = 0x462,  // AvailableChapters
    DVDVIDEOPLAYER_FKT_AVAILABLEANGLES                  = 0x463,  // AvailableAngles
    DVDVIDEOPLAYER_FKT_AUDIOSTREAMNUMBER                = 0x500,  // AudioStreamNumber
    DVDVIDEOPLAYER_FKT_AVAILABLEAUDIOLANGUAGES          = 0x501,  // AvailableAudioLanguages
    DVDVIDEOPLAYER_FKT_SUBTITLESTREAMNUMBER             = 0x502,  // SubtitleStreamNumber
    DVDVIDEOPLAYER_FKT_AVAILABLESUBTITLESLANGUAGES      = 0x503,  // AvailableSubtitlesLanguages
    DVDVIDEOPLAYER_FKT_MENUELANGUAGE                    = 0x504,  // MenueLanguage
    DVDVIDEOPLAYER_FKT_AVAILABLEMENUELANGUAGES          = 0x505,  // AvailableMenueLanguages
    DVDVIDEOPLAYER_FKT_VIDEOPARENTALLEVEL               = 0x509,  // VideoParentalLevel
    DVDVIDEOPLAYER_FKT_TEMPORARYPARENTALLEVEL           = 0x50A,  // TemporaryParentalLevel
    DVDVIDEOPLAYER_FKT_VIDEOPARENTALPWD                 = 0x50B,  // VideoParentalPwd
    DVDVIDEOPLAYER_FKT_VIDEODEFAULTSETTINGS             = 0x530,  // VideoDefaultSettings
    DVDVIDEOPLAYER_FKT_VIDEOANGLE                       = 0x531,  // VideoAngle
    DVDVIDEOPLAYER_FKT_COUNTRYCODE                      = 0x584,  // CountryCode
    DVDVIDEOPLAYER_FKT_KARAOKEPLAYER                    = 0x590,  // KaraokePlayer
    DVDVIDEOPLAYER_FKT_KARAOKE                          = 0x591,  // Karaoke
    DVDVIDEOPLAYER_FKT_AVAILABLEKARAOKESTREAMS          = 0x592,  // AvailableKaraokeStreams
    DVDVIDEOPLAYER_FKT_KARAOKESTREAM                    = 0x593,  // KaraokeStream
    DVDVIDEOPLAYER_FKT_KARAOKECHANNELMIX                = 0x594,  // KaraokeChannelMix
    DVDVIDEOPLAYER_FKT_KARAOKEMODERESERVATION           = 0x595,  // KaraokeModeReservation
    DVDVIDEOPLAYER_FKT_KARAOKEMODE                      = 0x596,  // KaraokeMode
    DVDVIDEOPLAYER_FKT_KARAOKEKEYCONTROL                = 0x597,  // KaraokeKeyControl
    DVDVIDEOPLAYER_FKT_KARAOKEECHO                      = 0x598,  // KaraokeEcho
    DVDVIDEOPLAYER_FKT_SUBPICTUREPALETTE                = 0x610,  // SubpicturePalette
    DVDVIDEOPLAYER_FKT_TIMEINFORMATION                  = 0x613,  // TimeInformation
    DVDVIDEOPLAYER_FKT_AUDIOSTREAMPROPERTIES            = 0x614,  // AudioStreamProperties
    DVDVIDEOPLAYER_FKT_APPLICATIONINFORMATION           = 0x615,  // ApplicationInformation
    DVDVIDEOPLAYER_FKT_MULTICHANNEL                     = 0x616,  // MultiChannel
    DVDVIDEOPLAYER_FKT_VIDEOSTREAMPROPERTIES            = 0x617,  // VideoStreamProperties
    DVDVIDEOPLAYER_FKT_SUBPIXSTREAMPROPERTIES           = 0x618,  // SubpixStreamProperties
    DVDVIDEOPLAYER_FKT_SHUFFLE                          = 0x620,  // Shuffle
} DVDVideoPlayer_FktID;

#endif // FBLOCK_DVDVIDEOPLAYER_H
