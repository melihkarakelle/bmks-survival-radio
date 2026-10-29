/* BMKs Survival Radio: status line and main screen (UI v2, design/ui_v2/UI_DESIGN.md)
 *
 * Single receiver layout, one meaning per row:
 *   status  screen title (PMR446, AIR BAND, FM RADIO, MENU ...), F or lock icon, voltage, battery
 *   R0      only while something happens: inverted SCAN UP / RX + signal bar / TX; PRI on the priority channel
 *   R1-R2   channel name, or the scan range of AIR BAND / CUSTOM (big font)
 *   R3-R4   frequency, every digit big
 *   R5      power / bandwidth / squelch, or RX ONLY; bands show modulation and step
 *   R6      the keys that matter right now
 */

#ifdef ENABLE_SURVIVAL

#include <string.h>

#include "app/chFrScanner.h"
#ifdef ENABLE_FMRADIO
    #include "app/fm.h"
#endif
#include "app/survival.h"
#include "driver/bk4819.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "frequencies.h"
#include "functions.h"
#include "helper/battery.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/helper.h"
#include "ui/inputbox.h"
#include "ui/main.h"
#include "ui/status.h"
#include "ui/ui.h"

static void InvertRow(uint8_t *row)
{
    for (uint8_t i = 0; i < 128; i++)
        row[i] ^= 0xFF;
}

// ------------------------------------------------------------------ status line

static const char *Title(void)
{
    if (gScreenToDisplay == DISPLAY_MENU)
        return "MENU";
#ifdef ENABLE_FMRADIO
    if (gScreenToDisplay == DISPLAY_FM)
        return "FM RADIO";
#endif
    const uint8_t mode = SURVIVAL_GetMode();
    return (mode == SURV_MODE_AIR) ? "AIR BAND" : gSubMenu_SRV_MODE[mode];
}

static const uint8_t LOCK_ICON[7] = {0x78, 0x7E, 0x79, 0x49, 0x79, 0x7E, 0x78};

void UI_DisplayStatus(void)
{
    char     String[8];
    uint8_t *line = gStatusLine;

    memset(gStatusLine, 0, sizeof(gStatusLine));

    UI_PrintStringSmallBufferBold(Title(), line);

    if (gWasFKeyPressed)
        UI_PrintStringSmallBufferBold("F", line + 59);
    else if (gEeprom.KEY_LOCK)
        memcpy(line + 61, LOCK_ICON, sizeof(LOCK_ICON));

    // voltage and a three segment battery, full status line height
    sprintf(String, "%u.%u", gBatteryVoltageAverage / 100, (gBatteryVoltageAverage / 10) % 10);
    UI_PrintStringSmallBufferNormal(String, line + 84);

    const uint8_t percent  = BATTERY_VoltsToPercent(gBatteryVoltageAverage);
    const uint8_t segments = (percent > 66) ? 3 : (percent > 33) ? 2 : (percent > 5) ? 1 : 0;
    line[107] = 0x7F;
    for (uint8_t x = 108; x < 125; x++)
        line[x] = 0x41;
    line[125] = 0x7F;
    line[126] = 0x1C;
    for (uint8_t s = 0; s < segments; s++)
        for (uint8_t x = 0; x < 4; x++)
            line[109 + s * 5 + x] = 0x7F;
    if (gChargingWithTypeC)
        line[117] ^= 0x1C;

    ST7565_BlitStatusLine();
}

// ------------------------------------------------------------------ key help (F tap)

// key in capitals, then > and what it does in normal case; 18 characters per row
static const char HELP_MAIN[] = "0>FM radio\0"         "4>Heard list\0"      "5>Prio  7>Favorite\0" "8>Skip in scan\0"   "UP/DN>Squelch";
static const char HELP_FM[]   = "0>Exit FM\0"          "3>Saved / tune\0"    "*>Find all FM\0"      "HOLD M>Save\0"      "UP/DN>Tune";
static const char HELP_KEYS[] = "M>Next group\0"       "HOLD M>Menu\0"       "*>Scan / stop\0"      "HOLD UP/DN>Scan\0"  "HOLD F>Key lock";
static const char HELP_SIDE[] = "SIDE1>Light, blink\0" "      Strobe, SOS\0" "HOLD SIDE1>Monitor\0" "SIDE2>FM radio\0"   "HOLD SIDE2>Scan";

