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

#define MSGQUEUE_OBJECTS 16 // number of Message Queue Objects
#define DelayDeComandos 3000 // number of Message Queue Objects

/*----------------------------------------------------------------------------
 *      Declare Functions
 *---------------------------------------------------------------------------*/

// Thread Functions
void ThreadMain(void *argument);
void ThreadCentral(void *argument);
void ThreadEsquerdo(void *argument);
void ThreadDireito(void *argument);

// Elevator Functions
void InitElevators();
void InitElevator(char elevator);
void ChangeDoorStatus(char elevator, char status);
void ChangeButtonStatus(char elevator, char floor, char status);
void StopElevator(char elevator);
void MovElevator(char elevator, char direction);
void MovCommand(char *command, char actualFloor, char *targetFloor);
void SetMovement(char elevator, char actualFloor, char targetFloor);

// Aux Functions
void SetupUart(void);
void UARTIntHandler(void);
char GetFloorCharFromFloorNumberString(char floorNumber, char isHigher);

/*----------------------------------------------------------------------------
 *      Global Variables
 *---------------------------------------------------------------------------*/
MsgObj uartMsg;
osThreadId_t tidMain;
osThreadId_t tidCentral;
osThreadId_t tidEsquerdo;
osThreadId_t tidDireito;
osMessageQueueId_t qidMain;
osMessageQueueId_t qidCentralCommands;
osMessageQueueId_t qidCentralResponses;
osMessageQueueId_t qidEsquerdoCommands;
osMessageQueueId_t qidEsquerdoResponses;
osMessageQueueId_t qidDireitoCommands;
osMessageQueueId_t qidDireitoResponses;

/*----------------------------------------------------------------------------
 *      Main Function
 *---------------------------------------------------------------------------*/
