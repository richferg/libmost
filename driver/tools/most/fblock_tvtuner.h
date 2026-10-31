#ifndef FBLOCK_TVTUNER_H
#define FBLOCK_TVTUNER_H

// TVTuner FBlockID
#define FBLOCKID_TVTUNER 0x42

// TVTuner Function IDs (generated from XML)
typedef enum {
    TVTUNER_FKT_TUNEDPROGRAM                     = 0x200,  // TunedProgram
    TVTUNER_FKT_TUNETO                           = 0x201,  // TuneTo
    TVTUNER_FKT_TUNEUP                           = 0x202,  // TuneUp
    TVTUNER_FKT_TUNEDOWN                         = 0x203,  // TuneDown
    TVTUNER_FKT_STOREPROGRAM                     = 0x204,  // StoreProgram
    TVTUNER_FKT_AUTOSTORE                        = 0x205,  // AutoStore
    TVTUNER_FKT_PROGRAMLIST                      = 0x206,  // ProgramList
    TVTUNER_FKT_BROWSERLIST                      = 0x207,  // BrowserList
    TVTUNER_FKT_BROWSERCOUNTRY                   = 0x208,  // BrowserCountry
    TVTUNER_FKT_ALTERNATIVEFREQUENCIES           = 0x209,  // AlternativeFrequencies
    TVTUNER_FKT_SOURCE                           = 0x20A,  // Source
    TVTUNER_FKT_SWITCHSOURCE                     = 0x20B,  // SwitchSource
    TVTUNER_FKT_COUNTRYCODES                     = 0x20C,  // CountryCodes
    TVTUNER_FKT_TVREGMODE                        = 0x400,  // TVRegMode
    TVTUNER_FKT_TVFIXMODE                        = 0x401,  // TVFixMode
    TVTUNER_FKT_FREQUENCYLIST                    = 0x402,  // FrequencyList
    TVTUNER_FKT_GETVTPAGE                        = 0x403,  // GetVTPage
    TVTUNER_FKT_REMOVEPAGENOTIFICATION           = 0x404,  // RemovePageNotification
    TVTUNER_FKT_GETTOPKEYS                       = 0x405,  // GetTopKeys
    TVTUNER_FKT_STOREFREQUENCY                   = 0x406,  // StoreFrequency
    TVTUNER_FKT_AUTOSTOREFREQUENCY               = 0x407,  // AutoStoreFrequency
    TVTUNER_FKT_CHANNELLIST                      = 0x408,  // ChannelList
    TVTUNER_FKT_AVINFO                           = 0x409,  // AVInfo
    TVTUNER_FKT_COLOR                            = 0x40A,  // Color
    TVTUNER_FKT_CONTRAST                         = 0x40B,  // Contrast
    TVTUNER_FKT_BRIGHTNESS                       = 0x40C,  // Brightness
    TVTUNER_FKT_TINT                             = 0x40D,  // Tint
} TVTuner_FktID;

#endif // FBLOCK_TVTUNER_H
