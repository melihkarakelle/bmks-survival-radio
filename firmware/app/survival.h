/* BMKs Survival Radio
 *
 * Modes: fixed channel groups (PMR, FRS, marine, air, ham, CB) stored as
 * memory channels, FM broadcast, and CUSTOM (free frequency / VFO).
 * Side key 2 or the "Group" menu selects the mode; scan and up/down stay
 * inside the current group. TX is allowed per band (menu TxPMR..TxCB).
 */

#ifndef APP_SURVIVAL_H
#define APP_SURVIVAL_H

#ifdef ENABLE_SURVIVAL

#include <stdbool.h>
#include <stdint.h>

#include "driver/keyboard.h"

enum {
    SURV_MODE_PMR = 0,
    SURV_MODE_FRS,
    SURV_MODE_MARINE,
    SURV_MODE_AIR,
    SURV_MODE_HAM,
    SURV_MODE_CB,
    SURV_MODE_FM,
    SURV_MODE_CUSTOM,
    SURV_MODE_FAV,      // own channels after the groups (ChSave / F+7)
    SURV_MODE_COUNT
};

// TX permission bits, one per transmit capable band
enum {
    SURV_TX_PMR = 0,
    SURV_TX_FRS,
    SURV_TX_MARINE,
    SURV_TX_HAM,
    SURV_TX_CB,
    SURV_TX_ALL,      // everything except the blocked ranges, overrides the bits above
    SURV_TX_COUNT
};

extern const char gSubMenu_SRV_MODE[SURV_MODE_COUNT][9];
extern uint8_t    gSurvivalTxMask;
extern uint8_t    gSurvivalFmScan;        // FM background scan: 0 off, 1..6 = group (SURV_MODE_* + 1), 7 = last mode
#define SURV_FMSCAN_LAST (SURV_MODE_FM + 1)
extern uint32_t   gSurvivalCustomStart;   // CUSTOM mode scan range, 10 Hz units
extern uint32_t   gSurvivalCustomEnd;
extern uint8_t    gSurvivalPrio;          // priority channel, 0xFF = none

void    SURVIVAL_Init(void);
void    SURVIVAL_EraseMarker(void);
void    SURVIVAL_SaveSettings(void);
void    SURVIVAL_SaveCustomRange(void);
void    SURVIVAL_ApplyScanRange(void);
bool    SURVIVAL_GetGroupRange(uint8_t channel, uint8_t *first, uint8_t *last);
bool    SURVIVAL_IsFavChannel(uint8_t channel);
uint8_t SURVIVAL_FindNextChannel(uint8_t Channel, int8_t Direction, bool bCheckScanList, uint8_t VFO);
uint8_t SURVIVAL_GetMode(void);
void    SURVIVAL_SetMode(uint8_t mode);
bool    SURVIVAL_TxAllowed(uint32_t frequency);
void    ACTION_SurvivalGroup(void);
void    SURVIVAL_ToggleFm(void);

void    SURVIVAL_AddFav(void);            // F+7: current channel/frequency into / out of FAV
void    SURVIVAL_TogglePrio(void);        // F+5: current channel as priority channel
void    SURVIVAL_LogHeard(void);          // a transmission was received
void    SURVIVAL_TimeSlice500ms(void);
void    SURVIVAL_ShowLog(void);           // F+4: recently heard list
void    SURVIVAL_LogKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);
void    SURVIVAL_DisplayLog(void);
bool    SURVIVAL_EcoOn(void);
void    SURVIVAL_SetEco(bool on);
void    SURVIVAL_MaskScanDigits(char *freq);
const char *SURVIVAL_VfoLabel(uint8_t channel);   // "AIR BAND" or "CUSTOM" for the VFO modes
void    SURVIVAL_VfoRange(uint8_t channel, uint32_t *lower, uint32_t *upper);   // scan range of a VFO mode

// FM radio with a background scan of one group on the BK4819
void    SURVIVAL_FmStarted(void);
void    SURVIVAL_FmStopped(void);
bool    SURVIVAL_FmBgScanning(void);
void    SURVIVAL_FmBgFound(void);
uint8_t SURVIVAL_ScanResumeMode(void);

#endif

#endif
