#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"

#include "driverlib/sysctl.h"
#include "driverlib/uart.h"
#include "driverlib/interrupt.h"

#include "cmsis_os2.h" // CMSIS-RTOS

#include "UART.h"
#include "misc.h"
#include "driverleds.h" // device drivers

#define MSGQUEUE_OBJECTS 16 // number of Message Queue Objects

/*----------------------------------------------------------------------------
 *      Declare Functions
 *---------------------------------------------------------------------------*/

// Thread Functions
void ThreadMain(void *argument);
void ThreadCentral(void *argument);

// Elevator Functions
void InitElevator(char elevator);
void ChangeDoorStatus(char elevator, char status);
void ChangeButtonStatus(char elevator, char floor, char status);
void StopElevator(char elevator);
void MovElevator(char elevator, char direction);
void MovCommand(char* command, char actualFloor, char* targetFloor);
void ProcessCommand(MsgObj msg, char actualFloor, char* targetFloor);
void ProcessResponse(MsgObj msg, char* actualFloor);

// Aux Functions
void SetupUart(void);
void UARTIntHandler(void);
char GetFloorCharFromFloorNumberString(char* floorNumber);

/*----------------------------------------------------------------------------
 *      Global Variables
 *---------------------------------------------------------------------------*/
MsgObj uartMsg;
osThreadId_t tidMain;
osThreadId_t tidCentral;
osMessageQueueId_t qidMain;
osMessageQueueId_t qidCentralCommands;
osMessageQueueId_t qidCentralResponses;

/*----------------------------------------------------------------------------
 *      Main Function
 *---------------------------------------------------------------------------*/
int main(void)
{
  LEDInit(LED4 | LED3 | LED2 | LED1);

  osKernelInitialize(); // Initialize CMSIS-RTOS

  // Set threads, queues and mutex
  tidMain = osThreadNew(ThreadMain, NULL, NULL);
  tidCentral = osThreadNew(ThreadCentral, NULL, NULL);

  qidMain = osMessageQueueNew(MSGQUEUE_OBJECTS, sizeof(MsgObj), NULL);
  qidCentralCommands = osMessageQueueNew(MSGQUEUE_OBJECTS, sizeof(MsgObj), NULL);
	qidCentralResponses = osMessageQueueNew(MSGQUEUE_OBJECTS, sizeof(MsgObj), NULL);
	
  if (osKernelGetState() == osKernelReady)
  {
    IntMasterEnable(); // Enable interruptions
    SetupUart();       // Set UART configuration
		
		InitElevator(CENTRAL_ELEVATOR);

    osKernelStart(); // Start thread execution
  }

  while (1){};
}

/*----------------------------------------------------------------------------
 *      Threads Functions
 *---------------------------------------------------------------------------*/

void ThreadMain(void *argument)
{
  osStatus_t status;
  MsgObj msg;

  while (1)
  {
    status = osMessageQueueGet(qidMain, &msg, NULL, osWaitForever);
    if (status == osOK)
    {
      if(msg.Command[0] == 'c')
			{
				if(msg.Size > 2)
				{
					osMessageQueuePut(qidCentralCommands, &msg, 0U, osWaitForever);
				}
				else
				{
					osMessageQueuePut(qidCentralResponses, &msg, 0U, osWaitForever);
				}
			}
    }
  }
}

/*----------------------------------------------------------------------------
 *      Elevator Functions
 *---------------------------------------------------------------------------*/
void ThreadCentral(void *argument)
{
  osStatus_t statusCommand;
	osStatus_t statusResponse;
  MsgObj commandMsg;
	MsgObj responseMsg;
	char elevatorStatus = READY;
	char actualFloor = FLOOR_0;
  char targetFloor = FLOOR_0;
	

  while (1)
  {
		if(elevatorStatus == READY)
		{
			statusCommand = osMessageQueueGet(qidCentralCommands, &commandMsg, NULL, osWaitForever);
			if(statusCommand == osOK)
			{
				elevatorStatus = BUSY;
				ProcessCommand(commandMsg, actualFloor, &targetFloor);
			}
		}
		else if(elevatorStatus == BUSY)
		{
			statusResponse = osMessageQueueGet(qidCentralResponses, &responseMsg, NULL, osWaitForever);
			if(statusResponse == osOK)
			{
				ProcessResponse(commandMsg, &actualFloor);
			}
		}
		
		if(actualFloor == targetFloor)
		{
			elevatorStatus = READY;
			ChangeButtonStatus(CENTRAL_ELEVATOR, actualFloor, OFF);
			ChangeDoorStatus(CENTRAL_ELEVATOR, OPEN);
		}
  }
}