int main(void)
{
	osKernelInitialize(); // Initialize CMSIS-RTOS

	// Set threads, queues and mutex
	tidMain = osThreadNew(ThreadMain, NULL, NULL);
	tidCentral = osThreadNew(ThreadCentral, NULL, NULL);
	tidEsquerdo = osThreadNew(ThreadEsquerdo, NULL, NULL);
	tidDireito = osThreadNew(ThreadDireito, NULL, NULL);
	
	osThreadSetPriority(tidCentral, osPriorityAboveNormal);

	qidMain = osMessageQueueNew(MSGQUEUE_OBJECTS * 3, sizeof(MsgObj), NULL);
	qidCentralCommands = osMessageQueueNew(MSGQUEUE_OBJECTS, sizeof(MsgObj), NULL);
	qidCentralResponses = osMessageQueueNew(MSGQUEUE_OBJECTS, sizeof(MsgObj), NULL);
	
	qidEsquerdoCommands = osMessageQueueNew(MSGQUEUE_OBJECTS, sizeof(MsgObj), NULL);
	qidEsquerdoResponses = osMessageQueueNew(MSGQUEUE_OBJECTS, sizeof(MsgObj), NULL);
	
	qidDireitoCommands = osMessageQueueNew(MSGQUEUE_OBJECTS, sizeof(MsgObj), NULL);
	qidDireitoResponses = osMessageQueueNew(MSGQUEUE_OBJECTS, sizeof(MsgObj), NULL);

	if (osKernelGetState() == osKernelReady)
	{
		IntMasterEnable(); // Enable interruptions
		SetupUart();	   // Set UART configuration

		//InitElevator(CENTRAL_ELEVATOR);
		InitElevators();

		osKernelStart(); // Start thread execution
	}

	while (1)
	{
	};
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
			if (msg.Command[0] == CENTRAL_ELEVATOR)
			{
				if(msg.Command[1] == BUTTON_TRIGGER_FROM_INSIDE)
				{
						ChangeButtonStatus(CENTRAL_ELEVATOR, msg.Command[2], ON);
				}
				
				if (msg.Size > 2)
				{
					osMessageQueuePut(qidCentralCommands, &msg, 0U, osWaitForever);
				}
				else
				{
					osMessageQueuePut(qidCentralResponses, &msg, 0U, osWaitForever);
				}
			}
			else if (msg.Command[0] == LEFT_ELEVATOR)
			{
				if(msg.Command[1] == BUTTON_TRIGGER_FROM_INSIDE)
				{
						ChangeButtonStatus(LEFT_ELEVATOR, msg.Command[2], ON);
				}
				
				if (msg.Size > 2)
				{
					osMessageQueuePut(qidEsquerdoCommands, &msg, 0U, osWaitForever);
				}
				else
				{
					osMessageQueuePut(qidEsquerdoResponses, &msg, 0U, osWaitForever);
				}
			}
			else if (msg.Command[0] == RIGHT_ELEVATOR)
			{
				if(msg.Command[1] == BUTTON_TRIGGER_FROM_INSIDE)
				{
						ChangeButtonStatus(RIGHT_ELEVATOR, msg.Command[2], ON);
				}
				
				if (msg.Size > 2)
				{
					osMessageQueuePut(qidDireitoCommands, &msg, 0U, osWaitForever);
				}
				else
				{
					osMessageQueuePut(qidDireitoResponses, &msg, 0U, osWaitForever);
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
		if (elevatorStatus == READY)
		{
			statusCommand = osMessageQueueGet(qidCentralCommands, &commandMsg, NULL, osWaitForever);
			if (statusCommand == osOK)
			{
				elevatorStatus = BUSY;
				if (commandMsg.Size == 3)
				{
					targetFloor = commandMsg.Command[2];
				}
				else if (commandMsg.Size == 5)
				{
					targetFloor = GetFloorCharFromFloorNumberString(commandMsg.Command[3], commandMsg.Command[2]);
				}
			}

			//ChangeButtonStatus(commandMsg.Command[0], targetFloor, ON);
			ChangeDoorStatus(commandMsg.Command[0], CLOSED);
		}
		else if (elevatorStatus == BUSY)
		{
			statusResponse = osMessageQueueGet(qidCentralResponses, &responseMsg, NULL, osWaitForever);
			if (statusResponse == osOK)
			{
				if (responseMsg.Command[1] != SIGNAL_OPEN && responseMsg.Command[1] != SIGNAL_CLOSED)
				{
					if (responseMsg.Size == 2)
					{
						actualFloor = GetFloorCharFromFloorNumberString(responseMsg.Command[1], '0');
					}
					else if (responseMsg.Size == 3)
					{
						actualFloor = GetFloorCharFromFloorNumberString(responseMsg.Command[2], responseMsg.Command[1]);
					}
				}
				else
				{
					if (responseMsg.Command[1] == SIGNAL_CLOSED)
					{
						SetMovement(responseMsg.Command[0], actualFloor, targetFloor);
					}
				}
			}
		}

		if (actualFloor == targetFloor)
		{
			elevatorStatus = READY;
			StopElevator(CENTRAL_ELEVATOR);
			ChangeButtonStatus(CENTRAL_ELEVATOR, actualFloor, OFF);
			ChangeDoorStatus(CENTRAL_ELEVATOR, OPEN);
			osDelay(DelayDeComandos);
		}
		//osThreadYield();                                            // suspend thread
	}
}

void ThreadEsquerdo(void *argument)
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
		if (elevatorStatus == READY)
		{
			statusCommand = osMessageQueueGet(qidEsquerdoCommands, &commandMsg, NULL, osWaitForever);
			if (statusCommand == osOK)
			{
				elevatorStatus = BUSY;
				if (commandMsg.Size == 3)
				{
					targetFloor = commandMsg.Command[2];
				}
				else if (commandMsg.Size == 5)
				{
					targetFloor = GetFloorCharFromFloorNumberString(commandMsg.Command[3], commandMsg.Command[2]);
				}
			}

			//ChangeButtonStatus(commandMsg.Command[0], targetFloor, ON);
			ChangeDoorStatus(commandMsg.Command[0], CLOSED);
		}
		else if (elevatorStatus == BUSY)
		{
			statusResponse = osMessageQueueGet(qidEsquerdoResponses, &responseMsg, NULL, osWaitForever);
			if (statusResponse == osOK)
			{
				if (responseMsg.Command[1] != SIGNAL_OPEN && responseMsg.Command[1] != SIGNAL_CLOSED)
				{
					if (responseMsg.Size == 2)
					{
						actualFloor = GetFloorCharFromFloorNumberString(responseMsg.Command[1], '0');
					}
					else if (responseMsg.Size == 3)
					{
						actualFloor = GetFloorCharFromFloorNumberString(responseMsg.Command[2], responseMsg.Command[1]);
					}
				}
				else
				{
					if (responseMsg.Command[1] == SIGNAL_CLOSED)
					{
						SetMovement(responseMsg.Command[0], actualFloor, targetFloor);
					}
				}
			}
		}

		if (actualFloor == targetFloor)
		{
			elevatorStatus = READY;
			StopElevator(LEFT_ELEVATOR);
			ChangeButtonStatus(LEFT_ELEVATOR, actualFloor, OFF);
			ChangeDoorStatus(LEFT_ELEVATOR, OPEN);
			osDelay(DelayDeComandos);
		}
		//osThreadYield();                                            // suspend thread
	}
}


