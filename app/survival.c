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
#include "functions.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/ui.h"

// EEPROM 0x1FF8: 'B' 'M' 'K' 'S', table version, TX mask, FM background scan, settings revision
#define SURV_MARKER_ADDR   0x1FF8
// EEPROM 0x1FE8: CUSTOM scan range, start and end as uint32 (10 Hz units)
#define SURV_RANGE_ADDR    0x1FE8
#define SURV_RANGE_START   14400000   // 144.000 MHz
#define SURV_RANGE_END     14600000   // 146.000 MHz
// bump when the channel tables or defaults change, radios re-init on next boot
#define SURV_TABLE_VERSION 5
// bump for setting changes that existing radios get without a re-init (byte 7)
#define SURV_SETTINGS_REV  1
#define SURV_TX_DEFAULT    ((1u << SURV_TX_PMR) | (1u << SURV_TX_FRS))

static const uint8_t SURV_MAGIC[4] = {'B', 'M', 'K', 'S'};

uint8_t  gSurvivalTxMask      = SURV_TX_DEFAULT;
uint8_t  gSurvivalFmScan      = SURV_FMSCAN_LAST;

static bool sFmBgScan;       // BK4819 scans a group while the FM radio plays
static bool sStopOnRxEnd;    // a background hit ends the scan when that transmission ends
static bool sScanBeforeFm;   // a scan was running when FM was switched on
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
    {14400000, 14800000, SURV_TX_HAM},  // 2 m, widest (region 2/3)
    {42000000, 45000000, SURV_TX_HAM},  // 70 cm, widest (region 2)
    { 2656500,  2799125, SURV_TX_CB},   // 40 ch CEPT/FCC plus national CB plans
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

// distress/calling first, then the common simplex ship channels, then the
// duplex channels on the coast station frequency (coastguard MSI, ports, VTS);
// duplex channels transmit 4.6 MHz lower on the ship frequency.
// 201..207 = NOAA weather WX1..WX7 (US/Canada, receive only)
static const uint8_t MARINE_NUMS[] = {
    16, 6, 67, 8, 9, 10, 11, 12, 13, 14, 15, 17, 68, 69, 71, 72, 73, 74, 77,
     1,  2,  3,  4,  5,  7, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28,
    60, 61, 62, 63, 64, 65, 66, 78, 79, 80, 81, 82, 83, 84, 85, 86,
    201, 202, 203, 204, 205, 206, 207,
};
static const uint32_t MARINE_FREQS[] = {
    15680000, 15630000, 15637500, 15640000, 15645000, 15650000, 15655000,
    15660000, 15665000, 15670000, 15675000, 15685000, 15642500, 15647500,
    15657500, 15662500, 15667500, 15672500, 15687500,
    16065000, 16070000, 16075000, 16080000, 16085000, 16095000, 16150000,
    16155000, 16160000, 16165000, 16170000, 16175000, 16180000, 16185000,
    16190000, 16195000, 16200000, 16062500, 16067500, 16072500, 16077500,
    16082500, 16087500, 16092500, 16152500, 16157500, 16162500, 16167500,
    16172500, 16177500, 16182500, 16187500, 16192500,
    16255000, 16240000, 16247500, 16242500, 16245000, 16250000, 16252500,
};
#define MARINE_DUPLEX_OFFSET 460000   // 4.6 MHz

// AIR is not a channel group but the whole air band on its own VFO slot
// (108-137 MHz band memory): AM, 25 kHz steps, scanned 118.000-136.975 MHz
#define AIR_VFO_CHANNEL (FREQ_CHANNEL_FIRST + BAND2_108MHz)
#define AIR_SCAN_START  11800000
#define AIR_SCAN_END    13697500
#define AIR_START_FREQ  12150000   // 121.500 guard

