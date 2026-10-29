/* BMKs Survival Radio: status line and main screen (UI v2, design/ui_v2/UI_DESIGN.md)
 *
 * Single receiver layout, one meaning per row:
 *   status  state word (READY / SCAN UP / RX / TX ...), LOCK or RSSI bar, voltage, battery
 *   R0      group (inverted while scanning, receiving or sending), PRI on the priority channel
 *   R1-R2   channel name, AIR BAND or CUSTOM (big font)
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

static const char *StateWord(void)
{
    if (gLowBattery)
        return "LOW BAT";
    if (gCurrentFunction == FUNCTION_TRANSMIT)
        return "TX";
    if (gCurrentFunction == FUNCTION_MONITOR)
        return "MONITOR";
    if (FUNCTION_IsRx())
        return "RX";
    if (gScanStateDir != SCAN_OFF)
        return (gScanStateDir == SCAN_FWD) ? "SCAN UP" : "SCAN DN";
    if (gWasFKeyPressed)
        return "F";
#ifdef ENABLE_FMRADIO
    if (gFmRadioMode)
        return "FM";
#endif
    if (gScreenToDisplay == DISPLAY_MENU)
        return "MENU";
    return "READY";
}

void UI_DisplayStatus(void)
{
    char     String[8];
    uint8_t *line = gStatusLine;

    memset(gStatusLine, 0, sizeof(gStatusLine));

    UI_PrintStringSmallBufferBold(StateWord(), line);

    if (gEeprom.KEY_LOCK) {
        UI_PrintStringSmallBufferNormal("LOCK", line + 49);
    } else if (FUNCTION_IsRx()) {
        // signal strength bar x=49..76: -125 dBm empty, -70 dBm full
        int16_t dbm   = BK4819_GetRSSI_dBm();
        int16_t width = (dbm + 125) / 2;
        if (width < 1)
            width = 1;
        if (width > 28)
            width = 28;
        for (uint8_t i = 0; i < width; i++)
            line[49 + i] = 0x7E;
    }

    // voltage and a three segment battery
    sprintf(String, "%u.%u", gBatteryVoltageAverage / 100, (gBatteryVoltageAverage / 10) % 10);
    UI_PrintStringSmallBufferNormal(String, line + 84);

    const uint8_t percent  = BATTERY_VoltsToPercent(gBatteryVoltageAverage);
    const uint8_t segments = (percent > 66) ? 3 : (percent > 33) ? 2 : (percent > 5) ? 1 : 0;
    line[107] = 0x7E;
    for (uint8_t x = 108; x < 125; x++)
        line[x] = 0x42;
    line[125] = 0x7E;
    line[126] = 0x3C;
    for (uint8_t s = 0; s < segments; s++)
        for (uint8_t x = 0; x < 4; x++)
            line[109 + s * 5 + x] = 0x7E;
    if (gChargingWithTypeC)
        line[117] ^= 0x18;

    ST7565_BlitStatusLine();
}

// ------------------------------------------------------------------ main screen

// the main screen redraws on its own; this only refreshes the RSSI bar in the status line
void DisplayRSSIBar(const bool now)
{
    (void)now;
    gUpdateStatus = true;
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

    if (gLowBattery && !gLowBatteryConfirmed) {
        UI_DisplayPopup("LOW BATTERY");
        ST7565_BlitFullScreen();
        return;
    }

    // R0: group, PRI
    UI_PrintStringSmallBold(gSubMenu_SRV_MODE[SURVIVAL_GetMode()], 0, 0, 0);
    if (mr && ch == gSurvivalPrio)
        UI_PrintStringSmallBold("PRI", 105, 0, 0);
    if (tx || FUNCTION_IsRx() || gScanStateDir != SCAN_OFF)
        InvertRow(gFrameBuffer[0]);

    // R1-R2: name
    if (gInputBoxIndex > 0 && mr) {
        sprintf(String, "CH %.3s", INPUTBOX_GetAscii());
    } else if (mr) {
        SETTINGS_FetchChannelName(String, ch);
        if (String[0] == 0)
            sprintf(String, "CH %u", ch + 1);
    } else {
        strcpy(String, SURVIVAL_VfoLabel(ch));
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
