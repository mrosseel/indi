/*******************************************************************************
 AshDome Dome INDI Driver

 Copyright(c) 2019 Mike Rosseel. All rights reserved.

 based on:

 ScopeDome INDI driver by Jarno Paananen

 and

 Baader Planetarium Dome INDI Driver

 Copyright(c) 2014 Jasem Mutlaq. All rights reserved.

 Baader Dome INDI Driver

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

#include "connectionplugins/connectionserial.h"
#include "indicom.h"
#include "indilogger.h"
#include <bits/stdint-uintn.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <termios.h>
// https://linux.die.net/man/3/wordexp
#include <wordexp.h>
#include <unistd.h>

// added by Mike for stacktracing
#include <stdio.h>
#include <execinfo.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
// added by Mike for stacktracing

using namespace std;

// We declare an auto pointer to AshDome.
std::unique_ptr<AshDome> ashDome(new AshDome());

void ISPoll(void *p);

void ISGetProperties(const char *dev)
{
    ashDome->ISGetProperties(dev);
}

void ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    ashDome->ISNewSwitch(dev, name, states, names, n);
}

void ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n)
{
    ashDome->ISNewText(dev, name, texts, names, n);
}

void ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    ashDome->ISNewNumber(dev, name, values, names, n);
}

void ISNewBLOB(const char *dev, const char *name, int sizes[], int blobsizes[], char *blobs[], char *formats[],
               char *names[], int n)
{
    INDI_UNUSED(dev);
    INDI_UNUSED(name);
    INDI_UNUSED(sizes);
    INDI_UNUSED(blobsizes);
    INDI_UNUSED(blobs);
    INDI_UNUSED(formats);
    INDI_UNUSED(names);
    INDI_UNUSED(n);
}

void ISSnoopDevice(XMLEle *root)
{
    ashDome->ISSnoopDevice(root);
}

AshDome::AshDome()
{
    setVersion(1, 0);
    targetAz         = 0;
    m_ShutterState     = SHUTTER_UNKNOWN;
    simShutterStatus = SHUTTER_CLOSED; 

    status        = DOME_UNKNOWN;
    targetShutter = SHUTTER_CLOSE;

    // didn't work, tcp was still there ?
    // SetDomeConnection(CONNECTION_SERIAL | CONNECTION_NONE);
    SetDomeCapability(DOME_CAN_ABORT | DOME_CAN_ABS_MOVE | DOME_CAN_REL_MOVE | DOME_CAN_PARK | DOME_HAS_SHUTTER);

    stepsPerTurn = -1;

    LOG_INFO("In Startup.");
    // Load dome inertia table if present
    wordexp_t wexp;
    if (wordexp("~/.indi/AshDome_DomeInertia_Table.txt", &wexp, 0) == 0)
    {
        FILE *inertia = fopen(wexp.we_wordv[0], "r");
        if (inertia)
        {
            // skip UTF-8 marker bytes
            fseek(inertia, 3, SEEK_SET);
            char line[100];
            int lineNum = 0;
            while (fgets(line, sizeof(line), inertia))
            {
                int step, result;
                if (sscanf(line, "%d ;%d", &step, &result) != 2)
                {
                    sscanf(line, "%d;%d", &step, &result);
                }
                if (step == lineNum)
                {
                    inertiaTable.push_back(result);
                }
                lineNum++;
            }
            fclose(inertia);
            LOGF_INFO("Read inertia file %s", wexp.we_wordv[0]);
        }
        else
        {
            LOG_INFO("Could not read inertia file, please generate one with Windows driver setup and copy to "
                     "~/.indi/AshDome_DomeInertia_Table.txt");
        }
    }
    wordfree(&wexp);
    LOG_INFO("Startup Done.");
}


bool AshDome::initProperties()
{
    LOG_INFO("Initproperties.");
    INDI::Dome::initProperties();

    IUFillNumber(&DomeHomePositionN[0], "DH_POSITION", "AZ (deg)", "%6.2f", 0.0, 360.0, 1.0, 0.0);
    IUFillNumberVector(&DomeHomePositionNP, DomeHomePositionN, 1, getDeviceName(), "DOME_HOME_POSITION",
                       "Home sensor position", SITE_TAB, IP_RW, 60, IPS_OK);

    IUFillSwitch(&ParkShutterS[0], "ON", "On", ISS_ON);
    IUFillSwitch(&ParkShutterS[1], "OFF", "Off", ISS_OFF);
    IUFillSwitchVector(&ParkShutterSP, ParkShutterS, 2, getDeviceName(), "PARK_SHUTTER", "Park controls shutter",
                       OPTIONS_TAB, IP_RW, ISR_1OFMANY, 0, IPS_OK);

    IUFillSwitch(&FindHomeS[0], "START", "Start", ISS_OFF);
    IUFillSwitchVector(&FindHomeSP, FindHomeS, 1, getDeviceName(), "FIND_HOME", "Find home", MAIN_CONTROL_TAB, IP_RW,
                       ISR_ATMOST1, 0, IPS_OK);

    IUFillSwitch(&DerotateS[0], "START", "Start", ISS_OFF);
    IUFillSwitchVector(&DerotateSP, DerotateS, 1, getDeviceName(), "DEROTATE", "Derotate", MAIN_CONTROL_TAB, IP_RW,
                       ISR_ATMOST1, 0, IPS_OK);

    IUFillSwitch(&PowerRelaysS[0], "CCD", "CCD", ISS_OFF);
    IUFillSwitch(&PowerRelaysS[1], "SCOPE", "Telescope", ISS_OFF);
    IUFillSwitch(&PowerRelaysS[2], "LIGHT", "Light", ISS_OFF);
    IUFillSwitch(&PowerRelaysS[3], "FAN", "Fan", ISS_OFF);
    IUFillSwitchVector(&PowerRelaysSP, PowerRelaysS, 4, getDeviceName(), "POWER_RELAYS", "Power relays",
                       MAIN_CONTROL_TAB, IP_RW, ISR_NOFMANY, 0, IPS_IDLE);

    IUFillSwitch(&RelaysS[0], "RELAY_1", "Relay 1 (reset)", ISS_OFF);
    IUFillSwitch(&RelaysS[1], "RELAY_2", "Relay 2 (heater)", ISS_OFF);
    IUFillSwitch(&RelaysS[2], "RELAY_3", "Relay 3", ISS_OFF);
    IUFillSwitch(&RelaysS[3], "RELAY_4", "Relay 4", ISS_OFF);
    IUFillSwitchVector(&RelaysSP, RelaysS, 4, getDeviceName(), "RELAYS", "Relays", MAIN_CONTROL_TAB, IP_RW, ISR_NOFMANY,
                       0, IPS_IDLE);

    IUFillSwitch(&AutoCloseS[0], "CLOUD", "Cloud sensor", ISS_OFF);
    IUFillSwitch(&AutoCloseS[1], "RAIN", "Rain sensor", ISS_OFF);
    IUFillSwitch(&AutoCloseS[2], "FREE", "Free input", ISS_OFF);
    IUFillSwitch(&AutoCloseS[3], "NO_POWER", "No power", ISS_OFF);
    IUFillSwitch(&AutoCloseS[4], "DOME_LOW", "Low dome battery", ISS_OFF);
    IUFillSwitch(&AutoCloseS[5], "SHUTTER_LOW", "Low shutter battery", ISS_OFF);
    IUFillSwitch(&AutoCloseS[6], "WEATHER", "Bad weather", ISS_OFF);
    IUFillSwitch(&AutoCloseS[7], "LOST_CONNECTION", "Lost connection", ISS_OFF);
    IUFillSwitchVector(&AutoCloseSP, AutoCloseS, 8, getDeviceName(), "AUTO_CLOSE", "Close shutter automatically",
                       SITE_TAB, IP_RW, ISR_NOFMANY, 0, IPS_IDLE);

    IUFillNumber(&EnvironmentSensorsN[0], "LINK_STRENGTH", "Shutter link strength", "%3.0f", 0.0, 100.0, 1.0, 0.0);
    IUFillNumber(&EnvironmentSensorsN[1], "SHUTTER_POWER", "Shutter internal power", "%2.2f", 0.0, 100.0, 1.0, 0.0);
    IUFillNumber(&EnvironmentSensorsN[2], "SHUTTER_BATTERY", "Shutter battery power", "%2.2f", 0.0, 100.0, 1.0, 0.0);
    IUFillNumber(&EnvironmentSensorsN[3], "CARD_POWER", "Card internal power", "%2.2f", 0.0, 100.0, 1.0, 0.0);
    IUFillNumber(&EnvironmentSensorsN[4], "CARD_BATTERY", "Card battery power", "%2.2f", 0.0, 100.0, 1.0, 0.0);
    IUFillNumber(&EnvironmentSensorsN[5], "TEMP_DOME_IN", "Temperature in dome", "%2.2f", -100.0, 100.0, 1.0, 0.0);
    IUFillNumber(&EnvironmentSensorsN[6], "TEMP_DOME_OUT", "Temperature outside dome", "%2.2f", -100.0, 100.0, 1.0,
                 0.0);
    IUFillNumber(&EnvironmentSensorsN[7], "TEMP_DOME_HUMIDITY", "Temperature humidity sensor", "%2.2f", -100.0, 100.0,
                 1.0, 0.0);
    IUFillNumber(&EnvironmentSensorsN[8], "HUMIDITY", "Humidity", "%3.2f", 0.0, 100.0, 1.0, 0.0);
    IUFillNumber(&EnvironmentSensorsN[9], "PRESSURE", "Pressure", "%4.1f", 0.0, 2000.0, 1.0, 0.0);
    IUFillNumber(&EnvironmentSensorsN[10], "DEW_POINT", "Dew point", "%2.2f", -100.0, 100.0, 1.0, 0.0);
    IUFillNumberVector(&EnvironmentSensorsNP, EnvironmentSensorsN, 11, getDeviceName(), "SCOPEDOME_SENSORS",
                       "Environment sensors", INFO_TAB, IP_RO, 60, IPS_IDLE);

    IUFillSwitch(&SensorsS[0], "AZ_COUNTER", "Az counter", ISS_OFF);
    IUFillSwitch(&SensorsS[1], "ROTATE_CCW", "Rotate CCW", ISS_OFF);
    IUFillSwitch(&SensorsS[2], "HOME", "Dome at home", ISS_OFF);
    IUFillSwitch(&SensorsS[3], "OPEN_1", "Shutter 1 open", ISS_OFF);
    IUFillSwitch(&SensorsS[4], "CLOSE_1", "Shutter 1 closed", ISS_OFF);
    IUFillSwitch(&SensorsS[5], "OPEN_2", "Shutter 2 open", ISS_OFF);
    IUFillSwitch(&SensorsS[6], "CLOSE_2", "Shutter 2 closed", ISS_OFF);
    IUFillSwitch(&SensorsS[7], "SCOPE_HOME", "Scope at home", ISS_OFF);
    IUFillSwitch(&SensorsS[8], "RAIN", "Rain sensor", ISS_OFF);
    IUFillSwitch(&SensorsS[9], "CLOUD", "Cloud sensor", ISS_OFF);
    IUFillSwitch(&SensorsS[10], "SAFE", "Observatory safe", ISS_OFF);
    IUFillSwitch(&SensorsS[11], "LINK", "Rotary link", ISS_OFF);
    IUFillSwitch(&SensorsS[12], "FREE", "Free input", ISS_OFF);
    IUFillSwitchVector(&SensorsSP, SensorsS, 13, getDeviceName(), "INPUTS", "Input sensors", INFO_TAB, IP_RO,
                       ISR_NOFMANY, 0, IPS_IDLE);

    IUFillNumber(&FirmwareVersionsN[0], "MAIN", "Main part", "%2.2f", 0.0, 99.0, 1.0, 0.0);
    IUFillNumber(&FirmwareVersionsN[1], "ROTARY", "Rotary part", "%2.1f", 0.0, 99.0, 1.0, 0.0);
    IUFillNumberVector(&FirmwareVersionsNP, FirmwareVersionsN, 2, getDeviceName(), "FIRMWARE_VERSION",
                       "Firmware versions", INFO_TAB, IP_RO, 60, IPS_IDLE);

    IUFillNumber(&StepsPerRevolutionN[0], "STEPS", "Steps per revolution", "%5.0f", 0.0, 99999.0, 1.0, 0.0);
    IUFillNumberVector(&StepsPerRevolutionNP, StepsPerRevolutionN, 1, getDeviceName(), "CALIBRATION_VALUES",
                       "Calibration values", SITE_TAB, IP_RO, 60, IPS_IDLE);



    IUFillSwitch(&EncoderResetS[0], "RESET", "Encoder reset", ISS_OFF);
    IUFillSwitchVector(&EncoderResetSP, EncoderResetS, 1, getDeviceName(), "RUN_ENCODER_RESET", "Run encoder reset",
                       SITE_TAB, IP_RW, ISR_ATMOST1, 0, IPS_OK);

    // IText PortT[1] {};
    // IUFillText(&PortT[0], "PORT", "Port", "/dev/serial0");
    // IUFillTextVector(&PortTP, PortT, 1, dev->getDeviceName(), INDI::SP::DEVICE_PORT, "Ports", CONNECTION_TAB, IP_RW, 60, IPS_IDLE);

    SetParkDataType(PARK_AZ);

    addAuxControls();
    /* addDebugControl(); */
    /* addSimulationControl(); */
    // addConfigurationControl();
    // addPollPeriodControl();
    // Set serial parameters
    serialConnection->setDefaultBaudRate(Connection::Serial::B_9600);
    serialConnection->setDefaultPort("/dev/serial0");
    // reconnect();

    LOG_INFO("INitproperties Done.");
    return true;
}

