/*******************************************************************************
 AshDome Dome INDI Driver

 Copyright(c) 2019 Mike Rosseel. All rights reserved.

 based on:

 ScopeDome INDI driver by Jarno Paananen

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Library General Public
 License version 2 as published by the Free Software Foundation.
 .
 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Library General Public License for more details.
 .
 You should have received a copy of the GNU Library General Public License
 along with this library; see the file COPYING.LIB.  If not, write to
 the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 Boston, MA 02110-1301, USA.
*******************************************************************************/

#include "ashdome_dome.h"

bool AshDomeSim::detect()
{
    return true;
}

int AshDomeSim::writeBuf(AshDomeCommand cmd, uint8_t len, uint8_t *buff)
{
    (void)len;
    (void)buff;
    int err = 0;
    switch (cmd)
    {
        default:
            err = FUNCTION_NOT_SUPPORTED_BY_FIRMWARE;
            break;
    }
    lastCmd = cmd;
    return err;
}

int AshDomeSim::write(AshDomeCommand cmd)
{
    int err = 0;
    switch (cmd)
    {
        default:
            err = FUNCTION_NOT_SUPPORTED_BY_FIRMWARE;
            break;
    }
    lastCmd = cmd;
    return err;
}

int AshDomeSim::readBuf(AshDomeCommand &cmd, uint8_t len, uint8_t *buff)
{
    (void)len;
    (void)buff;
    int err                   = 0;
//    int BytesToRead           = len + 4;
//    uint8_t cbuf[BytesToRead] = { 0 };

    cmd = lastCmd;
    return err;
}

int AshDomeSim::read(AshDomeCommand &cmd)
{
    int err         = 0;
//    uint8_t cbuf[4] = { 0 };

    cmd = lastCmd;
    return err;
}