static const char *const HELP_TITLE[] = {"F + KEY      1/3", "KEYS         2/3", "SIDE KEYS    3/3"};

// drawn instead of the main / FM screen while the F prefix is active
bool UI_DisplayHelp(bool fm)
{
    if (!gWasFKeyPressed || gInputBoxIndex > 0)
        return false;

    const uint8_t page = SURVIVAL_HelpPage();
    const char   *text = (page == 1) ? HELP_KEYS : (page == 2) ? HELP_SIDE : fm ? HELP_FM : HELP_MAIN;

    UI_DisplayClear();
    UI_PrintStringSmallBold(HELP_TITLE[page], 0, 0, 0);
    InvertRow(gFrameBuffer[0]);
    for (uint8_t row = 1; row <= 5; row++) {
        UI_PrintStringSmallNormal(text, 0, 0, row);
        text += strlen(text) + 1;
    }
    UI_PrintStringSmallNormal("M>Next  EXIT>Back", 0, 0, 6);
    ST7565_BlitFullScreen();
    return true;
}

// ------------------------------------------------------------------ main screen

// called every 500 ms while receiving: the signal bar lives in R0 of the main screen
void DisplayRSSIBar(const bool now)
{
    (void)now;
    gUpdateDisplay = true;
}

static const char *ActivityWord(void)
{
    if (gCurrentFunction == FUNCTION_TRANSMIT)
        return "TX";
    if (gCurrentFunction == FUNCTION_MONITOR)
        return "MON";
    if (FUNCTION_IsRx())
        return "RX";
    if (gScanStateDir != SCAN_OFF)
        return (gScanStateDir == SCAN_FWD) ? "SCAN UP" : "SCAN DN";
    return NULL;
}

static void PowerText(char *s, uint8_t power)
{
    if (power >= OUTPUT_POWER_LOW1 && power <= OUTPUT_POWER_LOW5)
        sprintf(s, "LOW%u", power - OUTPUT_POWER_LOW1 + 1);
    else
        strcpy(s, (power == OUTPUT_POWER_MID) ? "MID" : (power == OUTPUT_POWER_HIGH) ? "HIGH" : "USER");
}

static void StepText(char *s, uint16_t step)   // 10 Hz units
{
    if (step % 100 == 0)
        sprintf(s, "%uK", step / 100);
    else if (step % 10 == 0)
        sprintf(s, "%u.%uK", step / 100, (step / 10) % 10);
    else
        sprintf(s, "%u.%02uK", step / 100, step % 100);
}

static const char *VfoStateText(VfoState_t state)
{
    switch (state) {
        case VFO_STATE_BUSY:         return "CHANNEL BUSY";
        case VFO_STATE_BAT_LOW:      return "BATTERY LOW";
        case VFO_STATE_TX_DISABLE:   return "TX BLOCKED";
        case VFO_STATE_TIMEOUT:      return "TX TIME LIMIT";
        case VFO_STATE_VOLTAGE_HIGH: return "VOLTAGE HIGH";
        default:                     return NULL;
    }
}