/************************************************************************************
 *
* ***********************************************************************************/
bool AshDome::SetupParms()
{
    LOG_INFO("SetupParams");
    targetAz = 0;
    stepsPerTurn = 4096;
    /* stepsPerTurn = 307200; */
    LOGF_INFO("Steps per turn read as %d", stepsPerTurn);
    StepsPerRevolutionN[0].value = stepsPerTurn;
    StepsPerRevolutionNP.s       = IPS_OK;
    IDSetNumber(&StepsPerRevolutionNP, nullptr);
    /* readS32(GetHomeSensorPosition, homePosition); */
    homePosition = 0; // TODO figure out the correct home position (az?)
    LOGF_INFO("Home position read as %d", homePosition);

    if (UpdatePosition())
        IDSetNumber(&DomeAbsPosNP, nullptr);


    if (UpdateShutterStatus())
        IDSetSwitch(&DomeShutterSP, nullptr);

    UpdateSensorStatus();
    UpdateRelayStatus();

    if (InitPark())
    {
        // If loading parking data is successful, we just set the default parking
        // values.
        SetAxis1ParkDefault(0);
    }
    else
    {
        // Otherwise, we set all parking data to default in case no parking data is
        // found.
        SetAxis1Park(0);
        SetAxis1ParkDefault(0);
    }

    /* tty_set_debug(0); */
    LOG_INFO("SetupParams");
    return true;
}

