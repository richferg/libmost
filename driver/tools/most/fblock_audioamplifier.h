#ifndef FBLOCK_AUDIOAMPLIFIER_H
#define FBLOCK_AUDIOAMPLIFIER_H

// AudioAmplifier FBlockID
#define FBLOCKID_AUDIOAMPLIFIER 0x22

// AudioAmplifier Function IDs (generated from XML)
typedef enum {
    AUDIOAMPLIFIER_FKT_BALANCE                          = 0x200,  // Balance
    AUDIOAMPLIFIER_FKT_LOUDNESS                         = 0x201,  // Loudness
    AUDIOAMPLIFIER_FKT_BASS                             = 0x202,  // Bass
    AUDIOAMPLIFIER_FKT_TREBLE                           = 0x203,  // Treble
    AUDIOAMPLIFIER_FKT_FADER                            = 0x204,  // Fader
    AUDIOAMPLIFIER_FKT_VOLUME                           = 0x400,  // Volume
    AUDIOAMPLIFIER_FKT_FADEINOUT                        = 0x401,  // FadeInOut
    AUDIOAMPLIFIER_FKT_SUBWOOFER                        = 0x402,  // Subwoofer
    AUDIOAMPLIFIER_FKT_BASSBOOST                        = 0x404,  // BassBoost
    AUDIOAMPLIFIER_FKT_COMPTHRESHOLD                    = 0x421,  // CompThreshold
    AUDIOAMPLIFIER_FKT_LIMTHRESHOLD                     = 0x422,  // LimThreshold
    AUDIOAMPLIFIER_FKT_COMPGAIN                         = 0x423,  // CompGain
    AUDIOAMPLIFIER_FKT_ATTACKTIME                       = 0x424,  // AttackTime
    AUDIOAMPLIFIER_FKT_RELEASETIME                      = 0x425,  // ReleaseTime
    AUDIOAMPLIFIER_FKT_COMPRESSORSETTINGS               = 0x426,  // CompressorSettings
    AUDIOAMPLIFIER_FKT_LIMITERSETTINGS                  = 0x427,  // LimiterSettings
    AUDIOAMPLIFIER_FKT_CROSSOVER                        = 0x430,  // Crossover
    AUDIOAMPLIFIER_FKT_CROSSOVERSLOPE                   = 0x431,  // CrossoverSlope
    AUDIOAMPLIFIER_FKT_DELAYLINE                        = 0x440,  // DelayLine
    AUDIOAMPLIFIER_FKT_SPEAKERDELAY                     = 0x441,  // SpeakerDelay
    AUDIOAMPLIFIER_FKT_INPUTGAINOFFSET                  = 0x450,  // InputGainOffset
    AUDIOAMPLIFIER_FKT_OUTPUTGAINOFFSET                 = 0x451,  // OutputGainOffset
    AUDIOAMPLIFIER_FKT_OUTPUTPHASE                      = 0x452,  // OutputPhase
    AUDIOAMPLIFIER_FKT_EQUALIZERONOFF                   = 0x460,  // EqualizerOnOff
    AUDIOAMPLIFIER_FKT_EQUALIZERSETTINGS                = 0x461,  // EqualizerSettings
    AUDIOAMPLIFIER_FKT_GRAPHEQUALIZERONOFF              = 0x462,  // GraphEqualizerOnOff
    AUDIOAMPLIFIER_FKT_GRAPHEQUALIZER                   = 0x463,  // GraphEqualizer
    AUDIOAMPLIFIER_FKT_GRAPHEQUALIZERLINEAR             = 0x464,  // GraphEqualizerLinear
    AUDIOAMPLIFIER_FKT_MIDTONES                         = 0x465,  // MidTones
    AUDIOAMPLIFIER_FKT_MUTEPARAMETERS                   = 0x466,  // MuteParameters
    AUDIOAMPLIFIER_FKT_MIXERLEVEL                       = 0x467,  // MixerLevel
    AUDIOAMPLIFIER_FKT_SOUNDSETTINGLIST                 = 0x468,  // SoundSettingList
    AUDIOAMPLIFIER_FKT_RECALLSOUNDSETTING               = 0x469,  // RecallSoundSetting
    AUDIOAMPLIFIER_FKT_SAVESOUNDSETTING                 = 0x46A,  // SaveSoundSetting
    AUDIOAMPLIFIER_FKT_DYNSOUNDCONTROL                  = 0x46B,  // DynSoundControl
    AUDIOAMPLIFIER_FKT_CURRENTSOUNDSETTING              = 0x46C,  // CurrentSoundSetting
    AUDIOAMPLIFIER_FKT_SPEAKERLEVEL                     = 0x46D,  // SpeakerLevel
} AudioAmplifier_FktID;

#endif // FBLOCK_AUDIOAMPLIFIER_H