void InitElevator(char elevator)
{
  UART_OutChar(elevator);
  UART_OutChar(INIT_ELEVATOR);
  UART_OutChar(END_COMMAND);
}

void ChangeDoorStatus(char elevator, char status)
{
  UART_OutChar(elevator);
  UART_OutChar(status);
  UART_OutChar(END_COMMAND);
}

void ChangeButtonStatus(char elevator, char floor, char status)
{
  UART_OutChar(elevator);
  UART_OutChar(status);
  UART_OutChar(floor);
  UART_OutChar(END_COMMAND);
}

void StopElevator(char elevator)
{
  UART_OutChar(elevator);
  UART_OutChar(STOP);
  UART_OutChar(END_COMMAND);
}

void MovElevator(char elevator, char direction)
{   
  UART_OutChar(elevator);
  UART_OutChar(direction);
  UART_OutChar(END_COMMAND);
}

void ProcessCommand(MsgObj msg, char actualFloor, char *targetFloor)
{
  if (msg.Size == 3)
  {
    *targetFloor = msg.Command[2];
  }
  else if (msg.Size == 5)
  {
    *targetFloor = GetFloorCharFromFloorNumberString(strcat(&msg.Command[2],&msg.Command[3]));
  }
	
	ChangeButtonStatus(msg.Command[0], *targetFloor, ON);
  ChangeDoorStatus(msg.Command[0], CLOSED);
	
	if ((int)*targetFloor > (int)actualFloor)
  {
    MovElevator(msg.Command[0], UP);
  }
  else if ((int)*targetFloor < (int)actualFloor)
  {
    MovElevator(msg.Command[0], DOWN);
  }
}

void ProcessResponse(MsgObj msg, char *actualFloor)
{
	if(msg.Command[1] != 'A' && msg.Command[1] != 'F')
	{
		if(msg.Size == 2)
		{
			*actualFloor = GetFloorCharFromFloorNumberString(&msg.Command[1]);
		}
		else if(msg.Size == 3)
		{
			*actualFloor = GetFloorCharFromFloorNumberString(strcat(&msg.Command[1],&msg.Command[2]));
		}
	}
}

/*----------------------------------------------------------------------------
 *      Aux Functions
 *---------------------------------------------------------------------------*/
void SetupUart()
{
  // Enable the UART0 connection
  UART_Init();

  // Register the handler for the UART0 interruption
  IntRegister(INT_UART0, UARTIntHandler);

  // Enable UART0 interruptions for the pin INT_UART0
  IntEnable(INT_UART0);

  // Enable interruptions in the RX and TX for the port UART0_BASE
  UARTIntEnable(UART0_BASE, UART_INT_RX | UART_INT_RT);

  // Clean the Uart Msg
  uartMsg.Size = 0;
}

void UARTIntHandler()
{
  // Clear the interruption flag for the pin INT_UART0 in the port UART0_BASE
  UARTIntClear(UART0_BASE, INT_UART0);

  // Get the character received in the UART buffer
  char received = UART_InChar();

  if (received != '\n')
  {
    if (received != '\r')
    {
      uartMsg.Command[uartMsg.Size] = received;
      uartMsg.Size++;
    }
    else
    {
      osMessageQueuePut(qidMain, &uartMsg, 0U, 0U);
      uartMsg.Size = 0;
    }
  }
}

char GetFloorCharFromFloorNumberString(char* floorNumber)
{
  switch (atoi(floorNumber))
  {
    case 0:
      return FLOOR_0;
      break;
      
    case 1:
      return FLOOR_1;
      break;
      
    case 2:
      return FLOOR_2;
      break;
      
    case 3:
      return FLOOR_3;
      break;
      
    case 4:
      return FLOOR_4;
      break;
      
    case 5:
      return FLOOR_5;
      break;
      
    case 6:
      return FLOOR_6;
      break;
      
    case 7:
      return FLOOR_7;
      break;
      
    case 8:
      return FLOOR_8;
      break;
      
    case 9:
      return FLOOR_9;
      break;
      
    case 10:
      return FLOOR_10;
      break;
      
    case 11:
      return FLOOR_11;
      break;
      
    case 12:
      return FLOOR_12;
      break;
      
    case 13:
      return FLOOR_13;
      break;
      
    case 14:
      return FLOOR_14;
      break;
      
    case 15:
      return FLOOR_15;
      break;
      
    default:
      return FLOOR_0;
      break;
  }
}