/************************************************************************************
 *
* ***********************************************************************************/
bool AshDome::Handshake()
{
    return Ack();
}

/************************************************************************************
 *
* ***********************************************************************************/
const char *AshDome::getDefaultName()
{
    return (const char *)"AshDome";
}

/************************************************************************************
 *
* ***********************************************************************************/
bool AshDome::updateProperties()
{
    INDI::Dome::updateProperties();

    if (isConnected())
    {
        defineSwitch(&FindHomeSP);
        defineSwitch(&DerotateSP);
        defineSwitch(&AutoCloseSP);
        defineSwitch(&PowerRelaysSP);
        defineSwitch(&RelaysSP);
        defineNumber(&DomeHomePositionNP);
        defineNumber(&EnvironmentSensorsNP);
        defineSwitch(&SensorsSP);
        defineSwitch(&ParkShutterSP);
        defineNumber(&StepsPerRevolutionNP);
        defineSwitch(&EncoderResetSP);
        defineNumber(&FirmwareVersionsNP);
        SetupParms();
    }
    else
    {
        deleteProperty(FindHomeSP.name);
        deleteProperty(DerotateSP.name);
        deleteProperty(PowerRelaysSP.name);
        deleteProperty(RelaysSP.name);
        deleteProperty(SensorsSP.name);
        deleteProperty(AutoCloseSP.name);
        deleteProperty(DomeHomePositionNP.name);
        deleteProperty(EnvironmentSensorsNP.name);
        deleteProperty(ParkShutterSP.name);
        deleteProperty(StepsPerRevolutionNP.name);
        deleteProperty(EncoderResetSP.name);
        deleteProperty(FirmwareVersionsNP.name);
    }

    return true;
}

/************************************************************************************
 *
* ***********************************************************************************/
bool AshDome::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    LOGF_INFO("New switch %s, %d, %s, %s, %s", names, n,states,name, dev);
    if (dev != nullptr && strcmp(dev, getDeviceName()) == 0)
    {
        if (strcmp(name, FindHomeSP.name) == 0)
        {
            if (status != DOME_HOMING)
            {
                LOG_INFO("Finding home sensor");
                status = DOME_HOMING;
                IUResetSwitch(&FindHomeSP);
                DomeAbsPosNP.s = IPS_BUSY;
                FindHomeSP.s   = IPS_BUSY;
                IDSetSwitch(&FindHomeSP, nullptr);
                writeCmd(FindHome);
            }
            return true;
        }

        if (strcmp(name, DerotateSP.name) == 0)
        {
            if (status != DOME_DEROTATING)
            {
                LOG_INFO("De-rotating started");
                status = DOME_DEROTATING;
                IUResetSwitch(&DerotateSP);
                DomeAbsPosNP.s = IPS_BUSY;
                DerotateSP.s   = IPS_BUSY;
                IDSetSwitch(&DerotateSP, nullptr);
            }
            return true;
        }

        if (strcmp(name, EncoderResetSP.name) == 0)
        {
            if (status != DOME_CALIBRATING)
            {
                LOG_INFO("Reset started");
                status = DOME_CALIBRATING;
                IUResetSwitch(&EncoderResetSP);
                DomeAbsPosNP.s       = IPS_BUSY;
                EncoderResetSP.s = IPS_BUSY;
                IDSetSwitch(&EncoderResetSP, nullptr);
                Reset();
            }
            return true;
        }

        if (strcmp(name, PowerRelaysSP.name) == 0)
        {
            IUUpdateSwitch(&PowerRelaysSP, states, names, n);
            setOutputState(OUT_CCD, PowerRelaysS[0].s);
            setOutputState(OUT_SCOPE, PowerRelaysS[1].s);
            setOutputState(OUT_LIGHT, PowerRelaysS[2].s);
            setOutputState(OUT_FAN, PowerRelaysS[3].s);
            IDSetSwitch(&PowerRelaysSP, nullptr);
            return true;
        }

        if (strcmp(name, RelaysSP.name) == 0)
        {
            IUUpdateSwitch(&RelaysSP, states, names, n);
            setOutputState(OUT_RELAY1, RelaysS[0].s);
            setOutputState(OUT_RELAY2, RelaysS[1].s);
            setOutputState(OUT_RELAY3, RelaysS[2].s);
            setOutputState(OUT_RELAY4, RelaysS[3].s);
            IDSetSwitch(&RelaysSP, nullptr);
            return true;
        }

        if (strcmp(name, ParkShutterSP.name) == 0)
        {
            IUUpdateSwitch(&ParkShutterSP, states, names, n);
            ParkShutterSP.s = IPS_OK;
            IDSetSwitch(&ParkShutterSP, nullptr);
            return true;
        }
    }

    return INDI::Dome::ISNewSwitch(dev, name, states, names, n);
}

