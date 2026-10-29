/* BMKs Survival Radio
 *
 * Channel groups are written to memory channels 0.. on first boot (marker at
 * EEPROM 0x1FF8). The group of a channel is found from its index, so scan and
 * up/down wrap inside the group (see RADIO_FindNextChannel).
 */

#ifdef ENABLE_SURVIVAL

#include <string.h>

#include "app/action.h"
#include "app/chFrScanner.h"
#ifdef ENABLE_FMRADIO
    #include "app/fm.h"
#endif
#include "app/survival.h"
#include "driver/eeprom.h"
#include "external/printf/printf.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/ui.h"

#define SURV_MARKER_ADDR 0x1FF8

// bump the last byte when the channel tables change, radios re-init on next boot
static const uint8_t SURV_MARKER[8] = {'B', 'M', 'K', 'S', 'R', 'V', 0, 1};

#define SURV_AM     (1u << 0)
#define SURV_NARROW (1u << 1)
#define SURV_HIGH   (1u << 2)   // full power, used only if TX lock is opened in an emergency

typedef struct {
    const char     *prefix;  // name = prefix + channel number
    const uint32_t *freqs;   // 10 Hz units, NULL: base + i * step
    uint32_t        base;
    uint16_t        step;
    uint8_t         count;
    uint8_t         flags;
    const uint8_t  *nums;    // channel numbers, NULL: 1..count, 200+n prints "Mn"
    const char     *names;   // NUL separated names, overrides prefix/nums
} SurvivalGroup_t;

static const uint32_t FRS_FREQS[] = {
    46256250, 46258750, 46261250, 46263750, 46266250, 46268750, 46271250,
    46756250, 46758750, 46761250, 46763750, 46766250, 46768750, 46771250,
    46255000, 46257500, 46260000, 46262500, 46265000, 46267500, 46270000, 46272500,
};

// distress/calling first, then the common simplex ship channels
static const uint8_t MARINE_NUMS[] = {
    16, 6, 67, 8, 9, 10, 11, 12, 13, 14, 15, 17, 68, 69, 71, 72, 73, 74, 77, 201, 202,
};
static const uint32_t MARINE_FREQS[] = {
    15680000, 15630000, 15637500, 15640000, 15645000, 15650000, 15655000,
    15660000, 15665000, 15670000, 15675000, 15685000, 15642500, 15647500,
    15657500, 15662500, 15667500, 15672500, 15687500, 15785000, 16142500,
};

static const uint32_t AIR_FREQS[] = {
    12150000, 24300000, 12310000, 12345000, 12280000, 12997500,
};
static const char AIR_NAMES[] = "GUARD\0MIL GUARD\0SAR\0AIR2AIR\0UNICOM\0GLIDING";

static const uint32_t HAM_FREQS[] = {
    14550000, 43350000, 14580000,
};
static const char HAM_NAMES[] = "2M CALL\0" "70CM CALL\0ISS";

// CEPT 40 channel plan, channel 9 = emergency
static const uint32_t CB_FREQS[] = {
    2696500, 2697500, 2698500, 2700500, 2701500, 2702500, 2703500, 2705500,
    2706500, 2707500, 2708500, 2710500, 2711500, 2712500, 2713500, 2715500,
    2716500, 2717500, 2718500, 2720500, 2721500, 2722500, 2725500, 2723500,
    2724500, 2726500, 2727500, 2728500, 2729500, 2730500, 2731500, 2732500,
    2733500, 2734500, 2735500, 2736500, 2737500, 2738500, 2739500, 2740500,
};

static const SurvivalGroup_t GROUPS[] = {
    {"PMR ", NULL,         44600625, 1250, 16,                      SURV_NARROW,           NULL,        NULL},
    {"FRS ", FRS_FREQS,    0,        0,    ARRAY_SIZE(FRS_FREQS),    SURV_HIGH,             NULL,        NULL},
    {"MAR ", MARINE_FREQS, 0,        0,    ARRAY_SIZE(MARINE_FREQS), SURV_HIGH,             MARINE_NUMS, NULL},
    {"",     AIR_FREQS,    0,        0,    ARRAY_SIZE(AIR_FREQS),    SURV_AM,               NULL,        AIR_NAMES},
    {"",     HAM_FREQS,    0,        0,    ARRAY_SIZE(HAM_FREQS),    SURV_HIGH,             NULL,        HAM_NAMES},
    {"CB ",  CB_FREQS,     0,        0,    ARRAY_SIZE(CB_FREQS),     SURV_NARROW|SURV_HIGH, NULL,        NULL},
};

static_assert(ARRAY_SIZE(MARINE_NUMS) == ARRAY_SIZE(MARINE_FREQS));

#define GROUP_NONE 0xFF

static uint8_t GroupStart(uint8_t g)
{
    uint8_t start = 0;
    for (uint8_t i = 0; i < g; i++)
        start += GROUPS[i].count;
    return start;
}

static uint8_t FindGroup(uint8_t channel)
{
    uint8_t start = 0;
    for (uint8_t g = 0; g < ARRAY_SIZE(GROUPS); g++) {
        start += GROUPS[g].count;
        if (channel < start)
            return g;
    }
    return GROUP_NONE;
}

bool SURVIVAL_GetGroupRange(uint8_t channel, uint8_t *first, uint8_t *last)
{
    const uint8_t g = FindGroup(channel);
    if (g == GROUP_NONE)
        return false;
    *first = GroupStart(g);
    *last  = *first + GROUPS[g].count - 1;
    return true;
}

