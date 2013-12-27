/*
    This file is part of Repetier-Firmware.

    Repetier-Firmware is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Repetier-Firmware is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Repetier-Firmware.  If not, see <http://www.gnu.org/licenses/>.

    This firmware is a nearly complete rewrite of the sprinter firmware
    by kliment (https://github.com/kliment/Sprinter)
    which based on Tonokip RepRap firmware rewrite based off of Hydra-mmm firmware.
*/

#include "Repetier.h"

const int sensitive_pins[] PROGMEM = SENSITIVE_PINS; // Sensitive pin list for M42
int Commands::lowestRAMValue=MAX_RAM;
int Commands::lowestRAMValueSend=MAX_RAM;

void Commands::commandLoop()
{
    while(true)
    {
        GCode::readFromSerial();
        GCode *code = GCode::peekCurrentCommand();
        //UI_SLOW; // do longer timed user interface action
        UI_MEDIUM; // do check encoder
        if(code)
        {
#if SDSUPPORT
            if(sd.savetosd)
            {
                if(!(code->hasM() && code->M==29))   // still writing to file
                {
                    sd.write_command(code);
                }
                else
                {
                    sd.finishWrite();
                }
#ifdef ECHO_ON_EXECUTE
                code->echoCommand();
#endif
            }
            else
#endif
                Commands::executeGCode(code);
            code->popCurrentCommand();
        }
        Printer::defaultLoopActions();
    }
}

void Commands::checkForPeriodicalActions()
{
    if(!execute_periodical) return;
    execute_periodical=0;
    Extruder::manageTemperatures();
    if(--counter_250ms==0)
    {
        if(manage_monitor<=1+NUM_EXTRUDER)
            write_monitor();
        counter_250ms=5;
    }
    UI_SLOW;
}

/** \brief Waits until movement cache is empty.

  Some commands expect no movement, before they can execute. This function
  waits, until the steppers are stopped. In the meanwhile it buffers incoming
  commands and manages temperatures.
*/
void Commands::waitUntilEndOfAllMoves()
{
    while(PrintLine::hasLines())
    {
        GCode::readFromSerial();
        Commands::checkForPeriodicalActions();
        UI_MEDIUM;
    }
}
void Commands::printCurrentPosition()
{
    Com::printF(Com::tXColon,Printer::currentPositionSteps[0]*inv_axis_steps_per_unit[0]*(unit_inches?0.03937:1),2);
    Com::printF(Com::tSpaceYColon,Printer::currentPositionSteps[1]*inv_axis_steps_per_unit[1]*(unit_inches?0.03937:1),2);
    Com::printF(Com::tSpaceZColon,Printer::currentPositionSteps[2]*inv_axis_steps_per_unit[2]*(unit_inches?0.03937:1),2);
    Com::printF(Com::tSpaceEColon,Printer::currentPositionSteps[3]*inv_axis_steps_per_unit[3]*(unit_inches?0.03937:1),2);
}
void Commands::printTemperatures()
{
    float temp = current_extruder->tempControl.currentTemperatureC;
#if HEATED_BED_SENSOR_TYPE==0
    Com::printF(Com::tTColon,temp);
#else
    Com::printF(Com::tTColon,temp);
    Com::printF(Com::tSpaceBColon,Extruder::getHeatedBedTemperature());
#endif
#ifdef TEMP_PID
    Com::printF(Com::tSpaceAtColon,(autotuneIndex==255?pwm_pos[current_extruder->id]:pwm_pos[autotuneIndex])); // Show output of autotune when tuning!
#endif
#if NUM_EXTRUDER>1
    for(byte i=0; i<NUM_EXTRUDER; i++)
    {
        Com::printF(Com::tSpaceT,(int)i);
        Com::printF(Com::tColon,extruder[i].tempControl.currentTemperatureC);
#ifdef TEMP_PID
        Com::printF(Com::tSpaceAt,(int)i);
        Com::printF(Com::tColon,(pwm_pos[extruder[i].tempControl.pwmIndex])); // Show output of autotune when tuning!
#endif
    }
#endif
    Com::println();
}
void Commands::changeFeedrateMultiply(int factor)
{
    if(factor<25) factor=25;
    if(factor>500) factor=500;
    printer.feedrate *= (float)factor/(float)printer.feedrateMultiply;
    printer.feedrateMultiply = factor;
    Com::printFLN(Com::tSpeedMultiply,factor);
}
void Commands::changeFlowateMultiply(int factor)
{
    if(factor<25) factor=25;
    if(factor>200) factor=200;
    printer.extrudeMultiply = factor;
    Com::printFLN(Com::tFlowMultiply,factor);
}
void Commands::setFanSpeed(int speed,bool wait)
{
#if FAN_PIN>=0
    speed = constrain(speed,0,255);
    if(wait)
        Commands::waitUntilEndOfAllMoves(); // use only if neededthis to change the speed exactly at that point, but it may cause blobs if you do!
    if(speed!=pwm_pos[NUM_EXTRUDER+2])
        Com::printFLN(Com::tFanspeed,speed);
    pwm_pos[NUM_EXTRUDER+2] = speed;
#endif
}
#if DRIVE_SYSTEM==3
void delta_move_to_top_endstops(float feedrate)
{
    long up_steps = printer.zMaxSteps;
    for (byte i=0; i<3; i++)
        Printer::currentPositionSteps[i] = 0;
    calculate_delta(Printer::currentPositionSteps, printer.currentDeltaPositionSteps);
    PrintLine::moveRelativeDistanceInSteps(0,0,printer.zMaxSteps*ENDSTOP_Z_BACK_MOVE,0,feedrate, true, true);
}
void Commands::homeXAxis()
{
    Printer::destinationSteps[0] = 0;
    split_delta_move(true,false,false);
}
void Commands::homeYAxis()
{
    Printer::destinationSteps[1] = 0;
    split_delta_move(true,false,false);
}
void Commands::homeZAxis()
{
    delta_move_to_top_endstops(homing_feedrate[0]);
    PrintLine::moveRelativeDistanceInSteps(0,0,axis_steps_per_unit[0]*-ENDSTOP_Z_BACK_MOVE,0,homing_feedrate[0]/ENDSTOP_X_RETEST_REDUCTION_FACTOR, true, false);
    delta_move_to_top_endstops(homing_feedrate[0]/ENDSTOP_X_RETEST_REDUCTION_FACTOR);
    Printer::currentPositionSteps[0] = 0;
    Printer::currentPositionSteps[1] = 0;
    Printer::currentPositionSteps[2] = printer.zMaxSteps;
    calculate_delta(Printer::currentPositionSteps, printer.currentDeltaPositionSteps);
    printer.maxDeltaPositionSteps = printer.currentDeltaPositionSteps[0];
}