bool AshDome::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    if (dev != nullptr && strcmp(dev, getDeviceName()) == 0)
    {
        if (strcmp(name, DomeHomePositionNP.name) == 0)
        {
            IUUpdateNumber(&DomeHomePositionNP, values, names, n);
            DomeHomePositionNP.s = IPS_OK;
            IDSetNumber(&DomeHomePositionNP, nullptr);
            return true;
        }
    }
    return INDI::Dome::ISNewNumber(dev, name, values, names, n);
}

/************************************************************************************
 *
* ***********************************************************************************/
bool AshDome::Ack()
{
    LOG_INFO("Ack");
    sim = isSimulation();

    if (sim)
    {
        interface.reset(static_cast<AshDomeCard *>(new AshDomeSim()));
    }
    else
    {
        LOGF_INFO("Before Getting portfd: %d", PortFD);
        PortFD = serialConnection->getPortFD();
        // TODO, detect card version and instantiate correct one
        LOGF_INFO("Starting AshdomeSerial with port f: %d", PortFD);
        interface.reset(static_cast<AshDomeCard *>(new AshDomeSerial(PortFD)));
    }
    return interface->detect();
}

/************************************************************************************
 *
* ***********************************************************************************/
bool AshDome::UpdateShutterStatus()
{
    // TODO implement correct shutter status?
    return true;
    LOG_INFO("UpdateShutterStatus");
    int rc = readBuffer(GetAllDigitalExt, 5, digitalSensorState);
    if (rc != 0)
    {
        LOGF_ERROR("Error reading input state: %d", rc);
        return false;
    }
    // LOGF_INFO("digitalext %x %x %x %x %x", digitalSensorState[0],
    // digitalSensorState[1], digitalSensorState[2], digitalSensorState[3],
    // digitalSensorState[4]);
    SensorsS[0].s  = getInputState(IN_ENCODER);
    SensorsS[1].s  = ISS_OFF; // ?
    SensorsS[2].s  = getInputState(IN_HOME);
    SensorsS[3].s  = getInputState(IN_OPEN1);
    SensorsS[4].s  = getInputState(IN_CLOSED1);
    SensorsS[5].s  = getInputState(IN_OPEN2);
    SensorsS[6].s  = getInputState(IN_CLOSED2);
    SensorsS[7].s  = getInputState(IN_S_HOME);
    SensorsS[8].s  = getInputState(IN_CLOUDS);
    SensorsS[9].s  = getInputState(IN_CLOUD);
    SensorsS[10].s = getInputState(IN_SAFE);
    SensorsS[11].s = getInputState(IN_ROT_LINK);
    SensorsS[12].s = getInputState(IN_FREE);
    SensorsSP.s    = IPS_OK;
    IDSetSwitch(&SensorsSP, nullptr);

    DomeShutterSP.s = IPS_OK;
    IUResetSwitch(&DomeShutterSP);

    if (getInputState(IN_OPEN1) == ISS_ON) // shutter open switch triggered
    {
        if (m_ShutterState == SHUTTER_MOVING && targetShutter == SHUTTER_OPEN)
        {
            LOGF_INFO("%s", GetShutterStatusString(SHUTTER_OPENED));
            setOutputState(OUT_OPEN1, ISS_OFF);
            m_ShutterState = SHUTTER_OPENED;
            if (getDomeState() == DOME_UNPARKING)
                SetParked(false);
        }
        DomeShutterS[SHUTTER_OPEN].s = ISS_ON;
    }
    else if (getInputState(IN_CLOSED1) == ISS_ON) // shutter closed switch triggered
    {
        if (m_ShutterState == SHUTTER_MOVING && targetShutter == SHUTTER_CLOSE)
        {
            LOGF_INFO("%s", GetShutterStatusString(SHUTTER_CLOSED));
            setOutputState(OUT_CLOSE1, ISS_OFF);
            m_ShutterState = SHUTTER_CLOSED;

            if (getDomeState() == DOME_PARKING && DomeAbsPosNP.s != IPS_BUSY)
            {
                SetParked(true);
            }
        }
        DomeShutterS[SHUTTER_CLOSE].s = ISS_ON;
    }
    else
    {
        m_ShutterState    = SHUTTER_MOVING;
        DomeShutterSP.s = IPS_BUSY;
    }
    return true;
}

/************************************************************************************
 *
* ***********************************************************************************/
bool AshDome::UpdatePosition()
{
    LOG_DEBUG("In UpdatePosition");
    bool pos = readU16(GetPosition, positionCounter);
    bool turns = readU16(GetTurns, turnsCounter);
    LOGF_DEBUG("Raw position: %d, raw turn: %d, poscode:%d turncode:%d", positionCounter, turnsCounter, pos, turns);
    bool positionChecksum = check_checksum(positionCounter);
    bool turnsChecksum = check_checksum(turnsCounter);
    uint16_t positionResult = get_result(positionCounter);
    uint16_t turnsResult = get_result(turnsCounter);
    double spt = (double)4096; // steps per turn
    double tpc = (double)75; // turns per circle
    double spc = (double)tpc*spt; // steps per circle
    double az = fmod(((double)positionResult)/ spc, 1)*360 - DomeHomePositionN[0].value;
    /* LOGF_INFO("%f, %f, %f, %f", ((double)turnsResult*spt + (double)positionCounter)/ spc, fmod(((double)turnsResult*spt + (double)positionCounter)/ spc, 1), DomeHomePositionN[0].value); */
    /* az = fmod(az, 360.0); */
    if (az < 0.0) {
      az += 360.0;
    }
    DomeAbsPosN[0].value = az;
    /* LOGF_INFO("Encoder pos:%d, turns:%d, AZ: %f, stepsPerTurn: %d, checksum: (%d,%d), spc: %f", positionResult,turnsResult, az, stepsPerTurn, positionChecksum, turnsChecksum, spc); */
    return true;
}

