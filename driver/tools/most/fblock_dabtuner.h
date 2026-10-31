#ifndef FBLOCK_DABTUNER_H
#define FBLOCK_DABTUNER_H

// DABTuner FBlockID
#define FBLOCKID_DABTUNER 0x43

// DABTuner Function IDs (generated from XML)
typedef enum {
    DABTUNER_FKT_CURRENTENSEMBLE                  = 0x210,  // CurrentEnsemble
    DABTUNER_FKT_CURRENTAUDIOSERVICE              = 0x211,  // CurrentAudioService
    DABTUNER_FKT_CURRENTAUDIOCOMPONENT            = 0x212,  // CurrentAudioComponent
    DABTUNER_FKT_CURRENTTUNERSTATUS               = 0x213,  // CurrentTunerStatus
    DABTUNER_FKT_LEARN                            = 0x220,  // Learn
    DABTUNER_FKT_SEEK                             = 0x221,  // Seek
    DABTUNER_FKT_TUNE                             = 0x222,  // Tune
    DABTUNER_FKT_SELECTAUDIOSERVICE               = 0x223,  // SelectAudioService
    DABTUNER_FKT_SELECTAUDIOCOMPONENT             = 0x224,  // SelectAudioComponent
    DABTUNER_FKT_ANNOUNCEMENTFILTER               = 0x400,  // AnnouncementFilter
    DABTUNER_FKT_ANNOUNCEMENTPRIORITY             = 0x401,  // AnnouncementPriority
    DABTUNER_FKT_ANNOUNCEMENTSTATUS               = 0x402,  // AnnouncementStatus
    DABTUNER_FKT_ANNOUNCEMENTSWITCH               = 0x403,  // AnnouncementSwitch
    DABTUNER_FKT_DRCSWITCH                        = 0x410,  // DRCSwitch
    DABTUNER_FKT_CURRENTRADIOTEXT                 = 0x420,  // CurrentRadiotext
    DABTUNER_FKT_FREQUENCYTABLE                   = 0x430,  // FrequencyTable
    DABTUNER_FKT_GETSINKHANDLE                    = 0x440,  // GetSinkHandle
    DABTUNER_FKT_RELEASESINKHANDLE                = 0x441,  // ReleaseSinkHandle
    DABTUNER_FKT_SERVICEFOLLOWINGMODE             = 0x450,  // ServiceFollowingMode
    DABTUNER_FKT_SETPTYFILTER                     = 0x451,  // SetPTYFilter
    DABTUNER_FKT_PRESETSAVE                       = 0x460,  // PresetSave
    DABTUNER_FKT_PRESETSELECT                     = 0x461,  // PresetSelect
    DABTUNER_FKT_AUDIOPRESETS                     = 0x462,  // AudioPresets
    DABTUNER_FKT_LEARNSTATUS                      = 0x470,  // LearnStatus
    DABTUNER_FKT_SEEKSTATUS                       = 0x471,  // SeekStatus
    DABTUNER_FKT_TUNESTATUS                       = 0x472,  // TuneStatus
    DABTUNER_FKT_SELECTAUDIOSERVICESTATUS         = 0x473,  // SelectAudioServiceStatus
    DABTUNER_FKT_SELECTAUDIOCOMPONENTSTATUS       = 0x474,  // SelectAudioComponentStatus
    DABTUNER_FKT_ENSEMBLELIST                     = 0x500,  // EnsembleList
    DABTUNER_FKT_AUDIOSERVICELIST                 = 0x510,  // AudioServiceList
    DABTUNER_FKT_AUDIOCOMPONENTLIST               = 0x520,  // AudioComponentList
    DABTUNER_FKT_CURRENTDATASERVICE               = 0x711,  // CurrentDataService
    DABTUNER_FKT_CURRENTDATACOMPONENT             = 0x712,  // CurrentDataComponent
    DABTUNER_FKT_CURRENTDATAUSERAPP               = 0x713,  // CurrentDataUserApp
    DABTUNER_FKT_SELECTDATASERVICE                = 0x723,  // SelectDataService
    DABTUNER_FKT_SELECTDATACOMPONENT              = 0x724,  // SelectDataComponent
    DABTUNER_FKT_SELECTUSERAPP                    = 0x725,  // SelectUserApp
    DABTUNER_FKT_DATASERVICELIST                  = 0x730,  // DataServiceList
    DABTUNER_FKT_DATACOMPONENTLIST                = 0x740,  // DataComponentList
    DABTUNER_FKT_DATAUSERAPPLIST                  = 0x750,  // DataUserAppList
    DABTUNER_FKT_DATADECODERCAP                   = 0x760,  // DataDecoderCap
    DABTUNER_FKT_SELECTDATASERVICESTATUS          = 0x773,  // SelectDataServiceStatus
    DABTUNER_FKT_SELECTDATACOMPONENTSTATUS        = 0x774,  // SelectDataComponentStatus
    DABTUNER_FKT_SELECTUSERAPPSTATUS              = 0x775,  // SelectUserAppStatus
    DABTUNER_FKT_CREATEDATACHANNEL                = 0x800,  // CreateDataChannel
    DABTUNER_FKT_REMOVEDATACHANNEL                = 0x801,  // RemoveDataChannel
    DABTUNER_FKT_DATACHANNELLIST                  = 0x805,  // DataChannelList
    DABTUNER_FKT_MOTHERDATACHANNEL                = 0x810,  // MotherDataChannel
} DABTuner_FktID;

#endif // FBLOCK_DABTUNER_H
