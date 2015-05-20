/*
 * SensorCov().c
 *
 *  Created on: Jan 5, 2014
 *      Author: Jenn
 */

#include "all.h"
#include "MCP2515_spi.h"	//SPI initialization functions
#include "MCP2515.h"		//MCP2515 functions
#include "MCP2515_DEFS.h"

void MCP2515_Total_Reset(long delay);								//does a total reset of the MCP2515 system. stays off bus for delay before coming back on line.


stopwatch_struct* mirror_Ato2_watch;
stopwatch_struct* mirror_2toA_watch;
stopwatch_struct* canA_watch;
stopwatch_struct* can2_watch;


ops_struct ops_temp;
data_struct data_temp;

//buffers
buffer_struct Buf_Ato2;
buffer_struct Buf_2toA;

int MCP2515_Send_State = 0;
int MCP2515_Receive_State = 0;
int MCP2515_Error_State = 0;

unsigned int MCP_ShadowRegs[4];									//shadow registers for CANINTF, EFLG, CANSTAT, CANCTRL

void SensorCov()
{
	SensorCovInit();
	while (ops.State == STATE_SENSOR_COV)
	{
		LatchStruct();
		SensorCovMeasure();
	//	UpdateStruct();
		FillCANData();
	}
	SensorCovDeInit();
}

void SensorCovInit()
{
	//CONFIG GP_BUTTON
	ConfigGPButton();
	ConfigLED0();
	ConfigLED1();
	SETLED0();
	SETLED1();

	MCP2515_Total_Reset(0);

	mirror_Ato2_watch = StartStopWatch(100);		//start stopwatch for Ato2 timeout
	mirror_2toA_watch = StartStopWatch(100);    	//start stopwatch for 2toA timeout
	canA_watch = StartStopWatch(50000);				//start stopwatch for canA bus status
	can2_watch = StartStopWatch(50000);				//start stopwatch for can2 bus status
}


void LatchStruct()
{
	memcpy(&ops_temp, &ops, sizeof(struct OPERATIONS));
	memcpy(&data_temp, &data, sizeof(struct DATA));
	ops.Change.all = 0;	//clear change states
}