void Commands::homeAxis(bool xaxis,bool yaxis,bool zaxis)
{
    long steps;
    bool homeallaxis = (xaxis && yaxis && zaxis) || (!xaxis && !yaxis && !zaxis);
    if (X_MAX_PIN > -1 && Y_MAX_PIN > -1 && Z_MAX_PIN > -1 && MAX_HARDWARE_ENDSTOP_X & MAX_HARDWARE_ENDSTOP_Y && MAX_HARDWARE_ENDSTOP_Z)
    {
        UI_STATUS_UPD(UI_TEXT_HOME_DELTA);
        // Homing Z axis means that you must home X and Y
        if (homeallaxis || zaxis)
        {
            homeZAxis();
        }
        else
        {
            if (xaxis) Printer::destinationSteps[0] = 0;
            if (yaxis) Printer::destinationSteps[1] = 0;
            split_delta_move(true,false,false);
        }
        printer.countZSteps = 0;
        UI_CLEAR_STATUS
    }
}
#else
void Commands::homeXAxis()
{
    long steps;
    if ((MIN_HARDWARE_ENDSTOP_X && X_MIN_PIN > -1 && X_HOME_DIR==-1) || (MAX_HARDWARE_ENDSTOP_X && X_MAX_PIN > -1 && X_HOME_DIR==1))
    {
        UI_STATUS_UPD(UI_TEXT_HOME_X);
        steps = (printer.xMaxSteps-printer.xMinSteps) * X_HOME_DIR;
        Printer::currentPositionSteps[0] = -steps;
        PrintLine::moveRelativeDistanceInSteps(2*steps,0,0,0,homing_feedrate[0],true,true);
        Printer::currentPositionSteps[0] = 0;
        PrintLine::moveRelativeDistanceInSteps(axis_steps_per_unit[0]*-ENDSTOP_X_BACK_MOVE * X_HOME_DIR,0,0,0,homing_feedrate[0]/ENDSTOP_X_RETEST_REDUCTION_FACTOR,true,false);
        PrintLine::moveRelativeDistanceInSteps(axis_steps_per_unit[0]*2*ENDSTOP_X_BACK_MOVE * X_HOME_DIR,0,0,0,homing_feedrate[0]/ENDSTOP_X_RETEST_REDUCTION_FACTOR,true,true);
#if defined(ENDSTOP_X_BACK_ON_HOME)
        if(ENDSTOP_X_BACK_ON_HOME > 0)
            PrintLine::moveRelativeDistanceInSteps(axis_steps_per_unit[0]*-ENDSTOP_X_BACK_ON_HOME * X_HOME_DIR,0,0,0,homing_feedrate[0],true,false);
#endif
        long offX = 0;
#if NUM_EXTRUDER>1
        for(byte i=0; i<NUM_EXTRUDER; i++) offX = max(offX,extruder[i].xOffset);
        // Reposition extruder that way, that all extruders can be selected at home pos.
#endif
        Printer::currentPositionSteps[0] = (X_HOME_DIR == -1) ? printer.xMinSteps-offX : printer.xMaxSteps+offX;
#if NUM_EXTRUDER>1
        PrintLine::moveRelativeDistanceInSteps((current_extruder->xOffset-offX) * X_HOME_DIR,0,0,0,homing_feedrate[0],true,false);
#endif
    }
}
void Commands::homeYAxis()
{
    long steps;
    if ((MIN_HARDWARE_ENDSTOP_Y && Y_MIN_PIN > -1 && Y_HOME_DIR==-1) || (MAX_HARDWARE_ENDSTOP_Y && Y_MAX_PIN > -1 && Y_HOME_DIR==1))
    {
        UI_STATUS_UPD(UI_TEXT_HOME_Y);
        steps = (printer.yMaxSteps-printer.yMinSteps) * Y_HOME_DIR;
        Printer::currentPositionSteps[1] = -steps;
        PrintLine::moveRelativeDistanceInSteps(0,2*steps,0,0,homing_feedrate[1],true,true);
        Printer::currentPositionSteps[1] = 0;
        PrintLine::moveRelativeDistanceInSteps(0,axis_steps_per_unit[1]*-ENDSTOP_Y_BACK_MOVE * Y_HOME_DIR,0,0,homing_feedrate[1]/ENDSTOP_X_RETEST_REDUCTION_FACTOR,true,false);
        PrintLine::moveRelativeDistanceInSteps(0,axis_steps_per_unit[1]*2*ENDSTOP_Y_BACK_MOVE * Y_HOME_DIR,0,0,homing_feedrate[1]/ENDSTOP_X_RETEST_REDUCTION_FACTOR,true,true);
#if defined(ENDSTOP_Y_BACK_ON_HOME)
        if(ENDSTOP_Y_BACK_ON_HOME > 0)
            PrintLine::moveRelativeDistanceInSteps(0,axis_steps_per_unit[1]*-ENDSTOP_Y_BACK_ON_HOME * Y_HOME_DIR,0,0,homing_feedrate[1],true,false);
#endif
        long offY = 0;
#if NUM_EXTRUDER>1
        for(byte i=0; i<NUM_EXTRUDER; i++) offY = max(offY,extruder[i].yOffset);
        // Reposition extruder that way, that all extruders can be selected at home pos.
#endif
        Printer::currentPositionSteps[1] = (Y_HOME_DIR == -1) ? printer.yMinSteps-offY : printer.yMaxSteps+offY;
#if NUM_EXTRUDER>1
        PrintLine::moveRelativeDistanceInSteps(0,(current_extruder->yOffset-offY) * Y_HOME_DIR,0,0,homing_feedrate[1],true,false);
#endif
    }
}
void Commands::homeZAxis()
{
    long steps;
    if ((MIN_HARDWARE_ENDSTOP_Z && Z_MIN_PIN > -1 && Z_HOME_DIR==-1) || (MAX_HARDWARE_ENDSTOP_Z && Z_MAX_PIN > -1 && Z_HOME_DIR==1))
    {
        UI_STATUS_UPD(UI_TEXT_HOME_Z);
        steps = (printer.zMaxSteps-printer.zMinSteps) * Z_HOME_DIR;
        Printer::currentPositionSteps[2] = -steps;
        PrintLine::moveRelativeDistanceInSteps(0,0,2*steps,0,homing_feedrate[2],true,true);
        Printer::currentPositionSteps[2] = 0;
        PrintLine::moveRelativeDistanceInSteps(0,0,axis_steps_per_unit[2]*-ENDSTOP_Z_BACK_MOVE * Z_HOME_DIR,0,homing_feedrate[2]/ENDSTOP_Z_RETEST_REDUCTION_FACTOR,true,false);
        PrintLine::moveRelativeDistanceInSteps(0,0,axis_steps_per_unit[2]*2*ENDSTOP_Z_BACK_MOVE * Z_HOME_DIR,0,homing_feedrate[2]/ENDSTOP_Z_RETEST_REDUCTION_FACTOR,true,true);
#if defined(ENDSTOP_Z_BACK_ON_HOME)
        if(ENDSTOP_Z_BACK_ON_HOME > 0)
            PrintLine::moveRelativeDistanceInSteps(0,0,axis_steps_per_unit[2]*-ENDSTOP_Z_BACK_ON_HOME * Z_HOME_DIR,0,homing_feedrate[2],true,false);
#endif
        Printer::currentPositionSteps[2] = (Z_HOME_DIR == -1) ? printer.zMinSteps : printer.zMaxSteps;
    }
}
void Commands::homeAxis(bool xaxis,bool yaxis,bool zaxis)
{
#if !defined(HOMING_ORDER)
#define HOMING_ORDER HOME_ORDER_XYZ
#endif
#if HOMING_ORDER==HOME_ORDER_XYZ
    if(xaxis) homeXAxis();
    if(yaxis) homeYAxis();
    if(zaxis) homeZAxis();
#elif HOMING_ORDER==HOME_ORDER_XZY
    if(xaxis) homeXAxis();
    if(zaxis) homeZAxis();
    if(yaxis) homeYAxis();
#elif HOMING_ORDER==HOME_ORDER_YXZ
    if(yaxis) homeYAxis();
    if(xaxis) homeXAxis();
    if(zaxis) homeZAxis();
#elif HOMING_ORDER==HOME_ORDER_YZX
    if(yaxis) homeYAxis();
    if(zaxis) homeZAxis();
    if(xaxis) homeXAxis();
#elif HOMING_ORDER==HOME_ORDER_ZXY
    if(zaxis) homeZAxis();
    if(xaxis) homeXAxis();
    if(yaxis) homeYAxis();
#elif HOMING_ORDER==HOME_ORDER_ZYX
    if(zaxis) homeZAxis();
    if(yaxis) homeYAxis();
    if(xaxis) homeXAxis();
#endif
    UI_CLEAR_STATUS
}
#endif
void Commands::reportPrinterUsage()
{
#if EEPROM_MODE!=0
    float dist = printer.filamentPrinted*0.001+HAL::epr_get_float(EPR_PRINTING_DISTANCE);
    Com::printF(Com::tPrintedFilament,dist,2);
    Com::printF(Com::tSpacem);
    bool alloff = true;
    for(byte i=0; i<NUM_EXTRUDER; i++)
        if(tempController[i]->targetTemperatureC>15) alloff = false;

    long seconds = (alloff ? 0 : (HAL::timeInMilliseconds()-printer.msecondsPrinting)/1000)+HAL::epr_get_long(EPR_PRINTING_TIME);
    long tmp = seconds/86400;
    seconds-=tmp*86400;
    Com::printF(Com::tPrintingTime,tmp);
    tmp=seconds/3600;
    Com::printF(Com::tSpaceDaysSpace,tmp);
    seconds-=tmp*3600;
    tmp = seconds/60;
    Com::printF(Com::tSpaceHoursSpace,tmp);
    Com::printFLN(Com::tSpaceMin);
#endif
}
#if STEPPER_CURRENT_CONTROL==CURRENT_CONTROL_DIGIPOT
// Digipot methods for controling current and microstepping

