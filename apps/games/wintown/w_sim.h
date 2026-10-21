/* w_sim.h - Force-include for s_sim.c compilation only
 * Renames conflicting functions that WinTown reimplements
 */
#define _W_SIM_H

/* struct timeval of the original RandomlySeedRand: windows.h leaves winsock.h out
 * with WIN32_LEAN_AND_MEAN, which the NT build defines (2026-10) */
#include <winsock.h>

#define SimFrame _orig_SimFrame
#define Simulate _orig_Simulate
#define DoSimInit _orig_DoSimInit
#define MapScan _orig_MapScan
#define GetBoatDis _orig_GetBoatDis
#define DoMeltdown _orig_DoMeltdown
#define Rand _orig_Rand
#define Rand16 _orig_Rand16
#define Rand16Signed _orig_Rand16Signed
#define RandomlySeedRand _orig_RandomlySeedRand
#define SeedRand _orig_SeedRand

#define sim_rand rand
#define sim_srand srand

#define doAllGraphs()

static short NewMapFlags[16];
static short NewMap;
static short NewGraph;