void AshDome::Reset()
{
    LOG_INFO("In Reset");
    uint8_t dummy = '\0';
    writeU8(ResetCounter, dummy);
    usleep((useconds_t)200000); // 200ms as per spec */
}
/************************************************************************************
 *
* ***********************************************************************************/
bool AshDome::UpdateSensorStatus()
{
    LOG_INFO("TODO UpdateSensorStatus");
    return true;
    readU8(GetLinkStrength, linkStrength);
    readFloat(GetAnalog1, sensors[0]);
    readFloat(GetAnalog2, sensors[1]);
    readFloat(GetMainAnalog1, sensors[2]);
    readFloat(GetMainAnalog2, sensors[3]);
    readFloat(GetTempIn, sensors[4]);
    readFloat(GetTempOut, sensors[5]);
    readFloat(GetTempHum, sensors[6]);
    readFloat(GetHum, sensors[7]);
    readFloat(GetPressure, sensors[8]);

    EnvironmentSensorsN[0].value = linkStrength;
    for (int i = 0; i < 9; ++i)
    {
        EnvironmentSensorsN[i + 1].value = sensors[i];
    }
    EnvironmentSensorsN[10].value = getDewPoint(EnvironmentSensorsN[8].value, EnvironmentSensorsN[7].value);
    EnvironmentSensorsNP.s        = IPS_OK;

    IDSetNumber(&EnvironmentSensorsNP, nullptr);
    return true;
}

/************************************************************************************
 *
* ***********************************************************************************/
bool AshDome::UpdateRelayStatus()
{
    LOG_INFO("TODO UpdateRelayStatus");
    return true;
    PowerRelaysS[0].s = getInputState(OUT_CCD);
    PowerRelaysS[1].s = getInputState(OUT_SCOPE);
    PowerRelaysS[2].s = getInputState(OUT_LIGHT);
    PowerRelaysS[3].s = getInputState(OUT_FAN);
    PowerRelaysSP.s   = IPS_OK;
    IDSetSwitch(&PowerRelaysSP, nullptr);

    RelaysS[0].s = getInputState(OUT_RELAY1);
    RelaysS[0].s = getInputState(OUT_RELAY1);
    RelaysS[0].s = getInputState(OUT_RELAY1);
    RelaysS[0].s = getInputState(OUT_RELAY1);
    RelaysSP.s   = IPS_OK;
    IDSetSwitch(&RelaysSP, nullptr);
    return true;
}

/************************************************************************************
 *
* ***********************************************************************************/
void AshDome::TimerHit()
{
    LOG_INFO("TimerHit");
    if (!isConnected())
        return; //  No need to reset timer if we are not connected anymore

    /* readU16(GetStatus, currentStatus); */
    // LOGF_INFO("Status: %x", currentStatus);
    UpdatePosition();
    /* usleep((useconds_t)10000000); // 10s */
    UpdateShutterStatus();
    IDSetSwitch(&DomeShutterSP, nullptr);

    /* TODO UpdateRelayStatus(); */

    /* LOG_INFO("TimerHit is exiting early, because Mike put a return here"); */
    /* return; */

    if (status == DOME_HOMING)
    {
        if ((currentStatus & (STATUS_HOMING | STATUS_MOVING)) == 0)
        {
            double azDiff = DomeHomePositionN[0].value - DomeAbsPosN[0].value;

            if (azDiff > 180)
            {
                azDiff -= 360;
            }
            if (azDiff < -180)
            {
                azDiff += 360;
            }

            if (getInputState(IN_HOME) || fabs(azDiff) <= DomeParamN[0].value)
            {
                // Found home (or close enough)
                LOG_INFO("Home sensor found");
                status   = DOME_READY;
                targetAz = DomeHomePositionN[0].value;

                // Reset counters
                writeCmd(ResetCounter);
                writeCmd(ResetCounterExt);

                FindHomeSP.s   = IPS_OK;
                DomeAbsPosNP.s = IPS_OK;
                IDSetSwitch(&FindHomeSP, nullptr);
            }
            else
            {
                // We overshoot, go closer
                MoveAbs(DomeHomePositionN[0].value);
            }
        }
        IDSetNumber(&DomeAbsPosNP, nullptr);
    }
    else if (status == DOME_DEROTATING)
    {
        if ((currentStatus & STATUS_MOVING) == 0)
        {
            readS32(GetCounterExt, currentRotation);
            LOGF_INFO("Current rotation is %d", currentRotation);
            if (abs(currentRotation) < 100)
            {
                // Close enough
                LOG_INFO("De-rotation complete");
                status         = DOME_READY;
                DerotateSP.s   = IPS_OK;
                DomeAbsPosNP.s = IPS_OK;
                IDSetSwitch(&DerotateSP, nullptr);
            }
            else
            {
                if (currentRotation < 0)
                {
                    writeU16(CCWRotation, compensateInertia(-currentRotation));
                }
                else
                {
                    writeU16(CWRotation, compensateInertia(currentRotation));
                }
            }
        }
        IDSetNumber(&DomeAbsPosNP, nullptr);
    }
    else if (DomeAbsPosNP.s == IPS_BUSY)
    {
        if ((currentStatus & STATUS_MOVING) == 0)
        {
            // Rotation idle, are we close enough?
            double azDiff = targetAz - DomeAbsPosN[0].value;

            if (azDiff > 180)
            {
                azDiff -= 360;
            }
            if (azDiff < -180)
            {
                azDiff += 360;
            }
            if (!refineMove || fabs(azDiff) <= DomeParamN[0].value)
            {
                if (refineMove)
                    DomeAbsPosN[0].value = targetAz;
                DomeAbsPosNP.s = IPS_OK;
                LOG_INFO("Dome reached requested azimuth angle.");

                if (getDomeState() == DOME_PARKING)
                {
                    if (ParkShutterS[0].s == ISS_ON && getInputState(IN_CLOSED1) == ISS_OFF)
                    {
                        ControlShutter(SHUTTER_CLOSE);
                    }
                    else
                    {
                        SetParked(true);
                    }
                }
                else if (getDomeState() == DOME_UNPARKING)
                    SetParked(false);
                else
                    setDomeState(DOME_SYNCED);
            }
            else
            {
                // Refine azimuth
                MoveAbs(targetAz);
            }
        }

        IDSetNumber(&DomeAbsPosNP, nullptr);
    }
    else
        IDSetNumber(&DomeAbsPosNP, nullptr);

    // Read temperatures only every 10th time
    static int tmpCounter = 0;
    if (--tmpCounter <= 0)
    {
        UpdateSensorStatus();
        tmpCounter = 10;
    }

    SetTimer(POLLMS);
}