// FM calling channels for ITU region 1 (Europe/Africa/Middle East) and 2 (Americas)
static const uint32_t HAM_FREQS[] = {
    14550000, 14652000, 43350000, 44600000, 14580000,
};
static const char HAM_NAMES[] = "2M CALL EU\0" "2M CALL US\0" "70 CALL EU\0" "70 CALL US\0ISS";

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
    {"",     NULL,         0,        0,    0,                        SURV_AM,               NULL,        NULL},   // AIR: band mode, no channels
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
        sprintf(name, "%sWX%u", grp->prefix, num - 200);
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
            if (grp->freqs == MARINE_FREQS && grp->nums[i] < 200 && freq >= 16000000) {
                pVfo->TX_OFFSET_FREQUENCY           = MARINE_DUPLEX_OFFSET;
                pVfo->TX_OFFSET_FREQUENCY_DIRECTION = TX_OFFSET_FREQUENCY_DIRECTION_SUB;
            }
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
    gEeprom.KEY_2_SHORT_PRESS_ACTION = ACTION_OPT_FM;      // FM radio on/off, M switches modes
    gEeprom.KEY_2_LONG_PRESS_ACTION  = ACTION_OPT_SCAN;
    gEeprom.SCAN_RESUME_MODE         = 0;   // STOP: stay on the channel with activity
    gEeprom.SCAN_LIST_DEFAULT        = 5;   // all channels, the group limits the scan
    gEeprom.SQUELCH_LEVEL            = 1;   // most sensitive; raise it if noise stops the scan
    gEeprom.POWER_ON_DISPLAY_MODE    = POWER_ON_DISPLAY_MODE_ALL;
    gEeprom.CHANNEL_DISPLAY_MODE     = MDF_NAME_FREQ;
    gEeprom.DUAL_WATCH               = DUAL_WATCH_OFF;
    gEeprom.CROSS_BAND_RX_TX         = CROSS_BAND_OFF;
    gSetting_set_inv                 = false;   // normal (not negative) display
    gSetting_set_ctr                 = 10;      // F4HWN default contrast, blank EEPROM reads as 15
    gSetting_set_lck                 = false;   // key lock locks keys only, not PTT

    gEeprom.TX_VFO = 0;
    for (uint8_t vfo = 0; vfo < 2; vfo++) {
        gEeprom.MrChannel[vfo]     = 0;
        gEeprom.ScreenChannel[vfo] = 0;

        VFO_Info_t *pVfo = &gEeprom.VfoInfo[vfo];
        RADIO_InitInfo(pVfo, AIR_VFO_CHANNEL, AIR_START_FREQ);   // AM for this band
        pVfo->STEP_SETTING  = STEP_25kHz;
        pVfo->StepFrequency = gStepFrequencyTable[STEP_25kHz];
        SETTINGS_SaveChannel(AIR_VFO_CHANNEL, vfo, pVfo, 2);
    }

    SETTINGS_SaveSettings();
    SETTINGS_SaveVfoIndices();
}

void SURVIVAL_SaveSettings(void)
{
    const uint8_t marker[8] = {
        SURV_MAGIC[0], SURV_MAGIC[1], SURV_MAGIC[2], SURV_MAGIC[3],
        SURV_TABLE_VERSION, gSurvivalTxMask, gSurvivalFmScan, SURV_SETTINGS_REV,
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
        if (marker[6] <= SURV_FMSCAN_LAST)
            gSurvivalFmScan = marker[6];
        if (marker[7] != SURV_SETTINGS_REV) {
            // rev 1: side key 2 short press toggles the FM radio
            gEeprom.KEY_2_SHORT_PRESS_ACTION = ACTION_OPT_FM;
            SETTINGS_SaveSettings();
            SURVIVAL_SaveSettings();
        }
        if (range[0] >= BX4819_band1.lower && range[0] < range[1] && range[1] <= BX4819_band2.upper) {
            gSurvivalCustomStart = range[0];
            gSurvivalCustomEnd   = range[1];
        }
        return;
    }

    WriteChannels();
    WriteDefaults();
    gSurvivalTxMask = SURV_TX_DEFAULT;
    SURVIVAL_SaveSettings();
    SURVIVAL_SaveCustomRange();
}

