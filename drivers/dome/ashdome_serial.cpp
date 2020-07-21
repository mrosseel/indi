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
#include "indicom.h"

#include <termios.h>

// for usleep
#include <unistd.h>

#define ASHDOME_TIMEOUT 10
#define ASHDOME_MAX_READS 2

static const uint8_t header = 0xaa;

bool AshDomeSerial::detect()
{
    LOG_DEBUG("AshDomeSerial::detect");

    uint8_t reply;
    int rc = -1;
    AshDomeCommand cmd;
    // LOGF_INFO("Detect with cmd %d", Ping);
    rc = write(Ping);
    // LOGF_INFO("write rc: %d", rc);
    usleep((useconds_t)1000000); // 1s

    rc = readBuf(cmd, 1, (uint8_t *)&reply);
    return true;
    // LOGF_INFO("read rc: %d, cmd %d, reply 0x%x", rc, reply);
    bool answer = reply == Ping;
    LOGF_INFO("AshDomeSerial::detect -> reply:%x, connectiontest:%x, %i", reply, Ping, answer);
    return answer;
}


int AshDomeSerial::writeBuf(AshDomeCommand cmd, uint8_t len, uint8_t *buff)
{
    int BytesToWrite   = len;
    int BytesWritten   = 0;
    int nbytes_written = 0, rc = -1;
    char errstr[MAXRBUF];
    uint8_t cbuf[BytesToWrite];
    cbuf[0] = cmd;
    for (int i = 1; i < len; i++)
    {
        cbuf[i] = buff[i];
    }
    tcflush(PortFD, TCIOFLUSH);

    prevcmd = cmd;

    // Write buffer
    if ((rc = tty_write(PortFD, (const char *)cbuf, sizeof(cbuf), &nbytes_written)) != TTY_OK)
    {
        tty_error_msg(rc, errstr, MAXRBUF);
        LOGF_ERROR("Error writing command: %s. Cmd %d, portFD: %d", errstr, cmd, PortFD);
    }
    return rc;
}

int AshDomeSerial::write(AshDomeCommand cmd)
{
    int nbytes_written = 0, rc = -1;
    uint8_t cbuf[1];
    char errstr[MAXRBUF];

    tcflush(PortFD, TCIOFLUSH);
    cbuf[0] = cmd;

    prevcmd = cmd;

    // Write buffer
    LOGF_DEBUG("write cmd: 0x%x", cbuf[0]);
    if ((rc = tty_write(PortFD, (const char *)cbuf, sizeof(cbuf), &nbytes_written)) != TTY_OK)
    {
        tty_error_msg(rc, errstr, MAXRBUF);
        LOGF_ERROR("Error writing command: %s. Cmd: 0x%x, port: %d", errstr, cmd, PortFD);
    }
    return rc;
}

int AshDomeSerial::readBuf(AshDomeCommand &cmd, uint8_t len, uint8_t *buff)
{
    int nbytes_read = 0, rc = -1;
    int BytesToRead = len;
    char errstr[MAXRBUF];
    LOGF_DEBUG("Start %s. Cmd: %d, port:%d", errstr, prevcmd, PortFD);
    tty_set_debug(1);
    // Read buffer
    if ((rc = tty_read(PortFD, (char *)buff, len, ASHDOME_TIMEOUT, &nbytes_read)) != TTY_OK)
    {
        tty_error_msg(rc, errstr, MAXRBUF);
        LOGF_ERROR("Error reading: %s. Cmd: %d", errstr, prevcmd);
        // return rc;
    }
    //LOGF_INFO("bytes read: %d, buff: %s", nbytes_read, buff[len]);
    return rc;
}

int AshDomeSerial::read(AshDomeCommand &cmd)
{
    int nbytes_read = 0, rc = -1;
    int err         = 0;
    uint8_t cbuf[4] = { 0 };
    char errstr[MAXRBUF];
    LOGF_INFO("Reading: Cmd %d", cmd);
    // Read buffer
    if ((rc = tty_read(PortFD, (char *)cbuf, sizeof(cbuf), ASHDOME_TIMEOUT, &nbytes_read)) != TTY_OK)
    {
        tty_error_msg(rc, errstr, MAXRBUF);
        LOGF_ERROR("Error reading: %s. Cmd %d", errstr, prevcmd);
        return rc;
    }

    switch (cmd)
    {
        case MotionConflict:
            LOG_ERROR("read motion conflict");
            err = MOTION_CONFLICT;
            break;

        case FunctionNotSupported:
            LOG_ERROR("read function not supported");
            err = FUNCTION_NOT_SUPPORTED;
            break;

        case ParamError:
            LOG_ERROR("read param error");
            err = PARAM_ERROR;
            break;
        default:
            break;
    }
    return err;
}
