#ifndef FBLOCK_TELEPHONE_H
#define FBLOCK_TELEPHONE_H

// Telephone FBlockID
#define FBLOCKID_TELEPHONE 0x50

// Telephone Function IDs (generated from XML)
typedef enum {
    TELEPHONE_FKT_FKTIDS                           = 0x000,  // FktIDs
    TELEPHONE_FKT_NOTIFICATION                     = 0x001,  // Notification
    TELEPHONE_FKT_NOTIFICATIONCHECK                = 0x002,  // NotificationCheck
    TELEPHONE_FKT_SOURCEINFO                       = 0x100,  // SourceInfo
    TELEPHONE_FKT_ALLOCATE                         = 0x101,  // Allocate
    TELEPHONE_FKT_DEALLOCATE                       = 0x102,  // DeAllocate
    TELEPHONE_FKT_SOURCEACTIVITY                   = 0x103,  // SourceActivity
    TELEPHONE_FKT_SOURCENAME                       = 0x104,  // SourceName
    TELEPHONE_FKT_SOURCECONNECT                    = 0x105,  // SourceConnect
    TELEPHONE_FKT_SOURCEDISCONNECT                 = 0x106,  // SourceDisConnect
    TELEPHONE_FKT_SOURCEROUTING                    = 0x107,  // SourceRouting
    TELEPHONE_FKT_SINKINFO                         = 0x110,  // SinkInfo
    TELEPHONE_FKT_CONNECT                          = 0x111,  // Connect
    TELEPHONE_FKT_DISCONNECT                       = 0x112,  // DisConnect
    TELEPHONE_FKT_MUTE                             = 0x113,  // Mute
    TELEPHONE_FKT_SINKNAME                         = 0x114,  // SinkName
    TELEPHONE_FKT_CONNECTTO                        = 0x115,  // ConnectTo
    TELEPHONE_FKT_SYNCDATAINFO                     = 0x116,  // SyncDataInfo
    TELEPHONE_FKT_SINKROUTING                      = 0x117,  // SinkRouting
    TELEPHONE_FKT_PHONEENABLE                      = 0x200,  // PhoneEnable
    TELEPHONE_FKT_CHANGECODE                       = 0x201,  // ChangeCode
    TELEPHONE_FKT_LOCKSTATE                        = 0x202,  // LockState
    TELEPHONE_FKT_DIALNUMBER                       = 0x250,  // DialNumber
    TELEPHONE_FKT_HANGUPCALL                       = 0x251,  // HangupCall
    TELEPHONE_FKT_CURRENTNUMBER                    = 0x252,  // CurrentNumber
    TELEPHONE_FKT_CALLSTATE                        = 0x253,  // CallState
    TELEPHONE_FKT_CALLDURATION                     = 0x254,  // CallDuration
    TELEPHONE_FKT_ACCEPTCALL                       = 0x255,  // AcceptCall
    TELEPHONE_FKT_CALLINFO                         = 0x25A,  // CallInfo
    TELEPHONE_FKT_CALLHOLD                         = 0x260,  // CallHold
    TELEPHONE_FKT_RESUMECALL                       = 0x261,  // ResumeCall
    TELEPHONE_FKT_CALLFORWARD                      = 0x262,  // CallForward
    TELEPHONE_FKT_CALLDEFLECTION                   = 0x263,  // CallDeflection
    TELEPHONE_FKT_CALLBARRING                      = 0x264,  // CallBarring
    TELEPHONE_FKT_FOLLOWUPTIME                     = 0x400,  // FollowUpTime
    TELEPHONE_FKT_RESTOREFACTORYSETTINGS           = 0x401,  // RestoreFactorySettings
    TELEPHONE_FKT_CLIR                             = 0x402,  // CLIR
    TELEPHONE_FKT_CLIP                             = 0x403,  // CLIP
    TELEPHONE_FKT_CLOP                             = 0x404,  // CLOP
    TELEPHONE_FKT_NETWORKOPERATORSELECTION         = 0x420,  // NetworkOperatorSelection
    TELEPHONE_FKT_NETWORKOPERATORS                 = 0x421,  // NetworkOperators
    TELEPHONE_FKT_HOMENETWORK                      = 0x422,  // HomeNetwork
    TELEPHONE_FKT_SIGNALQUALITY                    = 0x423,  // SignalQuality
    TELEPHONE_FKT_SERIALNUMBER                     = 0x424,  // SerialNumber
    TELEPHONE_FKT_MOBILESTANDARD                   = 0x425,  // MobileStandard
    TELEPHONE_FKT_LINESTATE                        = 0x426,  // LineState
    TELEPHONE_FKT_REGISTERSTATE                    = 0x427,  // RegisterState
    TELEPHONE_FKT_PRESSEDKEY                       = 0x428,  // PressedKey
    TELEPHONE_FKT_VOICEMAILBOXNUMBER               = 0x441,  // VoiceMailboxNumber
    TELEPHONE_FKT_VOICEMAILBOXCONNECT              = 0x442,  // VoiceMailboxConnect
    TELEPHONE_FKT_SENDDTMF                         = 0x443,  // SendDTMF
    TELEPHONE_FKT_HANDSFREEONOFF                   = 0x446,  // HandsFreeOnOff
    TELEPHONE_FKT_SIMCARDREADERINFO                = 0x462,  // SimCardReaderInfo
    TELEPHONE_FKT_POWERSUPPLY                      = 0x466,  // PowerSupply
    TELEPHONE_FKT_PHONEDATE                        = 0x490,  // PhoneDate
    TELEPHONE_FKT_PHONETIME                        = 0x491,  // PhoneTime
    TELEPHONE_FKT_ADVICEOFCHARGE                   = 0x492,  // AdviceOfCharge
    TELEPHONE_FKT_SMSSHOWLIST                      = 0x500,  // SMSShowList
    TELEPHONE_FKT_SMSSHOWDETAILS                   = 0x501,  // SMSShowDetails
    TELEPHONE_FKT_SMSSEND                          = 0x502,  // SMSSend
    TELEPHONE_FKT_SMSSENDFROMSTORAGE               = 0x503,  // SMSSendFromStorage
    TELEPHONE_FKT_SMSSTORE                         = 0x504,  // SMSStore
    TELEPHONE_FKT_SMSNEW                           = 0x505,  // SMSNew
    TELEPHONE_FKT_SMSATTRIB                        = 0x506,  // SMSAttrib
    TELEPHONE_FKT_SMSDELETE                        = 0x507,  // SMSDelete
    TELEPHONE_FKT_SMSCBCLEAR                       = 0x510,  // SMSCBClear
    TELEPHONE_FKT_SMSCBRECEIVE                     = 0x511,  // SMSCBReceive
    TELEPHONE_FKT_SMSCBSETTINGS                    = 0x512,  // SMSCBSettings
    TELEPHONE_FKT_SMSSTORAGES                      = 0x513,  // SMSStorages
    TELEPHONE_FKT_SMSSTORAGESELECT                 = 0x514,  // SMSStorageSelect 
    TELEPHONE_FKT_SMSBINARYSHOWLIST                = 0x520,  // SMSBinaryShowList
    TELEPHONE_FKT_SMSBINARYSHOWDETAILS             = 0x521,  // SMSBinaryShowDetails
    TELEPHONE_FKT_SMSBINARYSEND                    = 0x522,  // SMSBinarySend
    TELEPHONE_FKT_MPMULTIPARTYCALL                 = 0x550,  // MPMultiPartyCall
    TELEPHONE_FKT_MPRELEASEACTIVECALLACCEPTHELDCALL = 0x551,  // MPReleaseActiveCallAcceptHeldCall
    TELEPHONE_FKT_MPRELEASEACTIVECALLACCEPTWAITINGCALL = 0x552,  // MPReleaseActiveCallAcceptWaitingCall
    TELEPHONE_FKT_MPSWAP                           = 0x553,  // MPSwap
    TELEPHONE_FKT_MPCALLHOLDACCEPTWAITINGCALL      = 0x554,  // MPCallHoldAcceptWaitingCall
    TELEPHONE_FKT_MPRELEASEALLCALLSEXCEPTWAITINGCALL = 0x555,  // MPReleaseAllCallsExceptWaitingCall
    TELEPHONE_FKT_MPRELEASEALLCALLSACCEPTWAITINGCALL = 0x556,  // MPReleaseAllCallsAcceptWaitingCall
    TELEPHONE_FKT_MPEXPLICITCALLTRANSFER           = 0x557,  // MPExplicitCallTransfer
    TELEPHONE_FKT_CCCONFERENCECALL                 = 0x558,  // CCConferenceCall
    TELEPHONE_FKT_CCSPLIT                          = 0x559,  // CCSplit
    TELEPHONE_FKT_CCJOIN                           = 0x55A,  // CCJoin
    TELEPHONE_FKT_WAITINGCALLALERT                 = 0x560,  // WaitingCallAlert
    TELEPHONE_FKT_OPENDATACONNECTION               = 0x600,  // OpenDataConnection
    TELEPHONE_FKT_ENDDATACONNECTION                = 0x601,  // EndDataConnection
    TELEPHONE_FKT_ACCEPTDATACONNECTION             = 0x602,  // AcceptDataConnection
    TELEPHONE_FKT_DATACAPABILITIES                 = 0x603,  // DataCapabilities
    TELEPHONE_FKT_CONNECTIONSTATUS                 = 0x604,  // ConnectionStatus
    TELEPHONE_FKT_DATASPEEDCAPABILITIES            = 0x605,  // DataSpeedCapabilities
} Telephone_FktID;

#endif // FBLOCK_TELEPHONE_H