void SensorCovMeasure()
{
	int tmp;
	struct ECAN_REGS ECanaShadow;	//shadow structure for modifying CAN registers

//*********************************************************************MCP2515 service*******************************************************************************************

	//MCP2515 interrupt handler
	if(GpioDataRegs.GPADAT.bit.GPIO20 == 0 ) // poll MCP2515 interrupt pin(active low)
	{
		MCP2515ReadBlock(MCP_CANINTF, MCP_ShadowRegs, 2);		//read the status registers
		//MCP_ShadowRegs[0] = CANINTF
		//MCP_ShadowRegs[1] = EFLG

		if(MCP_ShadowRegs[0] & MCP_CANINTF_MERRF)
		{
			MCP2515Write(MCP_CANINTF, (MCP_ShadowRegs[0] | ~MCP_CANINTF_MERRF)); // clear interrupt flag
			//this catches all message transmit/receive errors
		}

		if(MCP_ShadowRegs[0] & MCP_CANINTF_WAKIF)
		{
			MCP2515Write(MCP_CANINTF, (MCP_ShadowRegs[0] | ~MCP_CANINTF_WAKIF)); // clear interrupt flag
		}

		if(MCP_ShadowRegs[0] & MCP_CANINTF_ERRIF)
		{
			MCP2515Write(MCP_CANINTF, (MCP_ShadowRegs[0] | ~MCP_CANINTF_ERRIF)); // clear interrupt flag
			//check EFLG to see what generated the error
			if(MCP_ShadowRegs[1] & MCP_EFLG_RX1OVR)
				MCP2515Write(MCP_EFLG, MCP_ShadowRegs[1] | 0x40);	//clear RXB1 overflow error
			if(MCP_ShadowRegs[1] & MCP_EFLG_RX0OVR);
				MCP2515Write(MCP_EFLG, MCP_ShadowRegs[1] | 0x80);	//clear RXB0 overflow error
			if(MCP_ShadowRegs[1] & MCP_EFLG_TXBO);
			if(MCP_ShadowRegs[1] & MCP_EFLG_TXEP);
			if(MCP_ShadowRegs[1] & MCP_EFLG_RXEP);
			if(MCP_ShadowRegs[1] & MCP_EFLG_TXWAR);
			if(MCP_ShadowRegs[1] & MCP_EFLG_RXWAR);
			if(MCP_ShadowRegs[1] & MCP_EFLG_EWARN);
		}

		if(MCP_ShadowRegs[0] & MCP_CANINTF_TX2IF)
		{
			MCP2515Write(MCP_CANINTF, (MCP_ShadowRegs[0] | ~MCP_CANINTF_TX2IF)); // clear interrupt flag
		}

		if(MCP_ShadowRegs[0] & MCP_CANINTF_TX1IF)
		{
			MCP2515Write(MCP_CANINTF, (MCP_ShadowRegs[0] | ~MCP_CANINTF_TX1IF)); // clear interrupt flag
		}

		if(MCP_ShadowRegs[0] & MCP_CANINTF_TX0IF)
		{
			MCP2515Write(MCP_CANINTF, (MCP_ShadowRegs[0] | ~MCP_CANINTF_TX0IF)); // clear interrupt flag
			//This should signal successful transmission
			if((MCP2515Read(MCP_TXB0CTRL) & 0x78) == 0)					//check for no errors
				MCP2515_Send_State = 1;
			else
				MCP2515_Send_State = 4;
		}

		if(MCP_ShadowRegs[0] & MCP_CANINTF_RX1IF)
		{
			MCP2515Write(MCP_CANINTF, (MCP_ShadowRegs[0] | ~MCP_CANINTF_RX1IF)); // clear interrupt flag
			MCP2515_Receive_State = 2;
		}

		if(MCP_ShadowRegs[0] & MCP_CANINTF_RX0IF)
		{
			MCP2515Write(MCP_CANINTF, (MCP_ShadowRegs[0] | ~MCP_CANINTF_RX0IF)); // clear interrupt flag
			MCP2515_Receive_State = 1;
		}

	}//end MCP2515 interrupt handler


	//Send state machine (sends messages from 2 to A)
	switch(MCP2515_Send_State)
	{
	case 1: // attempt to send
			tmp = Buffer_MCPFillMessage(&Buf_2toA, 0);	//attempt to add message from buffer
			if(tmp != -1)								//if add was successful,
			{
				ops.Counters.fields.Buf2toA = tmp;		//keep count up to date
				MCP2515Write(MCP_TXB0CTRL,0x0B);		//flag message for transmission
				StopWatchRestart(mirror_2toA_watch);	//start timeout counter
				MCP2515_Send_State = 2;					//go to timeout wait
			}
			else
			{
				//there were no messages to send
			}
		break;

	case 2: // wait for message send timeout, interrupt handler will change state otherwise
			if(isStopWatchComplete(mirror_2toA_watch) == 1)
			{
				//Timeout error for send message on canA
				MCP2515_Send_State = 3;					//go to abort message
			}
		break;

	case 3: //abort message
			MCP2515Write(MCP_TXB0CTRL, 0x00);				//clear transmit request flag
			MCP2515_Send_State = 0;
		break;
	case 4: //error handler
		break;

	default: // Waiting state, check if transmitter status is ok.
			if((MCP2515Read(MCP_TXB0CTRL) & 0x08) == 0)		//check status of transmitter
				MCP2515_Send_State = 1;						//go to send messages state
		break;
	}

	//receive state machine (gets messages from A destined for 2)
	switch(MCP2515_Receive_State)
	{
	case 1:	//Get message from RXB0
		tmp = Buffer_MCPGetMessage(&Buf_Ato2, 0);
		if (tmp != -1)
		{
			ops.Counters.fields.BufAto2 = tmp;
		}
		else
		{
			tmp=1;						//todo handle full buffer here
		}

		MCP2515_Receive_State = 0;		//go back to waiting
		break;
	case 2: //Get message from RXB1
		tmp = Buffer_MCPGetMessage(&Buf_Ato2, 0);
		if (tmp != -1)
		{
			ops.Counters.fields.BufAto2 = tmp;
		}
		else
		{
			tmp=1;						//todo handle full buffer here
		}

		MCP2515_Receive_State = 0;		//go back to waiting
		break;
	case 3: //Error handler
		break;
	default:
		//default do nothing Interrupt handler moves state from 0 to something else
		break;
	}

	//error state machine
	switch(MCP2515_Error_State)
	{
	}

//*****************************************************************end MCP2515 service********************************************************************************************

//***********************************************This code takes care of sending messages out on CAN bus 2************************************************************************
	ECanaShadow.CANTRS.all = ECanaRegs.CANTRS.all;			//get CAN transmit status register
	if ((ECanaShadow.CANTRS.all & (1 << 0x04)) == 0)		//check if mailbox 4 is not sending
	{
		if(CANQueueEMPTY == 0)								//check if there are messages to send
		{
			StopWatchRestart(can2_watch);					//restart stopwatch for timeout

			//set up the actual CAN transmission
			EALLOW;
				//set up mailbox(4)
				//some values are dependent on message, ie the DLC and the message ID
				ECanaShadow.CANME.all = ECanaRegs.CANME.all;	//get current CAN registers
				ECanaShadow.CANMD.all = ECanaRegs.CANMD.all;	//get current CAN registers

				ECanaShadow.CANME.bit.ME4 = 0;
				ECanaRegs.CANME.all = ECanaShadow.CANME.all;	//disable mailbox so we can change the ID

				//message ID is: bit 31 = EXID, bit 30,29 = acceptance bits, 0 for transmit, 28:0 ID, ID is left aligned
				ECanaMboxes.MBOX4.MSGID.all = 0;
				if (CANQueue_raw[CANQueueOUT][1] & 0x0008)
				{//extended ID
					ECanaMboxes.MBOX4.MSGID.all = (0x80000000L | (CANQueue_raw[CANQueueOUT][1] & 0x00000003L) << 27 | (CANQueue_raw[CANQueueOUT][2] & 0x000000FFL) << 19 | (CANQueue_raw[CANQueueOUT][3] & 0x000000FFL) << 10 | (CANQueue_raw[CANQueueOUT][0] & 0x000000FFL) << 3 | (CANQueue_raw[CANQueueOUT][1] & 0x000000E0L) >> 5);
				}
				else
				{//standard ID
					ECanaMboxes.MBOX4.MSGID.bit.STDMSGID = (0x00000000L | (CANQueue_raw[CANQueueOUT][0] & 0x000000FFL) << 3 | (CANQueue_raw[CANQueueOUT][1] & 0x000000E0L) >> 5);
				}

				ECanaMboxes.MBOX4.MSGCTRL.bit.DLC = CANQueue_raw[CANQueueOUT][4] & 0x000F;			//DLC
				ECanaShadow.CANMD.bit.MD4 = 0; 			//transmit
				ECanaShadow.CANME.bit.ME4 = 1;			//enable

				ECanaRegs.CANMD.all = ECanaShadow.CANMD.all;
				ECanaRegs.CANME.all = ECanaShadow.CANME.all;

				//fill mailbox with data from buf
				ECanaShadow.CANMC.all = ECanaRegs.CANMC.all;	//get regs
				ECanaShadow.CANMC.bit.MBNR = 0x04;				//request write to mailbox 4
				ECanaShadow.CANMC.bit.CDR = 1;					//change data field request
				ECanaRegs.CANMC.all = ECanaShadow.CANMC.all;	//set regs

				//set data
				ECanaMboxes.MBOX4.MDL.all = (CANQueue_raw[CANQueueOUT][5] & 0x000000FFL) | (CANQueue_raw[CANQueueOUT][6] & 0x000000FFL) << 8 | (CANQueue_raw[CANQueueOUT][7] & 0x000000FFL) << 16 | (CANQueue_raw[CANQueueOUT][8] & 0x000000FFL) << 24;					//fill data
				ECanaMboxes.MBOX4.MDH.all = (CANQueue_raw[CANQueueOUT][9] & 0x000000FFL) | (CANQueue_raw[CANQueueOUT][10] & 0x000000FFL) << 8 | (CANQueue_raw[CANQueueOUT][11] & 0x000000FFL) << 16 | (CANQueue_raw[CANQueueOUT][12] & 0x000000FFL) << 24;

				ECanaShadow.CANMC.bit.MBNR = 0;					//clear request for mailbox 4
				ECanaShadow.CANMC.bit.CDR = 0;					//clear change data field request
				ECanaRegs.CANMC.all = ECanaShadow.CANMC.all;	//set regs again

			EDIS;

			//request that the mailbox be sent
			ECanaShadow.CANTRS.all = 1 << 0x04;				//mark mailbox 4 for transmit
			ECanaRegs.CANTRS.all = ECanaShadow.CANTRS.all;	//set in real registers

			if (++CANQueueOUT == CANQUEUEDEPTH) CANQueueOUT = 0;					//increment with wrap
			CANQueueFULL = 0;														//just pulled a message, can't be full
			if (CANQueueIN == CANQueueOUT) CANQueueEMPTY = 1;						//test for empty
			CANQueueCount -=1;														//decrement counter
			ops.Counters.fields.BufAto2 -=1;
		}
	}
	else	//mailbox 4 is sending
	{
		if(isStopWatchComplete(can2_watch) == 1)
		{
			//error on CAN send timeout
			//increment timeout counter and clear mailbox 4
			ops.Flags.fields.Timeout += 1;

			ECanaShadow.CANTRR.all = ECanaRegs.CANTRR.all;
			ECanaShadow.CANTRR.bit.TRR4 = 1;
			ECanaRegs.CANTRR.all = ECanaShadow.CANTRR.all;

		    ECanaMboxes.MBOX4.MDH.all = 0;
		    ECanaMboxes.MBOX4.MDL.all = 0;
		}
	}

    if (isStopWatchComplete(canA_watch) == 1)
	{
		//indicate CAN bus A down
    	ops.Flags.fields.can_status = 0;
    	SETLED0();
	}
}

void SensorCovDeInit()
{
	//todo USER: SensorCovDeInit()
	MCP2515_reset(1);				//hold MCP2515 in reset
	StopStopWatch(mirror_Ato2_watch);
	StopStopWatch(mirror_2toA_watch);
	StopStopWatch(canA_watch);
	StopStopWatch(can2_watch);
	SETLED0();
	SETLED1();
}

void MCP2515_Total_Reset(long delay)
{

	MCP2515_spi_init();								//initialize SPI port and GPIO associated with the MCP2515
	PgmInitMCP2515(((1<<CANFREQ)-1), MaskConfig);	//initialize MCP2515

	Buffer_Clear(&Buf_Ato2);
	ops.Counters.fields.BufAto2 = 0;
	ops.Flags.fields.Overflow = 0;
	ops.Flags.fields.Timeout = 0;

	Buffer_Clear(&Buf_2toA);
	ops.Counters.fields.Buf2toA = 0;
	ops.Flags2.fields.Overflow = 0;
	ops.Flags2.fields.Timeout = 0;

	MCP2515_Send_State = 0;								//MCP2515 state to default
	MCP2515_Receive_State = 0;
	MCP2515_Error_State = 0;
}
