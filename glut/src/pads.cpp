/*	  Copyright (C) 2000,2001,2002  Sony Computer Entertainment America

       	  This file is subject to the terms and conditions of the GNU Lesser
	  General Public License Version 2.1. See the file "COPYING" in the
	  main directory of this archive for more details.                             */

/********************************************
 * includes
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loadfile.h"
#include "sifrpc.h"

#include "pads.h"
#include "ps2s/math.h"

/********************************************
 * constants
 */

#define kPad0 0
#define kPad1 1

#define kPort0 0
#define kSlot0 0

#define kPadModeStandard 0x4
#define kPadModeAnalog 0x7

#define kPadSetLockModeUnchanged 0
#define kPadSetLockModeLock 3
#define kPadSetLockModeUnlock 1

#define kStickMaxRadius 120
#define kStickDeadRadius 25

/********************************************
 * globals
 */

CPad Pad0(kPad0);
CPad Pad1(kPad1);

/********************************************
 * Pads
 */

void Pads::Init(void)
{
    // open the pads.. this should be elsewhere..
    SifInitRpc(0);

    /* load sio2man.irx */
    if (SifLoadModule("rom0:SIO2MAN", 0, NULL) < 0) {
        printf("Can't load module sio2man\n");
        exit(0);
    }
    /* load padman.irx */
    if (SifLoadModule("rom0:PADMAN", 0, NULL) < 0) {
        printf("Can't load module padman\n");
        exit(0);
    }

    padInit(0); // "must be zero"

    if (!Pad0.Open()) {
        printf("Couldn't open Pad0.\n");
        exit(-1);
    }
}

void Pads::Read(void)
{
    Pad0.Read();
}

/********************************************
 * CPad
 */

CPad::CPad(unsigned int port)
    : uiPort(port)
    , bPadModeSet(false)
{
    memset(&CurStatus, 0, sizeof(tPadStatus));
    memset(&LastStatus, 0, sizeof(tPadStatus));

    // All buttons released
    CurStatus.buttons = 0xffff;
    LastStatus.buttons = 0xffff;
}

bool CPad::Open(void)
{
    // slot is only for use with multitap
    return padPortOpen(uiPort, kSlot0, DmaBuffer);
}

void CPad::Read(void)
{
    int32_t padState = padGetState(kPort0, kSlot0);
    if (padState != PAD_STATE_STABLE)
        return;

    if (!bPadModeSet) {
        // who knows what the 1 parameter is..  a return val of 1 indicates that the request is
        // being processed
        if (padSetMainMode(uiPort, kSlot0, 1, kPadSetLockModeUnlock) == 1)
            bPadModeSet = true;
    } else {
        tPadStatus padStatus;
        padRead(uiPort, kSlot0, (padButtonStatus*)&padStatus);

        if (padStatus.success == 0) { // 0 indicates success
            LastStatus           = CurStatus;
            padStatus.rightStick = CurStatus.rightStick;
            padStatus.leftStick  = CurStatus.leftStick;
            CurStatus            = padStatus;

            //	 int32_t id = padInfoMode( uiPort, kSlot0, PAD_MODECURID, 0 );
            //	 if ( id == kPadModeStandard || id == kPadModeAnalog ) {
            //				// flip the sense of the bit field (1 = pressed)
            //	    CurStatus.buttons ^= 0xffff;
            //	 }

            // sticks
            if (WasPushed(Pads::kRightStickButton)) {
                CurStatus.leftStick.isCentered  = false;
                CurStatus.rightStick.isCentered = false;
            }
            CurStatus.leftStick.xVal  = CurStatus.l3h;
            CurStatus.leftStick.yVal  = CurStatus.l3v;
            CurStatus.rightStick.xVal = CurStatus.r3h;
            CurStatus.rightStick.yVal = CurStatus.r3v;
            UpdateStick(&CurStatus.leftStick, &LastStatus.leftStick);
            UpdateStick(&CurStatus.rightStick, &LastStatus.rightStick);
        }
    }
}

bool CPad::UpdateStick(tStickData* stickCur, tStickData* stickLast)
{
    int8_t temp;
    bool isChanged = false;

    using namespace Math;

    if (!stickCur->isCentered) {
        stickCur->xCenter    = stickCur->xVal;
        stickCur->yCenter    = stickCur->yVal;
        stickCur->xPos       = 0.0f;
        stickCur->yPos       = 0.0f;
        stickCur->isCentered = true;

        isChanged = true;
    } else {
        if (!FuzzyEqualsi(stickCur->xVal, stickCur->xCenter, kStickDeadRadius)) {
            // stick is not inside the dead zone
            temp           = ((stickCur->xVal > stickCur->xCenter) ? -kStickDeadRadius : kStickDeadRadius);
            stickCur->xPos = (float)(stickCur->xVal - stickCur->xCenter + temp) / (float)kStickMaxRadius;
            isChanged      = true;
        } else {
            // stick is inside the dead zone
            stickCur->xPos = 0.0f;
            // if it just entered the dead zone, send out one last event
            if (!FuzzyEqualsi(stickLast->xVal, stickCur->xCenter, kStickDeadRadius))
                isChanged = true;
        }
        if (!FuzzyEqualsi(stickCur->yVal, stickCur->yCenter, kStickDeadRadius)) {
            // stick is not inside the dead zone
            temp           = (stickCur->yVal > stickCur->yCenter) ? kStickDeadRadius : -kStickDeadRadius;
            stickCur->yPos = (float)(stickCur->yCenter - stickCur->yVal + temp) / (float)kStickMaxRadius;
            isChanged      = true;
        } else {
            // stick is inside the dead zone
            stickCur->yPos = 0.0f;
            // if it just entered the dead zone, send out one last event
            if (!FuzzyEqualsi(stickLast->yVal, stickCur->yCenter, kStickDeadRadius))
                isChanged = true;
        }

        stickCur->xPos = Clamp(stickCur->xPos, -1.0f, 1.0f);
        stickCur->yPos = Clamp(stickCur->yPos, -1.0f, 1.0f);
    }

    return isChanged;
}

bool CPad::IsDown(tPadStatus status, unsigned int button)
{
    return !IsUp(status, button);
}

bool CPad::IsUp(tPadStatus status, unsigned int button)
{
    return status.buttons & (1 << button);
}

bool CPad::IsDown(unsigned int button)
{
    return IsDown(CurStatus, button);
}

bool CPad::IsUp(unsigned int button)
{
    return IsUp(CurStatus, button);
}

bool CPad::WasPushed(unsigned int button)
{
    return IsUp(LastStatus, button) && IsDown(CurStatus, button);
}

bool CPad::WasReleased(unsigned int button)
{
    return IsDown(LastStatus, button) && IsUp(CurStatus, button);
}
