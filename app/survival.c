/* BMKs Survival Radio
 *
 * Channel groups are written to memory channels 0.. on first boot (marker at
 * EEPROM 0x1FF8). The group of a channel is found from its index, so scan and
 * up/down wrap inside the group (see SURVIVAL_FindNextChannel).
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
#include "frequencies.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/ui.h"

// EEPROM 0x1FF8: 'B' 'M' 'K' 'S', table version, TX mask, FF, FF
#define SURV_MARKER_ADDR   0x1FF8
// EEPROM 0x1FE8: CUSTOM scan range, start and end as uint32 (10 Hz units)
#define SURV_RANGE_ADDR    0x1FE8
#define SURV_RANGE_START   14400000   // 144.000 MHz
#define SURV_RANGE_END     14600000   // 146.000 MHz
// bump when the channel tables or defaults change, radios re-init on next boot
#define SURV_TABLE_VERSION 2
#define SURV_TX_DEFAULT    ((1u << SURV_TX_PMR) | (1u << SURV_TX_FRS))

static const uint8_t SURV_MAGIC[4] = {'B', 'M', 'K', 'S'};

uint8_t  gSurvivalTxMask      = SURV_TX_DEFAULT;
uint32_t gSurvivalCustomStart = SURV_RANGE_START;
uint32_t gSurvivalCustomEnd   = SURV_RANGE_END;

const char gSubMenu_SRV_MODE[SURV_MODE_COUNT][9] = {
    "PMR446", "FRS", "MARINE", "AIR", "HAM", "CB", "FM RADIO", "CUSTOM",
};

#define SURV_AM     (1u << 0)
#define SURV_NARROW (1u << 1)
#define SURV_HIGH   (1u << 2)   // full power, otherwise LOW4 (~0.5 W)

typedef struct {
    uint32_t lower;
    uint32_t upper;
    uint8_t  bit;
} TxBand_t;

static const TxBand_t TX_BANDS[] = {
    {44600625, 44619375, SURV_TX_PMR},
    {46255000, 46272500, SURV_TX_FRS},
    {46755000, 46772500, SURV_TX_FRS},
    {15600000, 16202500, SURV_TX_MARINE},
    {14400000, 14600000, SURV_TX_HAM},
    {43000000, 44000000, SURV_TX_HAM},
    { 2696500,  2740500, SURV_TX_CB},   // CEPT
    { 2760125,  2799125, SURV_TX_CB},   // UK 27/81
};

// never transmit here, not even with TxAll
static const TxBand_t TX_BLOCKED[] = {
    { 8800000, 13700000, 0},   // FM broadcast + civil aviation (VOR/ILS, 121.5 guard)
    {22500000, 40000000, 0},   // military aviation (243.0 guard)
    {40600000, 40610000, 0},   // COSPAS-SARSAT distress beacons
};

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
    {"FRS ", FRS_FREQS,    0,        0,    ARRAY_SIZE(FRS_FREQS),    0,                     NULL,        NULL},
    {"MAR ", MARINE_FREQS, 0,        0,    ARRAY_SIZE(MARINE_FREQS), SURV_HIGH,             MARINE_NUMS, NULL},
    {"",     AIR_FREQS,    0,        0,    ARRAY_SIZE(AIR_FREQS),    SURV_AM,               NULL,        AIR_NAMES},
    {"",     HAM_FREQS,    0,        0,    ARRAY_SIZE(HAM_FREQS),    SURV_HIGH,             NULL,        HAM_NAMES},
    {"CB ",  CB_FREQS,     0,        0,    ARRAY_SIZE(CB_FREQS),     SURV_NARROW|SURV_HIGH, NULL,        NULL},
};

static_assert(ARRAY_SIZE(MARINE_NUMS) == ARRAY_SIZE(MARINE_FREQS));
static_assert(ARRAY_SIZE(GROUPS) == SURV_MODE_FM);

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

    gEeprom.TX_VFO = 0;
    for (uint8_t vfo = 0; vfo < 2; vfo++) {
        gEeprom.MrChannel[vfo]     = 0;
        gEeprom.ScreenChannel[vfo] = 0;
    }

    SETTINGS_SaveSettings();
    SETTINGS_SaveVfoIndices();
}

void SURVIVAL_SaveTxMask(void)
{
    const uint8_t marker[8] = {
        SURV_MAGIC[0], SURV_MAGIC[1], SURV_MAGIC[2], SURV_MAGIC[3],
        SURV_TABLE_VERSION, gSurvivalTxMask, 0xFF, 0xFF,
    };
    EEPROM_WriteBuffer(SURV_MARKER_ADDR, marker);
}

void SURVIVAL_SaveCustomRange(void)
{
    if (gSurvivalCustomStart > gSurvivalCustomEnd) {
        const uint32_t tmp   = gSurvivalCustomStart;
        gSurvivalCustomStart = gSurvivalCustomEnd;
        gSurvivalCustomEnd   = tmp;
    }

    const uint32_t range[2] = {gSurvivalCustomStart, gSurvivalCustomEnd};
    EEPROM_WriteBuffer(SURV_RANGE_ADDR, range);
}

void SURVIVAL_Init(void)
{
    uint8_t marker[8];

    EEPROM_ReadBuffer(SURV_MARKER_ADDR, marker, sizeof(marker));
    if (memcmp(marker, SURV_MAGIC, sizeof(SURV_MAGIC)) == 0 && marker[4] == SURV_TABLE_VERSION) {
        uint32_t range[2];
        EEPROM_ReadBuffer(SURV_RANGE_ADDR, range, sizeof(range));
        gSurvivalTxMask = marker[5];
        if (range[0] >= BX4819_band1.lower && range[0] < range[1] && range[1] <= BX4819_band2.upper) {
            gSurvivalCustomStart = range[0];
            gSurvivalCustomEnd   = range[1];
        }
        return;
    }

    WriteChannels();
    WriteDefaults();
    gSurvivalTxMask = SURV_TX_DEFAULT;
    SURVIVAL_SaveTxMask();
    SURVIVAL_SaveCustomRange();
}

void SURVIVAL_ApplyScanRange(void)
{
    // CUSTOM mode (VFO) scans the user range with the VFO step
    if (!IS_MR_CHANNEL(gRxVfo->CHANNEL_SAVE)) {
        gScanRangeStart = gSurvivalCustomStart;
        gScanRangeStop  = gSurvivalCustomEnd;
    }
}

bool SURVIVAL_TxAllowed(uint32_t frequency)
{
    if ((gSurvivalTxMask >> SURV_TX_ALL) & 1u) {
        for (uint8_t i = 0; i < ARRAY_SIZE(TX_BLOCKED); i++) {
            if (frequency >= TX_BLOCKED[i].lower && frequency < TX_BLOCKED[i].upper)
                return false;
        }
        return true;
    }

    for (uint8_t i = 0; i < ARRAY_SIZE(TX_BANDS); i++) {
        if (frequency >= TX_BANDS[i].lower && frequency <= TX_BANDS[i].upper)
            return (gSurvivalTxMask >> TX_BANDS[i].bit) & 1u;
    }
    return false;
}

void SURVIVAL_EraseMarker(void)
{
    uint8_t blank[8];
    memset(blank, 0xFF, sizeof(blank));
    EEPROM_WriteBuffer(SURV_MARKER_ADDR, blank);
}

uint8_t SURVIVAL_GetMode(void)
{
#ifdef ENABLE_FMRADIO
    if (gFmRadioMode)
        return SURV_MODE_FM;
#endif
    const uint8_t channel = gEeprom.ScreenChannel[gEeprom.TX_VFO];
    if (!IS_MR_CHANNEL(channel))
        return SURV_MODE_CUSTOM;

    const uint8_t g = FindGroup(channel);
    return (g == GROUP_NONE) ? SURV_MODE_CUSTOM : g;   // own saved channels count as custom
}

void SURVIVAL_SetMode(uint8_t mode)
{
    const uint8_t vfo = gEeprom.TX_VFO;

    if (gScanStateDir != SCAN_OFF)
        CHFRSCANNER_Stop();

#ifdef ENABLE_FMRADIO
    if (gFmRadioMode != (mode == SURV_MODE_FM))
        ACTION_FM();   // toggles FM radio on/off
    if (mode == SURV_MODE_FM)
        return;
#else
    if (mode == SURV_MODE_FM)
        mode = SURV_MODE_CUSTOM;
#endif

    if (mode == SURV_MODE_CUSTOM) {
        gEeprom.ScreenChannel[vfo] = gEeprom.FreqChannel[vfo];   // free frequency (VFO)
    } else {
        const uint8_t ch = GroupStart(mode);
        gEeprom.MrChannel[vfo]     = ch;
        gEeprom.ScreenChannel[vfo] = ch;
    }

    gRequestSaveVFO       = true;
    gVfoConfigureMode     = VFO_CONFIGURE_RELOAD;
    gRequestDisplayScreen = DISPLAY_MAIN;
    gUpdateStatus         = true;
}

void ACTION_SurvivalGroup(void)
{
    SURVIVAL_SetMode((SURVIVAL_GetMode() + 1) % SURV_MODE_COUNT);
}

#endif