/************************************************************************************
 *
* ***********************************************************************************/
IPState AshDome::MoveAbs(double az)
{
    LOG_INFO("MoveAbs");
    LOGF_DEBUG("MoveAbs (%f)", az);
    targetAz      = az;
    double azDiff = az - DomeAbsPosN[0].value;
    LOGF_DEBUG("azDiff = %f", azDiff);

    // Make relative (-180 - 180) regardless if it passes az 0
    if (azDiff > 180)
    {
        azDiff -= 360;
    }
    if (azDiff < -180)
    {
        azDiff += 360;
    }

    LOGF_DEBUG("azDiff rel = %f", azDiff);

    refineMove = true;
    return sendMove(azDiff);
}

/************************************************************************************
 *
* ***********************************************************************************/
IPState AshDome::MoveRel(double azDiff)
{
    LOG_INFO("MoveRel");
    refineMove = false;
    return sendMove(azDiff);
}

/************************************************************************************
 *
* ***********************************************************************************/
IPState AshDome::sendMove(double azDiff)
{
    LOG_INFO("sendMove");
    int rc;

    if (azDiff < 0)
    {
        uint16_t steps = (uint16_t)(-azDiff * stepsPerTurn / 360.0);
        LOGF_DEBUG("CCW (%d)", steps);
        steps = compensateInertia(steps);
        LOGF_DEBUG("CCW inertia (%d)", steps);
        if (steps == 0)
            return IPS_OK;
        rc = writeU16(CCWRotation, steps);
    }
    else
    {
        uint16_t steps = (uint16_t)(azDiff * stepsPerTurn / 360.0);
        LOGF_DEBUG("CW (%d)", steps);
        steps = compensateInertia(steps);
        LOGF_DEBUG("CW inertia (%d)", steps);
        if (steps == 0)
            return IPS_OK;
        rc = writeU16(CWRotation, steps);
    }
    if (rc != 0)
    {
        LOGF_ERROR("Error moving dome: %d", rc);
    }
    return IPS_BUSY;
}

/************************************************************************************
 *
* ***********************************************************************************/
IPState AshDome::Move(DomeDirection dir, DomeMotionCommand operation)
{
    LOG_INFO("Move");
    // Map to button outputs
    if (operation == MOTION_START)
    {
        refineMove = false;
        if (dir == DOME_CW)
        {
            setOutputState(OUT_CW, ISS_ON);
            setOutputState(OUT_CCW, ISS_OFF);
        }
        else
        {
            setOutputState(OUT_CW, ISS_OFF);
            setOutputState(OUT_CCW, ISS_ON);
        }
        return IPS_BUSY;
    }
    setOutputState(OUT_CW, ISS_OFF);
    setOutputState(OUT_CCW, ISS_OFF);
    return IPS_OK;
}

/************************************************************************************
 *
* ***********************************************************************************/
IPState AshDome::Park()
{
    LOG_INFO("Park");
    // First move to park position and then optionally close shutter
    targetAz  = GetAxis1Park();
    IPState s = MoveAbs(targetAz);
    if (s == IPS_OK && ParkShutterS[0].s == ISS_ON)
    {
        // Already at home, just close if needed
        return ControlShutter(SHUTTER_CLOSE);
    }
    return s;
}

/************************************************************************************
 *
* ***********************************************************************************/
IPState AshDome::UnPark()
{
    LOG_INFO("UnPark");
    if (ParkShutterS[0].s == ISS_ON)
    {
        return ControlShutter(SHUTTER_OPEN);
    }
    return IPS_OK;
}

/************************************************************************************
 *
* ***********************************************************************************/
IPState AshDome::ControlShutter(ShutterOperation operation)
{
    LOGF_INFO("Control shutter %d", (int)operation);
    targetShutter = operation;
    if (operation == SHUTTER_OPEN)
    {
        LOG_INFO("Opening shutter");
        if (getInputState(IN_OPEN1))
        {
            LOG_INFO("Shutter already open");
            return IPS_OK;
        }
        setOutputState(OUT_CLOSE1, ISS_OFF);
        setOutputState(OUT_OPEN1, ISS_ON);
    }
    else
    {
        LOG_INFO("Closing shutter");
        if (getInputState(IN_CLOSED1))
        {
            LOG_INFO("Shutter already closed");
            return IPS_OK;
        }
        setOutputState(OUT_OPEN1, ISS_OFF);
        setOutputState(OUT_CLOSE1, ISS_ON);
    }

    m_ShutterState = SHUTTER_MOVING;
    return IPS_BUSY;
}

/************************************************************************************
 *
* ***********************************************************************************/
bool AshDome::Abort()
{
    LOG_INFO("Abort");
    writeCmd(Stop);
    status = DOME_READY;
    return true;
}

/************************************************************************************
 *
* ***********************************************************************************/
bool AshDome::saveConfigItems(FILE *fp)
{
    INDI::Dome::saveConfigItems(fp);

    IUSaveConfigNumber(fp, &DomeHomePositionNP);
    IUSaveConfigSwitch(fp, &ParkShutterSP);
    return true;
}

/************************************************************************************
 *
* ***********************************************************************************/
bool AshDome::SetCurrentPark()
{
    SetAxis1Park(DomeAbsPosN[0].value);
    return true;
}
/************************************************************************************
 *
* ***********************************************************************************/

bool AshDome::SetDefaultPark()
{
    // By default set position to 90
    SetAxis1Park(90);
    return true;
}