void ThreadDireito(void *argument)
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
		if (elevatorStatus == READY)
		{
			statusCommand = osMessageQueueGet(qidDireitoCommands, &commandMsg, NULL, osWaitForever);
			if (statusCommand == osOK)
			{
				elevatorStatus = BUSY;
				if (commandMsg.Size == 3)
				{
					targetFloor = commandMsg.Command[2];
				}
				else if (commandMsg.Size == 5)
				{
					targetFloor = GetFloorCharFromFloorNumberString(commandMsg.Command[3], commandMsg.Command[2]);
				}
			}

			//ChangeButtonStatus(commandMsg.Command[0], targetFloor, ON);
			ChangeDoorStatus(commandMsg.Command[0], CLOSED);
		}
		else if (elevatorStatus == BUSY)
		{
			statusResponse = osMessageQueueGet(qidDireitoResponses, &responseMsg, NULL, osWaitForever);
			if (statusResponse == osOK)
			{
				if (responseMsg.Command[1] != SIGNAL_OPEN && responseMsg.Command[1] != SIGNAL_CLOSED)
				{
					if (responseMsg.Size == 2)
					{
						actualFloor = GetFloorCharFromFloorNumberString(responseMsg.Command[1], '0');
					}
					else if (responseMsg.Size == 3)
					{
						actualFloor = GetFloorCharFromFloorNumberString(responseMsg.Command[2], responseMsg.Command[1]);
					}
				}
				else
				{
					if (responseMsg.Command[1] == SIGNAL_CLOSED)
					{
						SetMovement(responseMsg.Command[0], actualFloor, targetFloor);
					}
				}
			}
		}

		if (actualFloor == targetFloor)
		{
			elevatorStatus = READY;
			StopElevator(RIGHT_ELEVATOR);
			ChangeButtonStatus(RIGHT_ELEVATOR, actualFloor, OFF);
			ChangeDoorStatus(RIGHT_ELEVATOR, OPEN);
			osDelay(DelayDeComandos);
		}
		//osThreadYield();                                            // suspend thread
	}
}

void InitElevators()
{
	UART_OutChar(LEFT_ELEVATOR);
	UART_OutChar(INIT_ELEVATOR);
	UART_OutChar(END_COMMAND);
	
	UART_OutChar(CENTRAL_ELEVATOR);
	UART_OutChar(INIT_ELEVATOR);
	UART_OutChar(END_COMMAND);
	
	UART_OutChar(RIGHT_ELEVATOR);
	UART_OutChar(INIT_ELEVATOR);
	UART_OutChar(END_COMMAND);
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

void SetMovement(char elevator, char actualFloor, char targetFloor)
{
	if ((int)targetFloor > (int)actualFloor)
	{
		MovElevator(elevator, UP);
	}
	else if ((int)targetFloor < (int)actualFloor)
	{
		MovElevator(elevator, DOWN);
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

char GetFloorCharFromFloorNumberString(char floorNumber, char isHigher)
{
	if (isHigher == '0')
	{
		if (floorNumber == '0')
			return FLOOR_0;
		else if (floorNumber == '1')
			return FLOOR_1;
		else if (floorNumber == '2')
			return FLOOR_2;
		else if (floorNumber == '3')
			return FLOOR_3;
		else if (floorNumber == '4')
			return FLOOR_4;
		else if (floorNumber == '5')
			return FLOOR_5;
		else if (floorNumber == '6')
			return FLOOR_6;
		else if (floorNumber == '7')
			return FLOOR_7;
		else if (floorNumber == '8')
			return FLOOR_8;
		else if (floorNumber == '9')
			return FLOOR_9;
	}
	else
	{
		if (floorNumber == '0')
			return FLOOR_10;
		else if (floorNumber == '1')
			return FLOOR_11;
		else if (floorNumber == '2')
			return FLOOR_12;
		else if (floorNumber == '3')
			return FLOOR_13;
		else if (floorNumber == '4')
			return FLOOR_14;
		else if (floorNumber == '5')
			return FLOOR_15;
	}
}