#if defined(DIGIPOTSS_PIN) && DIGIPOTSS_PIN > -1
int digitalPotWrite(int address, unsigned int value) // From Arduino DigitalPotControl example
{
    digitalWrite(DIGIPOTSS_PIN,LOW); // take the SS pin low to select the chip
    SPI.transfer(address); //  send in the address and value via SPI:
    SPI.transfer(value);
    digitalWrite(DIGIPOTSS_PIN,HIGH); // take the SS pin high to de-select the chip:
    //delay(10);
}

void set_current(uint8_t driver, unsigned int current)
{
    const uint8_t digipot_ch[] = DIGIPOT_CHANNELS;
    digitalPotWrite(digipot_ch[driver], current);
}
#endif

void current_control_init() //Initialize Digipot Motor Current
{
#if DIGIPOTSS_PIN && DIGIPOTSS_PIN > -1
    const uint8_t digipot_motor_current[] = MOTOR_CURRENT;

    SPI.begin();
    SET_OUTPUT(DIGIPOTSS_PIN);
    for(int i=0; i<=4; i++)
        //digitalPotWrite(digipot_ch[i], digipot_motor_current[i]);
        set_current(i,digipot_motor_current[i]);
#endif
}
#endif

#if STEPPER_CURRENT_CONTROL==CURRENT_CONTROL_LTC2600

void set_current( byte channel, unsigned short level )
{
    const byte ltc_channels[] =  LTC2600_CHANNELS;
    if(channel>LTC2600_NUM_CHANNELS) return;
    byte address = ltc_channels[channel];
    char i;


    // NOTE: Do not increase the current endlessly. In case the engine reaches its current saturation, the engine and the driver can heat up and loss power.
    // When the saturation is reached, more current causes more heating and more power loss.
    // In case of engines with lower quality, the saturation current may be reached before the nominal current.

    // configure the pins
    WRITE( LTC2600_CS_PIN, HIGH );
    SET_OUTPUT( LTC2600_CS_PIN );
    WRITE( LTC2600_SCK_PIN, LOW );
    SET_OUTPUT( LTC2600_SCK_PIN );
    WRITE( LTC2600_SDI_PIN, LOW );
    SET_OUTPUT( LTC2600_SDI_PIN );

    // enable the command interface of the LTC2600
    WRITE( LTC2600_CS_PIN, LOW );

    // transfer command and address
    for( i=7; i>=0; i-- )
    {
        WRITE( LTC2600_SDI_PIN, address & (0x01 << i));
        WRITE( LTC2600_SCK_PIN, 1 );
        WRITE( LTC2600_SCK_PIN, 0 );
    }

    // transfer the data word
    for( i=15; i>=0; i-- )
    {
        WRITE( LTC2600_SDI_PIN, level & (0x01 << i));
        WRITE( LTC2600_SCK_PIN, 1 );
        WRITE( LTC2600_SCK_PIN, 0 );
    }

    // disable the ommand interface of the LTC2600 -
    // this carries out the specified command
    WRITE( LTC2600_CS_PIN, HIGH );

} // setLTC2600

void current_control_init() //Initialize LTC2600 Motor Current
{
    const unsigned int ltc_current[] =  MOTOR_CURRENT;
    byte i;
    for(i=0; i<LTC2600_NUM_CHANNELS; i++)
    {
        set_current(i, ltc_current[i] );
    }
}
#endif

#if defined(X_MS1_PIN) && X_MS1_PIN > -1
void microstep_ms(uint8_t driver, int8_t ms1, int8_t ms2)
{
    if(ms1 > -1) switch(driver)
        {
        case 0:
            WRITE( X_MS1_PIN,ms1);
            break;
        case 1:
            WRITE( Y_MS1_PIN,ms1);
            break;
        case 2:
            WRITE( Z_MS1_PIN,ms1);
            break;
        case 3:
            WRITE(E0_MS1_PIN,ms1);
            break;
        case 4:
            WRITE(E1_MS1_PIN,ms1);
            break;
        }
    if(ms2 > -1) switch(driver)
        {
        case 0:
            WRITE( X_MS2_PIN,ms2);
            break;
        case 1:
            WRITE( Y_MS2_PIN,ms2);
            break;
        case 2:
            WRITE( Z_MS2_PIN,ms2);
            break;
        case 3:
            WRITE(E0_MS2_PIN,ms2);
            break;
        case 4:
            WRITE(E1_MS2_PIN,ms2);
            break;
        }
}