void UI_DisplayMain(void)
{
    char              String[22];
    char              Word[8];
    const uint8_t     vfo = gEeprom.TX_VFO;
    const VFO_Info_t *v   = &gEeprom.VfoInfo[vfo];
    const uint8_t     ch  = gEeprom.ScreenChannel[vfo];
    const bool        mr  = IS_MR_CHANNEL(ch);
    const bool        tx  = gCurrentFunction == FUNCTION_TRANSMIT;

    UI_DisplayClear();

    // the title follows the mode, whichever way the mode was changed
    static const char *shownTitle;
    if (Title() != shownTitle) {
        shownTitle    = Title();
        gUpdateStatus = true;
    }

    if (UI_DisplayHelp(false))
        return;

    if (gLowBattery && !gLowBatteryConfirmed) {
        UI_DisplayPopup("LOW BATTERY");
        ST7565_BlitFullScreen();
        return;
    }

    // R0: activity bar (the screen title is in the status line), PRI
    const char *act = ActivityWord();
    if (act) {
        UI_PrintStringSmallBold(act, 0, 0, 0);
        if (FUNCTION_IsRx() || gCurrentFunction == FUNCTION_MONITOR) {
            // signal strength x=24..100: -125 dBm empty, -65 dBm full
            int16_t width = (BK4819_GetRSSI_dBm() + 125) * 13 / 10;
            if (width < 1)
                width = 1;
            if (width > 76)
                width = 76;
            for (uint8_t i = 0; i < width; i++)
                gFrameBuffer[0][24 + i] = 0x3C;
        }
    }
    if (mr && ch == gSurvivalPrio)
        UI_PrintStringSmallBold("PRI", 105, 0, 0);
    if (!act && SURVIVAL_IsSkipped(v->pRX->Frequency))
        UI_PrintStringSmallBold("SKIP", 70, 0, 0);
    if (act)
        InvertRow(gFrameBuffer[0]);

    // R1-R2: name
    if (gInputBoxIndex > 0 && mr) {
        sprintf(String, "CH %.3s", INPUTBOX_GetAscii());
    } else if (mr) {
        SETTINGS_FetchChannelName(String, ch);
        if (String[0] == 0)
            sprintf(String, "CH %u", ch + 1);
    } else {
        uint32_t lo, hi;
        SURVIVAL_VfoRange(ch, &lo, &hi);
        sprintf(String, "%u.%03u-%u.%03u", lo / 100000, (lo / 100) % 1000, hi / 100000, (hi / 100) % 1000);
    }
    UI_PrintString(String, 0, 0, 1, 8);

    // R3-R4: frequency, all digits big (the TX frequency while sending, duplex included)
    const uint32_t f = tx ? v->pTX->Frequency : v->pRX->Frequency;
    if (gInputBoxIndex > 0 && !mr) {
        const char *ascii = INPUTBOX_GetAscii();
        sprintf(String, "%.3s.%.5s", ascii, ascii + 3);
    } else {
        sprintf(String, "%3u.%05u", f / 100000, f % 100000);
        if (!mr)
            SURVIVAL_MaskScanDigits(String);
    }
    if (f < _1GHz_in_KHz)
        UI_DisplayFrequency(String, 0, 3, false);
    else
        UI_PrintString(String, 0, 0, 3, 8);

    // R5: what this channel does
    const bool txOk = v->Modulation == MODULATION_FM && TX_freq_check(v->pTX->Frequency) == 0;
    if (!mr) {
        StepText(Word, v->StepFrequency);
        sprintf(String, "%s %s SQL%u", gModulationStr[v->Modulation], Word, gEeprom.SQUELCH_LEVEL);
    } else if (!txOk) {
        sprintf(String, "RX ONLY SQL%u", gEeprom.SQUELCH_LEVEL);
    } else {
        PowerText(Word, v->OUTPUT_POWER);
        sprintf(String, "%s %s SQL%u", Word, v->CHANNEL_BANDWIDTH ? "NAR" : "WIDE", gEeprom.SQUELCH_LEVEL);
    }
    if (v->TX_OFFSET_FREQUENCY_DIRECTION != TX_OFFSET_FREQUENCY_DIRECTION_OFF)
        strcat(String, " DUP");
    if (v->pRX->CodeType != CODE_TYPE_OFF || v->pTX->CodeType != CODE_TYPE_OFF)
        strcat(String, " CT");
    UI_PrintStringSmallNormal(String, 0, 0, 5);

    // R6: the keys that matter now
    const char *hint = VfoStateText(VfoState[vfo]);
    if (hint == NULL) {
        if (gEeprom.KEY_LOCK)
            hint = "HOLD F:UNLOCK";
        else if (tx)
            hint = "RELEASE PTT:STOP";
        else if (gScanStateDir != SCAN_OFF)
            hint = "*:STOP UP/DN:STOP";
        else
            hint = "HOLD M:MENU *:SCAN";
    }
    UI_PrintStringSmallNormal(hint, 0, 0, 6);

    ST7565_BlitFullScreen();
}

#endif