/************************************************************************************
 *
* ***********************************************************************************/
bool AshDome::readFloat(AshDomeCommand cmd, float &dst)
{
    float value;
    AshDomeCommand c;
    int rc;
    int retryCount = 2;
    do
    {
        rc = interface->write(cmd);
        if (rc == 0)
            rc = interface->readBuf(c, 4, (uint8_t *)&value);
        else
            reconnect();
    } while (rc != 0 && --retryCount);
    //    LOGF_ERROR("readFloat: %d %f", cmd, value);
    if (rc == 0)
    {
        dst = value;
        return true;
    }
    return false;
}

bool AshDome::readU8(AshDomeCommand cmd, uint8_t &dst)
{
    uint8_t value;
    AshDomeCommand c;
    int rc;
    int retryCount = 2;
    do
    {
        rc = interface->write(cmd);
        if (rc == 0)
            rc = interface->readBuf(c, 1, &value);
        else
            reconnect();
    } while (rc != 0 && --retryCount);
    //    LOGF_ERROR("readU8: %d %x", cmd, value);
    if (rc == 0)
    {
        dst = value;
        return true;
    }
    return false;
}

bool AshDome::readS8(AshDomeCommand cmd, int8_t &dst)
{
    int8_t value;
    AshDomeCommand c;
    int rc;
    int retryCount = 2;
    do
    {
        rc = interface->write(cmd);
        if (rc == 0)
            rc = interface->readBuf(c, 1, (uint8_t *)&value);
        else
            reconnect();
    } while (rc != 0 && --retryCount);
    //    LOGF_ERROR("readS8: %d %x", cmd, value);
    if (rc == 0)
    {
        dst = value;
        return true;
    }
    return false;
}

bool AshDome::readU16(AshDomeCommand cmd, uint16_t &dst)
{
    uint16_t value;
    AshDomeCommand c;
    int rc;
    int retryCount = 2;
    do
    {
        rc = interface->write(cmd);
        if (rc == 0)
            rc = interface->readBuf(c, 2, (uint8_t *)&value);
        else {
            LOGF_INFO("readU16: reconnecting because rc is %d", rc);
            reconnect();
         }
    } while (rc != 0 && --retryCount);
    //    LOGF_ERROR("readU16: %d %x", cmd, value);
    if (rc == 0)
    {
        dst = value;
        return true;
    }
    return false;
}

bool AshDome::readS16(AshDomeCommand cmd, int16_t &dst)
{
    int16_t value;
    AshDomeCommand c;
    int rc;
    int retryCount = 2;
    do
    {
        rc = interface->write(cmd);
        if (rc == 0)
            rc = interface->readBuf(c, 2, (uint8_t *)&value);
        else
            reconnect();
    } while (rc != 0 && --retryCount);
    //    LOGF_ERROR("readS16: %d %x", cmd, value);
    if (rc == 0)
    {
        dst = value;
        return true;
    }
    return false;
}

bool AshDome::readU32(AshDomeCommand cmd, uint32_t &dst)
{
    uint32_t value;
    AshDomeCommand c;
    int rc;
    int retryCount = 2;
    do
    {
        rc = interface->write(cmd);
        if (rc == 0)
            rc = interface->readBuf(c, 4, (uint8_t *)&value);
        else
            reconnect();
    } while (rc != 0 && --retryCount);
    //    LOGF_ERROR("readU32: %d %x", cmd, value);
    if (rc == 0)
    {
        dst = value;
        return true;
    }
    return false;
}

bool AshDome::readS32(AshDomeCommand cmd, int32_t &dst)
{
    int32_t value;
    AshDomeCommand c;
    int rc;
    int retryCount = 2;
    do
    {
        rc = interface->write(cmd);
        if (rc == 0)
            rc = interface->readBuf(c, 4, (uint8_t *)&value);
        else
            reconnect();
    } while (rc != 0 && --retryCount);
    //    LOGF_ERROR("readU32: %d %x", cmd, value);
    if (rc == 0)
    {
        dst = value;
        return true;
    }
    return false;
}

int AshDome::readBuffer(AshDomeCommand cmd, int len, uint8_t *cbuf)
{
    int rc;
    int retryCount = 2;
    AshDomeCommand c;
    do
    {
        rc = interface->write(cmd);
        if (rc == 0)
            rc = interface->readBuf(c, len, cbuf);
        else
            reconnect();
    } while (rc != 0 && --retryCount);
    return rc;
}

int AshDome::writeCmd(AshDomeCommand cmd)
{
    int rc = interface->write(cmd);
    if (rc != 0)
    {
        reconnect();
        return rc;
    }
    return interface->read(cmd);
}

int AshDome::writeU8(AshDomeCommand cmd, uint8_t value)
{
    int rc = interface->writeBuf(cmd, 1, &value);
    if (rc != 0)
    {
        reconnect();
        return rc;
    }
    return interface->read(cmd);
}

int AshDome::writeU16(AshDomeCommand cmd, uint16_t value)
{
    int rc = interface->writeBuf(cmd, 2, (uint8_t *)&value);
    if (rc != 0)
    {
        reconnect();
        return rc;
    }
    return interface->read(cmd);
}

int AshDome::writeU32(AshDomeCommand cmd, uint32_t value)
{
    int rc = interface->writeBuf(cmd, 4, (uint8_t *)&value);
    if (rc != 0)
    {
        reconnect();
        return rc;
    }
    return interface->read(cmd);
}

int AshDome::writeBuffer(AshDomeCommand cmd, int len, uint8_t *cbuf)
{
    int rc = interface->writeBuf(cmd, len, cbuf);
    if (rc != 0)
    {
        reconnect();
        return rc;
    }
    return interface->read(cmd);
}

void AshDome::reconnect()
{
    LOG_INFO("AshDome::reconnect -> Reconnecting serial port");
    // Reconnect serial port after write error
    serialConnection->Disconnect();
    usleep((useconds_t)1000000); // 1s
    /* serialConnection->setDefaultPort("/dev/serial0"); */
    serialConnection->Connect();
    PortFD = serialConnection->getPortFD();
    interface ->setPortFD(PortFD);
    LOGF_INFO("AshDome::reconnect -> Reconnected on %d.", PortFD);
}

ISState AshDome::getInputState(AshDomeDigitalIO channel)
{
    int ch      = (int)channel;
    int byte    = ch >> 3;
    uint8_t bit = 1 << (ch & 7);
    return (digitalSensorState[byte] & bit) ? ISS_ON : ISS_OFF;
}

