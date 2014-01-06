/*
 * SensorCov().c
 *
 *  Created on: Oct 30, 2013
 *      Author: Nathan
 */

#include "all.h"
#include "MCP2515_spi.h"	//SPI initialization functions
#include "MCP2515.h"		//MCP2515 functions
#include "MCP2515_DEFS.h"

ops_struct ops_temp;
data_struct data_temp;
stopwatch_struct* conv_watch;

void SensorCov()
{
	SensorCovInit();
	while (ops.State == STATE_SENSOR_COV)
	{
		LatchStruct();
		SensorCovMeasure();
		UpdateStruct();
		FillCANData();
	}
	SensorCovDeInit();
}

void SensorCovInit()
{

	MCP2515_spi_init();								//initialize SPI port and GPIO associated with the MCP2515
	RamInitMCP2515(((1<<CANFREQ)-1), MaskConfig);	//initialize MCP2515

	//CONFIG GP_BUTTON
	ConfigGPButton();

	//CONFIG LEDS
	//led 0
	ConfigLED0();
	//led 1
	ConfigLED1();

	conv_watch = StartStopWatch(4);
}


void LatchStruct()
{
	memcpy(&ops_temp, &ops, sizeof(struct OPERATIONS));
	memcpy(&data_temp, &data, sizeof(struct DATA));
	ops.Change.all = 0;	//clear change states
}

void SensorCovMeasure()
{
	unsigned int CANmessage_raw[13];

	data_temp.gp_button = READGPBUTTON();

	//todo CAN Mirror:
	if(GpioDataRegs.GPADAT.bit.GPIO20 == 0 ) // poll MCP2515 interrupt pin(active low)
	{
		//read CAN message from MCP2515
		SR2_SPI(MCP_READ, MCP_RXB0SIDH, 13, CANmessage_raw);	//read raw can message
		MCP2515Write(MCP_CANINTF, 0x00);						//clear interrupt

		//reset Stopwatch
		StopWatchRestart(conv_watch);

		//Send CAN message on native CAN interface
	}

	//todo
//	if(stopwatch overflow)
//	{
//		//indicate CAN bus A down
//	}

}

void UpdateStruct()
{
	memcpy(&data, &data_temp, sizeof(struct DATA));

	//todo USER: UpdateStruct
	//update with node specific op changes

	//if ops is not changed outside of sensor conversion copy temp over, otherwise don't change

	//Change bit is only set by ops changes outside of SensorCov.
	if (ops.Change.bit.State == 0)
	{
		ops.State = ops_temp.State;
	}

	if (ops.Change.bit.Flags == 0)
	{
		//only cov error happens inside of conversion so all other changes are considered correct.
		//update accordingly to correct cov_errors
		ops.Flags.bit.cov_error = ops_temp.Flags.bit.cov_error;
	}
	ops.Change.all = 0;	//clear change states
}

void SensorCovDeInit()
{
	//todo USER: SensorCovDeInit()
	MCP2515_reset(1);				//hold MCP2515 in reset
	StopStopWatch(conv_watch);
	CLEARLED0();
	CLEARLED1();
}
