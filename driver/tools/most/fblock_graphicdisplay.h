#ifndef FBLOCK_GRAPHICDISPLAY_H
#define FBLOCK_GRAPHICDISPLAY_H

// GraphicDisplay FBlockID
#define FBLOCKID_GRAPHICDISPLAY 0x60

// GraphicDisplay Function IDs (generated from XML)
typedef enum {
    GRAPHICDISPLAY_FKT_FKTIDS                           = 0x000,  // FktIDs
    GRAPHICDISPLAY_FKT_NOTIFICATION                     = 0x001,  // Notification
    GRAPHICDISPLAY_FKT_NOTIFICATIONCHECK                = 0x002,  // NotificationCheck
    GRAPHICDISPLAY_FKT_PHYSICALMETRICS                  = 0x200,  // PhysicalMetrics
    GRAPHICDISPLAY_FKT_COLORMODE                        = 0x201,  // ColorMode
    GRAPHICDISPLAY_FKT_AVAILABLERESOURCES               = 0x202,  // AvailableResources
    GRAPHICDISPLAY_FKT_CREATEVIEWPORT                   = 0x203,  // CreateViewport
    GRAPHICDISPLAY_FKT_DESTROYVIEWPORT                  = 0x204,  // DestroyViewport
    GRAPHICDISPLAY_FKT_CREATEPALETTE                    = 0x205,  // CreatePalette
    GRAPHICDISPLAY_FKT_DESTROYPALETTE                   = 0x206,  // DestroyPalette
    GRAPHICDISPLAY_FKT_PALETTEENTRIES                   = 0x207,  // PaletteEntries
    GRAPHICDISPLAY_FKT_PALETTECOUNT                     = 0x208,  // PaletteCount
    GRAPHICDISPLAY_FKT_PALETTEINFORMATION               = 0x209,  // PaletteInformation
    GRAPHICDISPLAY_FKT_GRAPHICDATA                      = 0x20A,  // GraphicData
} GraphicDisplay_FktID;

#endif // FBLOCK_GRAPHICDISPLAY_H
