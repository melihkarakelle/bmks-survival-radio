/* BMKs Survival Radio
 *
 * Fixed channel groups (PMR, FRS, marine, air, ham, CB) stored as memory
 * channels, plus FM broadcast. Side key 2 cycles through the groups; scan
 * and up/down stay inside the current group.
 */

#ifndef APP_SURVIVAL_H
#define APP_SURVIVAL_H

#ifdef ENABLE_SURVIVAL

#include <stdbool.h>
#include <stdint.h>

void SURVIVAL_Init(void);
void SURVIVAL_EraseMarker(void);
bool SURVIVAL_GetGroupRange(uint8_t channel, uint8_t *first, uint8_t *last);
uint8_t SURVIVAL_FindNextChannel(uint8_t Channel, int8_t Direction, bool bCheckScanList, uint8_t VFO);
void ACTION_SurvivalGroup(void);

#endif

#endif