void microstep_mode(uint8_t driver, uint8_t stepping_mode)
{
    switch(stepping_mode)
    {
    case 1:
        microstep_ms(driver,MICROSTEP1);
        break;
    case 2:
        microstep_ms(driver,MICROSTEP2);
        break;
    case 4:
        microstep_ms(driver,MICROSTEP4);
        break;
    case 8:
        microstep_ms(driver,MICROSTEP8);
        break;
    case 16:
        microstep_ms(driver,MICROSTEP16);
        break;
    }
}
void microstep_readings()
{
    Com::printFLN(Com::tMS1MS2Pins);
    Com::printF(Com::tXColon,READ(X_MS1_PIN));
    Com::printFLN(Com::tComma,READ(X_MS2_PIN));
    Com::printF(Com::tYColon,READ(Y_MS1_PIN));
    Com::printFLN(Com::tComma,READ(Y_MS2_PIN));
    Com::printF(Com::tZColon,READ(Z_MS1_PIN));
    Com::printFLN(Com::tComma,READ(Z_MS2_PIN));
    Com::printF(Com::tE0Colon,READ(E0_MS1_PIN));
    Com::printFLN(Com::tComma,READ(E0_MS2_PIN));
    Com::printF(Com::tE1Colon,READ(E1_MS1_PIN));
    Com::printFLN((Com::tComma,READ(E1_MS2_PIN));
}
#endif

void microstep_init()
{
#if defined(X_MS1_PIN) && X_MS1_PIN > -1
    const uint8_t microstep_modes[] = MICROSTEP_MODES;
    SET_OUTPUT(X_MS2_PIN);
    SET_OUTPUT(Y_MS2_PIN);
    SET_OUTPUT(Z_MS2_PIN);
    SET_OUTPUT(E0_MS2_PIN);
    SET_OUTPUT(E1_MS2_PIN);
    for(int i=0; i<=4; i++) microstep_mode(i,microstep_modes[i]);
#endif
}



/**
  \brief Execute the command stored in com.
*/
void Commands::executeGCode(GCode *com)
{
    unsigned long codenum; //throw away variable
#ifdef INCLUDE_DEBUG_COMMUNICATION
    if(Printer::debugCommunication())
    {
        if(com->hasG() || (com->hasM() && com->M!=111))
        {
            previous_millis_cmd = HAL::timeInMilliseconds();
            return;
        }
    }
#endif
    if(com->hasG())
    {
        switch(com->G)
        {
        case 0: // G0 -> G1
        case 1: // G1
            if(Printer::setDestinationStepsFromGCode(com)) // For X Y Z E F
#if DRIVE_SYSTEM == 3
                PrintLine::split_delta_move(ALWAYS_CHECK_ENDSTOPS, true, true);
#else
                PrintLine::queue_move(ALWAYS_CHECK_ENDSTOPS,true);
#endif
            break;
#if ARC_SUPPORT
        case 2: // CW Arc
        case 3: // CCW Arc MOTION_MODE_CW_ARC: case MOTION_MODE_CCW_ARC:
        {
            if(!Printer::setDestinationStepsFromGCode(com)) break; // For X Y Z E F
            float offset[2] = {(com->hasI()?com->I:0),(com->hasJ()?com->J:0)};
            if(unit_inches)
            {
                offset[0]*=25.4;
                offset[1]*=25.4;
            }
            float position[2] = {Printer::currentPositionSteps[0]*inv_axis_steps_per_unit[0],Printer::currentPositionSteps[1]*inv_axis_steps_per_unit[1]};
            float target[2] = {Printer::destinationSteps[0]*inv_axis_steps_per_unit[0],Printer::destinationSteps[1]*inv_axis_steps_per_unit[1]};
            float r;
            if (com->hasR())
            {
                /*
                  We need to calculate the center of the circle that has the designated radius and passes
                  through both the current position and the target position. This method calculates the following
                  set of equations where [x,y] is the vector from current to target position, d == magnitude of
                  that vector, h == hypotenuse of the triangle formed by the radius of the circle, the distance to
                  the center of the travel vector. A vector perpendicular to the travel vector [-y,x] is scaled to the
                  length of h [-y/d*h, x/d*h] and added to the center of the travel vector [x/2,y/2] to form the new point
                  [i,j] at [x/2-y/d*h, y/2+x/d*h] which will be the center of our arc.

                  d^2 == x^2 + y^2
                  h^2 == r^2 - (d/2)^2
                  i == x/2 - y/d*h
                  j == y/2 + x/d*h

                                                                       O <- [i,j]
                                                                    -  |
                                                          r      -     |
                                                              -        |
                                                           -           | h
                                                        -              |
                                          [0,0] ->  C -----------------+--------------- T  <- [x,y]
                                                    | <------ d/2 ---->|

                  C - Current position
                  T - Target position
                  O - center of circle that pass through both C and T
                  d - distance from C to T
                  r - designated radius
                  h - distance from center of CT to O

                  Expanding the equations:

                  d -> sqrt(x^2 + y^2)
                  h -> sqrt(4 * r^2 - x^2 - y^2)/2
                  i -> (x - (y * sqrt(4 * r^2 - x^2 - y^2)) / sqrt(x^2 + y^2)) / 2
                  j -> (y + (x * sqrt(4 * r^2 - x^2 - y^2)) / sqrt(x^2 + y^2)) / 2

                  Which can be written:

                  i -> (x - (y * sqrt(4 * r^2 - x^2 - y^2))/sqrt(x^2 + y^2))/2
                  j -> (y + (x * sqrt(4 * r^2 - x^2 - y^2))/sqrt(x^2 + y^2))/2

                  Which we for size and speed reasons optimize to:

                  h_x2_div_d = sqrt(4 * r^2 - x^2 - y^2)/sqrt(x^2 + y^2)
                  i = (x - (y * h_x2_div_d))/2
                  j = (y + (x * h_x2_div_d))/2

                */
                r = com->R;
                if(unit_inches) r*=25.4;
                // Calculate the change in position along each selected axis
                double x = target[0]-position[0];
                double y = target[1]-position[1];

                double h_x2_div_d = -sqrt(4 * r*r - x*x - y*y)/hypot(x,y); // == -(h * 2 / d)
                // If r is smaller than d, the arc is now traversing the complex plane beyond the reach of any
                // real CNC, and thus - for practical reasons - we will terminate promptly:
                if(isnan(h_x2_div_d))
                {
                    Com::printErrorFLN(Com::tInvalidArc);
                    break;
                }
                // Invert the sign of h_x2_div_d if the circle is counter clockwise (see sketch below)
                if (com->G==3)
                {
                    h_x2_div_d = -h_x2_div_d;
                }

                /* The counter clockwise circle lies to the left of the target direction. When offset is positive,
                   the left hand circle will be generated - when it is negative the right hand circle is generated.


                                                                 T  <-- Target position

                                                                 ^
                      Clockwise circles with this center         |          Clockwise circles with this center will have
                      will have > 180 deg of angular travel      |          < 180 deg of angular travel, which is a good thing!
                                                       \         |          /
                center of arc when h_x2_div_d is positive ->  x <----- | -----> x <- center of arc when h_x2_div_d is negative
                                                                 |
                                                                 |

                                                                 C  <-- Current position                                 */


                // Negative R is g-code-alese for "I want a circle with more than 180 degrees of travel" (go figure!),
                // even though it is advised against ever generating such circles in a single line of g-code. By
                // inverting the sign of h_x2_div_d the center of the circles is placed on the opposite side of the line of
                // travel and thus we get the unadvisably long arcs as prescribed.
                if (r < 0)
                {
                    h_x2_div_d = -h_x2_div_d;
                    r = -r; // Finished with r. Set to positive for mc_arc
                }
                // Complete the operation by calculating the actual center of the arc
                offset[0] = 0.5*(x-(y*h_x2_div_d));
                offset[1] = 0.5*(y+(x*h_x2_div_d));

            }
            else     // Offset mode specific computations
            {
                r = hypot(offset[0], offset[1]); // Compute arc radius for arc
            }

            // Set clockwise/counter-clockwise sign for arc computations
            uint8_t isclockwise = com->G == 2;

            // Trace the arc
            PrintLine::arc(position, target, offset,r, isclockwise);

            break;
        }
#endif
        case 4: // G4 dwell
            Commands::waitUntilEndOfAllMoves();
            codenum = 0;
            if(com->hasP()) codenum = com->P; // milliseconds to wait
            if(com->hasS()) codenum = com->S * 1000; // seconds to wait
            codenum += HAL::timeInMilliseconds();  // keep track of when we started waiting
            while((unsigned long)(codenum-HAL::timeInMilliseconds())  < 2000000000 )
            {
                GCode::readFromSerial();
                Commands::checkForPeriodicalActions();
            }
            break;
        case 20: // Units to inches
            unit_inches = 1;
            break;
        case 21: // Units to mm
            unit_inches = 0;
            break;
        case 28:  //G28 Home all Axis one at a time
        {
            byte home_all_axis = (com->hasNoXYZ());
            homeAxis(home_all_axis || com->hasX(),home_all_axis || com->hasY(),home_all_axis || com->hasZ());
        }
        break;
        case 90: // G90
            relative_mode = false;
            break;
        case 91: // G91
            relative_mode = true;
            break;
        case 92: // G92
            if(com->hasX()) Printer::currentPositionSteps[0] = com->X*axis_steps_per_unit[0]*(unit_inches?25.4:1.0)-printer.offsetX;
            if(com->hasY()) Printer::currentPositionSteps[1] = com->Y*axis_steps_per_unit[1]*(unit_inches?25.4:1.0)-printer.offsetY;
            if(com->hasZ()) Printer::currentPositionSteps[2] = com->Z*axis_steps_per_unit[2]*(unit_inches?25.4:1.0);
            if(com->hasE())
            {
                Printer::currentPositionSteps[3] = com->E*axis_steps_per_unit[3]*(unit_inches?25.4:1.0);
            }
            break;

        }
        previous_millis_cmd = HAL::timeInMilliseconds();
    }

    else if(com->hasM())    // Process M Code
    {

        switch( com->M )
        {
#if SDSUPPORT

        case 20: // M20 - list SD card
            sd.ls();
            break;
        case 21: // M21 - init SD card
            sd.mount();
            break;
        case 22: //M22 - release SD card
            sd.unmount();
            break;
        case 23: //M23 - Select file
            if(com->hasString())
                sd.selectFile(com->text);
            break;
        case 24: //M24 - Start SD print
            sd.startPrint();
            break;
        case 25: //M25 - Pause SD print
            sd.pausePrint();
            break;
        case 26: //M26 - Set SD index
            if(com->hasS())
                sd.setIndex(com->S);
            break;
        case 27: //M27 - Get SD status
            sd.printStatus();
            break;
        case 28: //M28 - Start SD write
            if(com->hasString())
                sd.startWrite(com->text);
            break;
        case 29: //M29 - Stop SD write
            //processed in write to file routine above
            //savetosd = false;
            break;
        case 30: // M30 filename - Delete file
            if(com->hasString())
                sd.deleteFile(com->text);
            break;
        case 32: // M32 directoryname
            if(com->hasString())
                sd.makeDirectory(com->text);
            break;
#endif
        case 42: //M42 -Change pin status via gcode
            if (com->hasS() && com->hasP() && com->S>=0 && com->S<=255)
            {
                int pin_number = com->P;
                for(byte i = 0; i < (byte)sizeof(sensitive_pins); i++)
                {
                    if (pgm_read_byte(&sensitive_pins[i]) == pin_number)
                    {
                        pin_number = -1;
                        break;
                    }
                }
                if (pin_number > -1)
                {
                    pinMode(pin_number, OUTPUT);
                    digitalWrite(pin_number, com->S);
                    analogWrite(pin_number, com->S);
                    Com::printF(Com::tSetOutputSpace,pin_number);
                    Com::printFLN(Com::tSpaceToSpace,(int)com->S);
                }
            }
            break;
        case 104: // M104
#if NUM_EXTRUDER>0
            if(reportTempsensorError()) break;
            previous_millis_cmd = HAL::timeInMilliseconds();
            if(Printer::debugDryrun()) break;
#ifdef EXACT_TEMPERATURE_TIMING
            Commands::waitUntilEndOfAllMoves();
#else
            if(com->hasP() || (com->hasS() && com->S == 0))
                Commands::waitUntilEndOfAllMoves();
#endif
            if (com->hasS())
            {
                if(com->hasT())
                    Extruder::setTemperatureForExtruder(com->S,com->T);
                else
                    Extruder::setTemperatureForExtruder(com->S,current_extruder->id);
            }
#endif
            break;
        case 140: // M140 set bed temp
            if(reportTempsensorError()) break;
            previous_millis_cmd = HAL::timeInMilliseconds();
            if(Printer::debugDryrun()) break;
            if (com->hasS()) Extruder::setHeatedBedTemperature(com->S);
            break;
        case 105: // M105  get temperature. Always returns the current temperature, doesn't wait until move stopped
            printTemperatures();
            break;
        case 109: // M109 - Wait for extruder heater to reach target.
        {
#if NUM_EXTRUDER>0
            if(reportTempsensorError()) break;
            previous_millis_cmd = HAL::timeInMilliseconds();
            if(Printer::debugDryrun()) break;
            UI_STATUS_UPD(UI_TEXT_HEATING_EXTRUDER);
            Commands::waitUntilEndOfAllMoves();
            Extruder *actExtruder = current_extruder;
            if(com->hasT() && com->T<NUM_EXTRUDER) actExtruder = &extruder[com->T];
            if (com->hasS()) Extruder::setTemperatureForExtruder(com->S,actExtruder->id);
#if defined(SKIP_M109_IF_WITHIN) && SKIP_M109_IF_WITHIN>0
            if(abs(actExtruder->tempControl.currentTemperatureC - actExtruder->tempControl.targetTemperatureC)<(SKIP_M109_IF_WITHIN)) break; // Already in range
#endif
            bool dir = actExtruder->tempControl.targetTemperature > actExtruder->tempControl.currentTemperature;
            millis_t printedTime = HAL::timeInMilliseconds();
            millis_t waituntil = 0;
#if RETRACT_DURING_HEATUP
            byte retracted = 0;
#endif
            millis_t cur_time;
            do
            {
                cur_time = HAL::timeInMilliseconds();
                if( (cur_time - printedTime) > 1000 )   //Print Temp Reading every 1 second while heating up.
                {
                    printTemperatures();
                    printedTime = cur_time;
                }
                Commands::checkForPeriodicalActions();
                //gcode_read_serial();
#if RETRACT_DURING_HEATUP
                if (actExtruder==current_extruder && actExtruder->waitRetractUnits > 0 && !retracted && dir && actExtruder->tempControl.currentTemperatureC > actExtruder->waitRetractTemperature)
                {
                    PrintLine::moveRelativeDistanceInSteps(0,0,0,-actExtruder->waitRetractUnits * axis_steps_per_unit[3],actExtruder->maxFeedrate,false,false);
                    retracted = 1;
                }
#endif
                if((waituntil==0 && (dir ? actExtruder->tempControl.currentTemperatureC >= actExtruder->tempControl.targetTemperatureC-1:actExtruder->tempControl.currentTemperatureC <= actExtruder->tempControl.targetTemperatureC+1))
#ifdef TEMP_HYSTERESIS
                        || (waituntil!=0 && (abs(actExtruder->tempControl.currentTemperatureC - actExtruder->tempControl.targetTemperatureC))>TEMP_HYSTERESIS)
#endif
                  )
                {
                    waituntil = cur_time+1000UL*(unsigned long)actExtruder->watchPeriod; // now wait for temp. to stabalize
                }
            }
            while(waituntil==0 || (waituntil!=0 && (unsigned long)(waituntil-cur_time)<2000000000UL));
#if RETRACT_DURING_HEATUP
            if (retracted && actExtruder==current_extruder)
            {
                PrintLine::moveRelativeDistanceInSteps(0,0,0,actExtruder->waitRetractUnits * axis_steps_per_unit[3],actExtruder->maxFeedrate,false,false);
            }
#endif
        }
        UI_CLEAR_STATUS;
#endif
        previous_millis_cmd = HAL::timeInMilliseconds();
        break;
        case 190: // M190 - Wait bed for heater to reach target.
#if HAVE_HEATED_BED
            if(Printer::debugDryrun()) break;
            UI_STATUS_UPD(UI_TEXT_HEATING_BED);
            Commands::waitUntilEndOfAllMoves();
#if HAVE_HEATED_BED
            if (com->hasS()) Extruder::setHeatedBedTemperature(com->S);
#if defined(SKIP_M190_IF_WITHIN) && SKIP_M190_IF_WITHIN>0
            if(abs(heatedBedController.currentTemperatureC-heatedBedController.targetTemperatureC)<SKIP_M190_IF_WITHIN) break;
#endif
            codenum = HAL::timeInMilliseconds();
            while(heatedBedController.currentTemperatureC+0.5<heatedBedController.targetTemperatureC)
            {
                if( (HAL::timeInMilliseconds()-codenum) > 1000 )   //Print Temp Reading every 1 second while heating up.
                {
                    printTemperatures();
                    codenum = HAL::timeInMilliseconds();
                }
                Commands::checkForPeriodicalActions();
            }
#endif
#endif
            UI_CLEAR_STATUS;
            previous_millis_cmd = HAL::timeInMilliseconds();
            break;
#ifdef BEEPER_PIN
        case 300:
        {
            int beepS = 1;
            int beepP = 1000;
            if(com->hasS()) beepS = com->S;
            if(com->hasP()) beepP = com->P;
            HAL::tone(BEEPER_PIN, beepS);
            HAL::delayMilliseconds(beepP);
            HAL::noTone(BEEPER_PIN);
        }
        break;
#endif
        case 303:
        {
#if defined(TEMP_PID) && NUM_TEMPERATURE_LOOPS>0
            int temp = 150;
            int cont = 0;
            if(com->hasS()) temp = com->S;
            if(com->hasP()) cont = com->P;
            if(cont>=NUM_TEMPERATURE_LOOPS) cont = NUM_TEMPERATURE_LOOPS;
            tempController[cont]->autotunePID(temp,cont);
#endif
        }
        break;
#if FAN_PIN>-1 && FEATURE_FAN_CONTROL
        case 106: //M106 Fan On
            setFanSpeed(com->hasS()?com->S:255,com->hasP());
            break;
        case 107: //M107 Fan Off
            setFanSpeed(0,com->hasP());
            break;
#endif
        case 80: // M80 - ATX Power On
#if PS_ON_PIN>-1
            Commands::waitUntilEndOfAllMoves();
            previous_millis_cmd = HAL::timeInMilliseconds();
            SET_OUTPUT(PS_ON_PIN); //GND
            WRITE(PS_ON_PIN, (POWER_INVERTING ? HIGH : LOW));
#endif
            break;
        case 81: // M81 - ATX Power Off
#if PS_ON_PIN>-1
            Commands::waitUntilEndOfAllMoves();
            SET_OUTPUT(PS_ON_PIN); //GND
            WRITE(PS_ON_PIN,(POWER_INVERTING ? LOW : HIGH));
#endif
            break;
        case 82:
            relative_mode_e = false;
            break;
        case 83:
            relative_mode_e = true;
            break;
        case 84:
            if(com->hasS())
            {
                stepper_inactive_time = com->S * 1000;
            }
            else
            {
                Commands::waitUntilEndOfAllMoves();
                Printer::kill(true);
            }
            break;
        case 85: // M85
            if(com->hasS())
                max_inactive_time = (long)com->S * 1000;
            else
                max_inactive_time = 0;
            break;
        case 92: // M92
            if(com->hasX()) axis_steps_per_unit[0] = com->X;
            if(com->hasY()) axis_steps_per_unit[1] = com->Y;
            if(com->hasZ()) axis_steps_per_unit[2] = com->Z;
            if(com->hasE()) current_extruder->stepsPerMM = com->E;
            Printer::updateDerivedParameter();
            break;
        case 111:
            if(com->hasS()) Printer::debugLevel = com->S;
            if(Printer::debugDryrun())   // simulate movements without printing
            {
                Extruder::setTemperatureForExtruder(0,0);
#if NUM_EXTRUDER>1
                Extruder::setTemperatureForExtruder(0,1);
#endif
#if HEATED_BED_TYPE!=0
                target_bed_raw = 0;
#endif
            }
            break;
        case 115:
            Com::printFLN(Com::tFirmware);
            reportPrinterUsage();
            break;
        case 114: // M114
            printCurrentPosition();
            break;
        case 117: // M117 message to lcd
            if(com->hasString())
            {
                UI_STATUS_UPD_RAM(com->text);
            }
            break;
        case 119: // M119
            Commands::waitUntilEndOfAllMoves();
#if (X_MIN_PIN > -1) && MIN_HARDWARE_ENDSTOP_X
            Com::printF(Com::tXMinColon);
            Com::printF(Printer::isXMinEndstopHit()?Com::tHSpace:Com::tLSpace);
#endif
#if (X_MAX_PIN > -1) && MAX_HARDWARE_ENDSTOP_X
            Com::printF(Com::tXMaxColon);
            Com::printF(Printer::isXMaxEndstopHit()?Com::tHSpace:Com::tLSpace);
#endif
#if (Y_MIN_PIN > -1) && MIN_HARDWARE_ENDSTOP_Y
            Com::printF(Com::tYMinColon);
            Com::printF(Printer::isYMinEndstopHit()?Com::tHSpace:Com::tLSpace);
#endif
#if (Y_MAX_PIN > -1) && MAX_HARDWARE_ENDSTOP_Y
            Com::printF(Com::tYMaxColon);
            Com::printF(Printer::isYMaxEndstopHit()?Com::tHSpace:Com::tLSpace);
#endif
#if (Z_MIN_PIN > -1) && MIN_HARDWARE_ENDSTOP_Z
            Com::printF(Com::tZMinColon);
            Com::printF(Printer::isZMinEndstopHit()?Com::tHSpace:Com::tLSpace);
#endif
#if (Z_MAX_PIN > -1) && MAX_HARDWARE_ENDSTOP_Z
            Com::printF(Com::tZMaxColon);
            Com::printF(Printer::isZMaxEndstopHit()?Com::tHSpace:Com::tLSpace);
#endif
            Com::println();
            break;
#if BEEPER_TYPE>0
        case 120: // Test beeper function
            if(com->hasS() && com->hasP())
                beep(com->S,com->P); // Beep test
            break;
#endif
#ifdef RAMP_ACCELERATION
        case 201: // M201
            if(com->hasX()) max_acceleration_units_per_sq_second[0] = com->X;
            if(com->hasY()) max_acceleration_units_per_sq_second[1] = com->Y;
            if(com->hasZ()) max_acceleration_units_per_sq_second[2] = com->Z;
            if(com->hasE()) max_acceleration_units_per_sq_second[3] = com->E;
            Printer::updateDerivedParameter();
            break;
        case 202: // M202
            if(com->hasX()) max_travel_acceleration_units_per_sq_second[0] = com->X;
            if(com->hasY()) max_travel_acceleration_units_per_sq_second[1] = com->Y;
            if(com->hasZ()) max_travel_acceleration_units_per_sq_second[2] = com->Z;
            if(com->hasE()) max_travel_acceleration_units_per_sq_second[3] = com->E;
            Printer::updateDerivedParameter();
            break;
#endif
        case 203: // M203 Temperature monitor
            if(com->hasS())
            {
                if(com->S<NUM_EXTRUDER) manage_monitor = com->S;
#if HAVE_HEATED_BED
                else manage_monitor=NUM_EXTRUDER; // Set 100 to heated bed
#endif
            }
            break;
        case 204:
        {
            TemperatureController *temp = &current_extruder->tempControl;
            if(com->hasS())
            {
                if(com->S<0) break;
                if(com->S<NUM_EXTRUDER) temp = &extruder[com->S].tempControl;
#if HAVE_HEATED_BED
                else temp = &heatedBedController;
#else
                else break;
#endif
            }
            if(com->hasX()) temp->pidPGain = com->X;
            if(com->hasY()) temp->pidIGain = com->Y;
            if(com->hasZ()) temp->pidDGain = com->Z;
            temp->updateTempControlVars();
        }
        break;
        case 503:
        case 205: // M205 Show EEPROM settings
            EEPROM::writeSettings();
            break;
        case 206: // M206 T[type] P[pos] [Sint(long] [Xfloat]  Set eeprom value
            EEPROM::update(com);
            break;
        case 207: // M207 X<XY jerk> Z<Z Jerk>
            if(com->hasX())
                printer.maxJerk = com->X;
            if(com->hasZ())
                printer.maxZJerk = com->Z;
            if(com->hasE())
            {
                current_extruder->maxStartFeedrate = com->E;
                Extruder::selectExtruderById(current_extruder->id);
            }
            Com::printF(Com::tJerkColon,printer.maxJerk);
            Com::printFLN(Com::tZJerkColon,printer.maxZJerk);
            break;
        case 220: // M220 S<Feedrate multiplier in percent>
            changeFeedrateMultiply(com->getS(100));
            break;
        case 221: // M221 S<Extrusion flow multiplier in percent>
            changeFlowateMultiply(com->getS(100));
            break;
#ifdef USE_ADVANCE
        case 223: // Extruder interrupt test
            if(com->hasS())
                printer.extruderStepsNeeded+=com->S;
            break;
        case 232:
            Com::printF(Com::tLinearStepsColon,maxadv2);
#ifdef ENABLE_QUADRATIC_ADVANCE
            Com::printF(Com::tQuadraticStepsColon,maxadv);
#endif
            Com::printFLN(Com::tCommaSpeedEqual,maxadvspeed);
#ifdef ENABLE_QUADRATIC_ADVANCE
            maxadv=0;
#endif
            maxadv2=0;
            maxadvspeed=0;
            break;
#endif
#if USE_OPS==1
        case 231: // M231 S<OPS_MODE> X<Min_Distance> Y<Retract> Z<Backslash> F<ReatrctMove>
            if(com->hasS() && com->S>=0 && com->S<3)
                printer.opsMode = com->S;
            if(com->hasX() && com->X>=0)
                printer.opsMinDistance = com->X;
            if(com->hasY() && com->Y>=0)
                printer.opsRetractDistance = com->Y;
            if(com->hasZ() && com->Z>=-printer.opsRetractDistance)
                printer.opsRetractBacklash = com->Z;
            if(com->hasF() && com->F>=0 && com->F<=100)
                printer.opsMoveAfter = com->F;
            Extruder::selectExtruderById(current_extruder->id);
            if(printer.opsMode==0)
            {
                Com::printFLN(Com::tOPSDisabled);
            }
            else
            {
                if(printer.opsMode==1)
                    Com::printFLN(Com::tOPSClassicMode);
                else
                    Com::printFLN(Com::tOPSFastMode);

                Com::printF(Com::tMinDistance,printer.opsMinDistance);
                Com::printF(Com::tRetractEqual,printer.opsRetractDistance);
                Com::printF(Com::tBacklashEqual,printer.opsRetractBacklash);
                if(printer.opsMode==2)
                    Com::printF(Com::tMoveAfter,printer.opsMoveAfter);
                Com::println();
                Printer::updateAdvanceFlags();
            }
#ifdef DEBUG_OPS
            Com::printFLN(Com::tRetrSteps,printer.opsRetractSteps);
            Com::printFLN(Com::tPushBackSteps,printer.opsPushbackSteps);
            Com::printFLN(Com::tMoveAfterSteps,printer.opsMoveAfterSteps);
#endif
            break;
#endif
#ifdef USE_ADVANCE
        case 233:
            if(com->hasY())
                current_extruder->advanceL = com->Y;
            Com::printF(Com::tLinearLColon,current_extruder->advanceL);
#ifdef ENABLE_QUADRATIC_ADVANCE
            if(com->hasX())
                current_extruder->advanceK = com->X;
            Com::printF(Com::tQuadraticKColon,current_extruder->advanceK);
#endif
            Com::println();
            Printer::updateAdvanceFlags();
            break;
#endif
        case 400: // Finish all moves
            Commands::waitUntilEndOfAllMoves();
            break;
#if FEATURE_MEMORY_POSITION
        case 401: // Memory position
            printer.memoryX = Printer::currentPositionSteps[0];
            printer.memoryY = Printer::currentPositionSteps[1];
            printer.memoryZ = Printer::currentPositionSteps[2];
            break;
        case 402: // Go to stored position
        {
            bool all = !(com->hasX() && com->hasY() && com->hasZ());
            PrintLine::moveRelativeDistanceInSteps((all || com->hasX() ? printer.memoryX-Printer::currentPositionSteps[0] : 0)
                                                   ,(all || com->hasY() ? printer.memoryY-Printer::currentPositionSteps[1] : 0)
                                                   ,(all || com->hasZ() ? printer.memoryZ-Printer::currentPositionSteps[2] : 0)
                                                   ,0,(com->hasF() ? com->F : printer.feedrate),false,ALWAYS_CHECK_ENDSTOPS);
        }
        break;
#endif
        case 908: // Control digital trimpot directly.
        {
#if STEPPER_CURRENT_CONTROL != CURRENT_CONTROL_MANUAL
            uint8_t channel,current;
            if(com->hasP() && com->hasS())
                set_current((uint8_t)com->P, (unsigned int)com->S);
#endif
        }
        break;
        case 500:
        {
#if EEPROM_MODE!=0
            EEPROM::storeDataIntoEEPROM(false);
            Com::printInfoF(Com::tConfigStoredEEPROM);
#else
            Com::printErrorF(tNoEEPROMSupport);
#endif
        }
        break;
        case 501:
        {
#if EEPROM_MODE!=0
            EEPROM::readDataFromEEPROM();
            Com::printInfoF(Com::tConfigLoadedEEPROM);
#else
            Com::printErrorF(tNoEEPROMSupport);
#endif
        }
        break;
        case 502:
            EEPROM::restoreEEPROMSettingsFromConfiguration();
            break;

        case 350: // Set microstepping mode. Warning: Steps per unit remains unchanged. S code sets stepping mode for all drivers.
        {
            OUT_P_LN("Set Microstepping");
#if defined(X_MS1_PIN) && X_MS1_PIN > -1
            if(com->hasS()) for(int i=0; i<=4; i++) microstep_mode(i,com->S);
            if(com->hasX()) microstep_mode(0,(uint8_t)com->X);
            if(com->hasY()) microstep_mode(1,(uint8_t)com->Y);
            if(com->hasZ()) microstep_mode(2,(uint8_t)com->Z);
            if(com->hasE()) microstep_mode(3,(uint8_t)com->E);
            if(com->hasP()) microstep_mode(4,com->P); // Original B but is not supported here
            microstep_readings();
#endif
        }
        break;
#ifdef STEP_COUNTER
#if DRIVE_SYSTEM==3
        case 251:
            if(com->hasS())
            {
                if (com->S == 0)
                {
                    printer.countZSteps = 0;
                    Com::printFLN(Com::tMeasurementReset);
                }
                else if (com->S == 1)
                {
                    Com::printFLN(Com::tMeasureDeltaSteps,printer.countZSteps * inv_axis_steps_per_unit[2]);
                    Com::printFLN(Com::tMeasureDelta,printer.countZSteps * inv_axis_steps_per_unit[2]);
                }
                else if (com->S = 2)
                {
                    if (printer.countZSteps < 0)
                        printer.countZSteps = -printer.countZSteps;
                    printer.zMin = 0;
                    printer.zLength = inv_axis_steps_per_unit[2] * printer.countZSteps;
                    printer.zMaxSteps = printer.countZSteps;
                    for (byte i=0; i<3; i++)
                    {
                        Printer::currentPositionSteps[i] = 0;
                    }
                    calculate_delta(Printer::currentPositionSteps, printer.currentDeltaPositionSteps);
                    Com::printFLN(Com::tMeasureOriginReset);
#if EEPROM_MODE!=0
                    EEPROM::storeDataIntoEEPROM(false);
                    Com::printFLN(Com::tEEPROMUpdated);
#endif
                }
            }
            break;
#endif
#endif
        }
    }
    else if(com->hasT())      // Process T code
    {
        Commands::waitUntilEndOfAllMoves();
        Extruder::selectExtruderById(com->T);
    }
    else
    {
        if(Printer::debugErrors())
        {
            Com::printF(Com::tUnknownCommand);
            com->printCommand();
        }
    }
}
void Commands::emergencyStop()
{
#if defined(KILL_METHOD) && KILL_METHOD==1
    HAL::resetHardware();
#else
    HAL::forbidInterrupts(); // Don't allow interrupts to do their work
    kill(false);
    Extruder::manageTemperatures();
    for(byte i=0; i<NUM_EXTRUDER+3; i++)
        pwm_pos[i] = 0;
#if EXT0_HEATER_PIN>-1
    WRITE(EXT0_HEATER_PIN,0);
#endif
#if defined(EXT1_HEATER_PIN) && EXT1_HEATER_PIN>-1 && NUM_EXTRUDER>1
    WRITE(EXT1_HEATER_PIN,0);
#endif
#if defined(EXT2_HEATER_PIN) && EXT2_HEATER_PIN>-1 && NUM_EXTRUDER>2
    WRITE(EXT2_HEATER_PIN,0);
#endif
#if FAN_PIN>-1
    WRITE(FAN_PIN,0);
#endif
    while(1) {}
#endif
}

void Commands::checkFreeMemory() {
    int newfree = HAL::getFreeRam();
    if(newfree<lowestRAMValue)
    {
        lowestRAMValue = newfree;
    }
}
void Commands::writeLowestFreeRAM()
{
    if(lowestRAMValueSend>lowestRAMValue)
    {
        lowestRAMValueSend = lowestRAMValue;
        Com::printFLN(Com::tFreeRAM,lowestRAMValue);
    }

}
