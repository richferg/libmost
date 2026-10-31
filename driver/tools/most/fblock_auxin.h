#ifndef FBLOCK_AUXIN_H
#define FBLOCK_AUXIN_H

// AuxIn FBlockID
#define FBLOCKID_AUXIN 0x24

// AuxIn Function IDs (generated from XML)
typedef enum {
    AUXIN_FKT_MEDIAPOSITION                    = 0x204,  // MediaPosition
    AUXIN_FKT_AUXDEVICEINFO                    = 0x432,  // AuxDeviceInfo
    AUXIN_FKT_DEVICEBROWSINGCAPABILITIES       = 0x433,  // DeviceBrowsingCapabilities
    AUXIN_FKT_TRACKINFORMATION                 = 0x434,  // TrackInformation
    AUXIN_FKT_ASYNCCONTROLSWITCH               = 0x436,  // AsyncControlSwitch
    AUXIN_FKT_INPUTGAINOFFSET                  = 0x437,  // InputGainOffset
    AUXIN_FKT_AUXTIMEINFORMATION               = 0x438,  // AuxTimeInformation
    AUXIN_FKT_AUXTRACKPOSITION                 = 0x439,  // AuxTrackPosition
    AUXIN_FKT_STRINGPARAMETERMAXLENGTH         = 0x490,  // StringParameterMaxLength
    AUXIN_FKT_SELECTAUDIOLISTINFO              = 0x4A1,  // SelectAudioListInfo
    AUXIN_FKT_SELECTAUDIOLISTFILTER            = 0x4A2,  // SelectAudioListFilter
    AUXIN_FKT_CURRENTAUDIOLISTFILTER           = 0x4A3,  // CurrentAudioListFilter
    AUXIN_FKT_SELECTCURRENTAUDIOLISTINFO       = 0x4A4,  // SelectCurrentAudioListInfo
    AUXIN_FKT_CURRENTMEDIALISTCONTENTCHANGED   = 0x4A5,  // CurrentMediaListContentChanged
    AUXIN_FKT_SELECTCOVERART                   = 0x4A6,  // SelectCoverArt
    AUXIN_FKT_SELECTCURRENTCOVERART            = 0x4A7,  // SelectCurrentCoverArt
    AUXIN_FKT_COVERARTSETUP                    = 0x4A8,  // CoverArtSetup
    AUXIN_FKT_RETRIEVECOVERART                 = 0x4A9,  // RetrieveCoverArt
    AUXIN_FKT_SELECTAUXPLAYER                  = 0x4AA,  // SelectAuxPlayer
    AUXIN_FKT_AVAILABLEAUXPLAYER               = 0x4AB,  // AvailableAuxPlayer
    AUXIN_FKT_AUXPLAYERSTATUS                  = 0x4AC,  // AuxPlayerStatus
    AUXIN_FKT_DECKSTATUSSET                    = 0x4D0,  // DeckStatusSet
    AUXIN_FKT_MEDIAPOSITIONSET                 = 0x4D1,  // MediaPositionSet
    AUXIN_FKT_TITLEPOSITIONSET                 = 0x4D2,  // TitlePositionSet
    AUXIN_FKT_CHAPTERPOSITIONSET               = 0x4D3,  // ChapterPositionSet
    AUXIN_FKT_TRACKINFORMATIONSET              = 0x4D4,  // TrackInformationSet
    AUXIN_FKT_AUXTRACKPOSITIONSET              = 0x4D5,  // AuxTrackPositionSet
    AUXIN_FKT_RANDOMSET                        = 0x4D6,  // RandomSet
    AUXIN_FKT_SCANSET                          = 0x4D7,  // ScanSet
    AUXIN_FKT_REPEATSET                        = 0x4D8,  // RepeatSet
} AuxIn_FktID;

#endif // FBLOCK_AUXIN_H