uint8_t SURVIVAL_FindNextChannel(uint8_t Channel, int8_t Direction, bool bCheckScanList, uint8_t VFO)
{
    // stay inside the group of the channel we come from
    uint8_t first, last;
    if (!SURVIVAL_GetGroupRange(Channel - Direction, &first, &last))
        return RADIO_FindNextChannel(Channel, Direction, bCheckScanList, VFO);

    for (unsigned int i = 0; i <= (unsigned int)(last - first); i++, Channel += Direction) {
        if (Channel < first || Channel > last)
            Channel = (Direction > 0) ? first : last;

        if (bCheckScanList && gMR_ChannelExclude[Channel])
            continue;

        if (RADIO_CheckValidChannel(Channel, false, VFO))
            return Channel;
    }
    return 0xFF;
}

static void ChannelName(const SurvivalGroup_t *grp, uint8_t i, char *name)
{
    if (grp->names) {
        const char *s = grp->names;
        while (i--)
            s += strlen(s) + 1;
        strcpy(name, s);
        return;
    }

    const uint8_t num = grp->nums ? grp->nums[i] : i + 1;
    if (num > 200)
        sprintf(name, "%sM%u", grp->prefix, num - 200);
    else
        sprintf(name, "%s%u", grp->prefix, num);
}

static void WriteChannels(void)
{
    VFO_Info_t *pVfo = &gEeprom.VfoInfo[0];   // reloaded by RADIO_ConfigureChannel afterwards
    uint8_t     ch   = 0;
    char        name[16];

    for (uint8_t g = 0; g < ARRAY_SIZE(GROUPS); g++) {
        const SurvivalGroup_t *grp = &GROUPS[g];

        for (uint8_t i = 0; i < grp->count; i++, ch++) {
            const uint32_t freq = grp->freqs ? grp->freqs[i] : grp->base + (uint32_t)i * grp->step;

            RADIO_InitInfo(pVfo, ch, freq);
            pVfo->Modulation        = (grp->flags & SURV_AM)     ? MODULATION_AM     : MODULATION_FM;
            pVfo->CHANNEL_BANDWIDTH = (grp->flags & SURV_NARROW) ? BANDWIDTH_NARROW  : BANDWIDTH_WIDE;
            pVfo->OUTPUT_POWER      = (grp->flags & SURV_HIGH)   ? OUTPUT_POWER_HIGH : OUTPUT_POWER_LOW4;
            SETTINGS_SaveChannel(ch, 0, pVfo, 2);

            ChannelName(grp, i, name);
            SETTINGS_SaveChannelName(ch, name);
        }
    }
}

static void WriteDefaults(void)
{
    gEeprom.KEY_1_SHORT_PRESS_ACTION = ACTION_OPT_MONITOR;
    gEeprom.KEY_1_LONG_PRESS_ACTION  = ACTION_OPT_FLASHLIGHT;
    gEeprom.KEY_2_SHORT_PRESS_ACTION = ACTION_OPT_GROUP;
    gEeprom.KEY_2_LONG_PRESS_ACTION  = ACTION_OPT_SCAN;
    gEeprom.SCAN_RESUME_MODE         = 0;   // STOP: stay on the channel with activity
    gEeprom.SCAN_LIST_DEFAULT        = 5;   // all channels, the group limits the scan
    gEeprom.POWER_ON_DISPLAY_MODE    = POWER_ON_DISPLAY_MODE_ALL;
    gEeprom.CHANNEL_DISPLAY_MODE     = MDF_NAME_FREQ;
    gEeprom.DUAL_WATCH               = DUAL_WATCH_OFF;
    gEeprom.CROSS_BAND_RX_TX         = CROSS_BAND_OFF;
    gSetting_F_LOCK                  = F_LOCK_PMR;   // license-free TX only, open via menu in an emergency

    gEeprom.TX_VFO = 0;
    for (uint8_t vfo = 0; vfo < 2; vfo++) {
        gEeprom.MrChannel[vfo]     = 0;
        gEeprom.ScreenChannel[vfo] = 0;
    }

    SETTINGS_SaveSettings();
    SETTINGS_SaveVfoIndices();
}

void SURVIVAL_Init(void)
{
    uint8_t marker[8];

    EEPROM_ReadBuffer(SURV_MARKER_ADDR, marker, sizeof(marker));
    if (memcmp(marker, SURV_MARKER, sizeof(marker)) == 0)
        return;

    WriteChannels();
    WriteDefaults();
    EEPROM_WriteBuffer(SURV_MARKER_ADDR, SURV_MARKER);
}

void SURVIVAL_EraseMarker(void)
{
    uint8_t blank[8];
    memset(blank, 0xFF, sizeof(blank));
    EEPROM_WriteBuffer(SURV_MARKER_ADDR, blank);
}

void ACTION_SurvivalGroup(void)
{
    const uint8_t vfo  = gEeprom.TX_VFO;
    uint8_t       next = 0;

#ifdef ENABLE_FMRADIO
    if (gFmRadioMode) {
        ACTION_FM();   // FM is the last stop, wrap to the first group
    } else
#endif
    {
        if (gScanStateDir != SCAN_OFF)
            CHFRSCANNER_Stop();

        const uint8_t g = FindGroup(gEeprom.MrChannel[vfo]);
        if (g != GROUP_NONE)
            next = g + 1;

        if (next >= ARRAY_SIZE(GROUPS)) {
#ifdef ENABLE_FMRADIO
            ACTION_FM();
            return;
#else
            next = 0;
#endif
        }
    }

    const uint8_t ch = GroupStart(next);
    gEeprom.MrChannel[vfo]     = ch;
    gEeprom.ScreenChannel[vfo] = ch;
    gRequestSaveVFO            = true;
    gVfoConfigureMode          = VFO_CONFIGURE_RELOAD;
    gRequestDisplayScreen      = DISPLAY_MAIN;
    gUpdateStatus              = true;
}

#endif