int AshDome::setOutputState(AshDomeDigitalIO channel, ISState onOff)
{
    return writeU8(onOff == ISS_ON ? SetDigitalChannel : ClearDigitalChannel, (int)channel);
}

/*
 * Saturation Vapor Pressure formula for range -100..0 Deg. C.
 * This is taken from
 *   ITS-90 Formulations for Vapor Pressure, Frostpoint Temperature,
 *   Dewpoint Temperature, and Enhancement Factors in the Range 100 to +100 C
 * by Bob Hardy
 * as published in "The Proceedings of the Third International Symposium on
 * Humidity & Moisture",
 * Teddington, London, England, April 1998
 */
static const float k0 = -5.8666426e3;
static const float k1 = 2.232870244e1;
static const float k2 = 1.39387003e-2;
static const float k3 = -3.4262402e-5;
static const float k4 = 2.7040955e-8;
static const float k5 = 6.7063522e-1;

static float pvsIce(float T)
{
    float lnP = k0 / T + k1 + (k2 + (k3 + (k4 * T)) * T) * T + k5 * log(T);
    return exp(lnP);
}

/**
 * Saturation Vapor Pressure formula for range 273..678 Deg. K.
 * This is taken from the
 *   Release on the IAPWS Industrial Formulation 1997
 *   for the Thermodynamic Properties of Water and Steam
 * by IAPWS (International Association for the Properties of Water and Steam),
 * Erlangen, Germany, September 1997.
 *
 * This is Equation (30) in Section 8.1 "The Saturation-Pressure Equation (Basic
 * Equation)"
 */

static const float n1  = 0.11670521452767e4;
static const float n6  = 0.14915108613530e2;
static const float n2  = -0.72421316703206e6;
static const float n7  = -0.48232657361591e4;
static const float n3  = -0.17073846940092e2;
static const float n8  = 0.40511340542057e6;
static const float n4  = 0.12020824702470e5;
static const float n9  = -0.23855557567849;
static const float n5  = -0.32325550322333e7;
static const float n10 = 0.65017534844798e3;

static float pvsWater(float T)
{
    float th = T + n9 / (T - n10);
    float A  = (th + n1) * th + n2;
    ;
    float B = (n3 * th + n4) * th + n5;
    float C = (n6 * th + n7) * th + n8;
    ;

    float p = 2.0f * C / (-B + sqrt(B * B - 4.0f * A * C));
    p *= p;
    p *= p;
    return p * 1e6;
}

static const float C_OFFSET = 273.15f;
static const float minT     = 173; // -100 Deg. C.
static const float maxT     = 678;

static float PVS(float T)
{
    if (T < minT || T > maxT)
        return 0;
    else if (T < C_OFFSET)
        return pvsIce(T);
    else
        return pvsWater(T);
}

static float solve(float (*f)(float), float y, float x0)
{
    float x      = x0;
    int maxCount = 10;
    int count    = 0;
    while (++count < maxCount)
    {
        float xNew;
        float dx = x / 1000;
        float z  = f(x);
        xNew     = x + dx * (y - z) / (f(x + dx) - z);
        if (fabs((xNew - x) / xNew) < 0.0001f)
            return xNew;
        x = xNew;
    }
    return 0;
}

float AshDome::getDewPoint(float RH, float T)
{
    T = T + C_OFFSET;
    return solve(PVS, RH / 100 * PVS(T), T) - C_OFFSET;
}

uint16_t AshDome::compensateInertia(uint16_t steps)
{
    if (inertiaTable.size() == 0)
    {
        LOGF_INFO("inertia passthrough %d", steps);
        return steps; // pass value as such if we don't have enough data
    }

    for (uint16_t out = 0; out < inertiaTable.size(); out++)
    {
        if (inertiaTable[out] > steps)
        {
            LOGF_INFO("inertia %d -> %d", steps, out - 1);
            return out - 1;
        }
    }
    // Check difference from largest table entry and assume we have
    // similar inertia also after that
    int lastEntry = inertiaTable.size() - 1;
    int inertia   = inertiaTable[lastEntry] - lastEntry;
    int movement  = (int)steps - inertia;
    LOGF_INFO("inertia %d -> %d", steps, movement);
    if (movement <= 0)
        return 0;

    return movement;
}

uint16_t AshDome::get_result(uint16_t full_response)
{
    // 0011111111111111 mask and shift because 12 bit
    return (full_response & 0x3FFF) >> 2;
}

bool AshDome::check_checksum(uint16_t word)
{
    /* Odd:   K1 = !(H5^H3^H1^L7^L5^L3^L1) */
    /* Even:  K0 = !(H4^H2^H0^L6^L4^L2^L0) */

    /* From the above response 0x61AB: */
    /* Odd:   0 = !(1^0^0^1^1^1^1) = correct */
    /* Even:  1 = !(0^0^1^0^0^0^1) = correct */

    /* example: */
    /*     HIGH     LOW */
    /*     01100001 10101011 */
    /*     76543210 76543210 */
    /*     FEDCBA98 76543210 */

    /*     H5^H3^H1^L7^L5^L3^L1 */
    /* K1: 13 ^ 11 ^ 9 ^ 7 ^ 5 ^ 3 ^ 1 */

    /*     H4^H2^H0^L6^L4^L2^L0 */
    /* K0: 12 ^ 10 ^ 8 ^ 6 ^ 4 ^ 2 ^ 0 */

    bool k1 = word >> 15 & 1;
    bool k0 = word >> 14 & 1;
    bool k1_result = !((word >> 13 & 1) ^ (word >> 11 & 1) ^ (word >> 9 & 1) ^
                       (word >> 7 & 1) ^ (word >> 5 & 1) ^ (word >> 3 & 1) ^
                       (word >> 1 & 1));
    bool k0_result = !((word >> 12 & 1) ^ (word >> 10 & 1) ^ (word >> 8 & 1) ^
                       (word >> 6 & 1) ^ (word >> 4 & 1) ^ (word >> 2 & 1) ^
                       (word >> 0 & 1));
    bool correct = k1_result == k1 and k0_result == k0;
    if (!correct) {
        LOGF_WARN("k1: %d, K1_result: %d, k0: %d, k0_result: %d, correct: %d",k1, k1_result, k0, k0_result, correct);
    }
    return correct;
}