void SURVIVAL_ApplyScanRange(void)
{
    sStopOnRxEnd = false;   // every scan start clears a pending background stop

    // VFO modes scan a range with the VFO step: AIR the air band, CUSTOM the user range
    const uint8_t channel = gRxVfo->CHANNEL_SAVE;
    if (channel == AIR_VFO_CHANNEL) {
        gScanRangeStart = AIR_SCAN_START;
        gScanRangeStop  = AIR_SCAN_END;
    } else if (!IS_MR_CHANNEL(channel)) {
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

    // bands may overlap (70 cm contains PMR446), any permitted band is enough
    for (uint8_t i = 0; i < ARRAY_SIZE(TX_BANDS); i++) {
        if (frequency >= TX_BANDS[i].lower && frequency <= TX_BANDS[i].upper &&
            ((gSurvivalTxMask >> TX_BANDS[i].bit) & 1u))
            return true;
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
    if (channel == AIR_VFO_CHANNEL)
        return SURV_MODE_AIR;
    if (!IS_MR_CHANNEL(channel))
        return SURV_MODE_CUSTOM;

    const uint8_t g = FindGroup(channel);
    return (g == GROUP_NONE) ? SURV_MODE_CUSTOM : g;   // own saved channels count as custom
}

// point the VFO at the first channel of a group, or at the air band VFO
static void SelectModeChannel(uint8_t mode, uint8_t vfo)
{
    if (mode == SURV_MODE_AIR) {
        gEeprom.ScreenChannel[vfo] = AIR_VFO_CHANNEL;
        return;
    }
    const uint8_t ch = GroupStart(mode);
    gEeprom.MrChannel[vfo]     = ch;
    gEeprom.ScreenChannel[vfo] = ch;
}

// while a VFO range is scanned the screen is redrawn every few steps only, so
// the 10 kHz and smaller digits would lag: show "123.4----" (the 100 kHz digit
// still moves) until the scan stops on a signal
void SURVIVAL_MaskScanDigits(char *freq)
{
    if (gScanStateDir == SCAN_OFF || FUNCTION_IsRx())
        return;
    char *p = strchr(freq, '.');
    if (p && p[1])
        for (p += 2; *p; p++)
            *p = '-';
}

const char *SURVIVAL_VfoLabel(uint8_t channel)
{
    return (channel == AIR_VFO_CHANNEL) ? "AIR BAND" : "CUSTOM";
}

void SURVIVAL_SetMode(uint8_t mode)
{
    const uint8_t vfo = gEeprom.TX_VFO;

    // the FM background scan only continues a scan that was already running
    sScanBeforeFm = (mode == SURV_MODE_FM) && gScanStateDir != SCAN_OFF;

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
        // free frequency (VFO); the air band slot belongs to AIR
        if (gEeprom.FreqChannel[vfo] == AIR_VFO_CHANNEL)
            gEeprom.FreqChannel[vfo] = FREQ_CHANNEL_FIRST + BAND6_400MHz;
        gEeprom.ScreenChannel[vfo] = gEeprom.FreqChannel[vfo];
    } else {
        SelectModeChannel(mode, vfo);
    }

    gRequestSaveVFO       = true;
    gVfoConfigureMode     = VFO_CONFIGURE_RELOAD;
    gRequestDisplayScreen = DISPLAY_MAIN;
    gUpdateStatus         = true;
}

void SURVIVAL_ToggleFm(void)
{
#ifdef ENABLE_FMRADIO
    if (gFmRadioMode)
        ACTION_FM();   // back to the previous mode
    else
        SURVIVAL_SetMode(SURV_MODE_FM);
#endif
}

void ACTION_SurvivalGroup(void)
{
    SURVIVAL_SetMode((SURVIVAL_GetMode() + 1) % SURV_MODE_COUNT);
}

void SURVIVAL_FmStarted(void)
{
    // scan on the BK4819 while the BK1080 plays FM
    const uint8_t vfo = gEeprom.TX_VFO;

    const bool scanning = sScanBeforeFm;
    sScanBeforeFm = false;

    // no scan before FM: only watch the current channel
    if (!scanning || gSurvivalFmScan == 0 || gSurvivalFmScan > SURV_FMSCAN_LAST)
        return;

    if (gSurvivalFmScan == SURV_FMSCAN_LAST) {
        // the mode used before FM: its group from the current channel on,
        // or the CStart..CEnd range in CUSTOM; own saved channels are not scanned
        const uint8_t ch = gEeprom.ScreenChannel[vfo];
        if (IS_MR_CHANNEL(ch) && FindGroup(ch) == GROUP_NONE)
            return;
    } else {
        SelectModeChannel(gSurvivalFmScan - 1, vfo);
        RADIO_ConfigureChannel(vfo, VFO_CONFIGURE_RELOAD);
    }
    RADIO_SelectVfos();

    sFmBgScan = true;
    CHFRSCANNER_Start(true, SCAN_FWD);
}

void SURVIVAL_FmStopped(void)
{
    if (!sFmBgScan)
        return;

    sFmBgScan = false;
    if (gScanStateDir != SCAN_OFF)
        CHFRSCANNER_Stop();
}

bool SURVIVAL_FmBgScanning(void)
{
#ifdef ENABLE_FMRADIO
    return sFmBgScan && gFmRadioMode && gScanStateDir != SCAN_OFF;
#else
    return false;
#endif
}

void SURVIVAL_FmBgFound(void)
{
    // activity on a scanned channel: leave FM for good and stay on that channel,
    // the user goes back to FM by hand (F+0 or M)
    sFmBgScan    = false;
    sStopOnRxEnd = true;
#ifdef ENABLE_FMRADIO
    FM_TurnOff();
#endif
    // switch to the main screen right away (gRequestDisplayScreen is only
    // handled after a key press); the switch also ends the scan, keep the channel
    gEeprom.MrChannel[gEeprom.RX_VFO] = gRxVfo->CHANNEL_SAVE;
    gRequestSaveVFO = true;
    GUI_SelectNextDisplay(DISPLAY_MAIN);
}

uint8_t SURVIVAL_ScanResumeMode(void)
{
    return sStopOnRxEnd ? 0 : gEeprom.SCAN_RESUME_MODE;   // 0 = STOP
}

#endif
