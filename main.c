#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"

#include "driverlib/sysctl.h"
#include "driverlib/debug.h"
#include "driverlib/uart.h"
#include "driverlib/interrupt.h"

#include "cmsis_os2.h" // CMSIS-RTOS

#include "UART.h"
#include "misc.h"

#define MSGQUEUE_OBJECTS 32	 // number of Message Queue Objects
#define DelayDeComandos 3000 // number of Message Queue Objects

/*----------------------------------------------------------------------------
 *      Declare Functions
 *---------------------------------------------------------------------------*/

// Thread Functions
void ThreadController(void *argument);
void ThreadCentral(void *argument);
void ThreadLeft(void *argument);
void ThreadRight(void *argument);

// Elevator Functions
void InitElevators();
void InitElevator(char elevator);
void ChangeDoorStatus(char elevator, char status);
void ChangeButtonStatus(char elevator, char floor, char status);
void StopElevator(char elevator);
void MovElevator(char elevator, char direction);
void MovCommand(char *command, char actualFloor, char *targetFloor);
void SetMovement(char elevator, char actualFloor, char targetFloor);
char SetDirection(char actualFloor, char targetFloor);
void ManagerElevator(char elevator);

// Aux Functions
void SetupUart(void);
void UARTIntHandler(void);
char GetFloorCharFromFloorNumberString(char floorNumber, char isHigher);

/*----------------------------------------------------------------------------
 *      Global Variables
 *---------------------------------------------------------------------------*/
MsgObj uartMsg;
osThreadId_t tidController, tidCentral, tidLeft, tidRight;
osMessageQueueId_t qidController;
osMessageQueueId_t qidCentralCommands, qidLeftCommands, qidRightCommands;
osMessageQueueId_t qidCentralResponses, qidLeftResponses, qidRightResponses;
/*----------------------------------------------------------------------------
 *      Main Function
 *---------------------------------------------------------------------------*/
int main(void)
{
	osKernelInitialize(); // Initialize CMSIS-RTOS

	// Set threads, queues and mutex
	tidCentral = osThreadNew(ThreadCentral, NULL, NULL);
	tidLeft = osThreadNew(ThreadLeft, NULL, NULL);
	tidRight = osThreadNew(ThreadRight, NULL, NULL);

	tidController = osThreadNew(ThreadController, NULL, NULL);

	osThreadSetPriority(tidCentral, osPriorityAboveNormal);

	qidController = osMessageQueueNew(MSGQUEUE_OBJECTS, sizeof(MsgObj), NULL);
	if (qidController == NULL)
		return (-1);

	qidCentralCommands = osMessageQueueNew(MSGQUEUE_OBJECTS, sizeof(MsgObj), NULL);
	if (qidCentralCommands == NULL)
		return (-1);

	qidCentralResponses = osMessageQueueNew(MSGQUEUE_OBJECTS, sizeof(MsgObj), NULL);
	if (qidCentralResponses == NULL)
		return (-1);

	qidLeftCommands = osMessageQueueNew(MSGQUEUE_OBJECTS, sizeof(MsgObj), NULL);
	if (qidLeftCommands == NULL)
		return (-1);
	qidLeftResponses = osMessageQueueNew(MSGQUEUE_OBJECTS, sizeof(MsgObj), NULL);
	if (qidLeftResponses == NULL)
		return (-1);

	qidRightCommands = osMessageQueueNew(MSGQUEUE_OBJECTS, sizeof(MsgObj), NULL);
	if (qidRightCommands == NULL)
		return (-1);
	qidRightResponses = osMessageQueueNew(MSGQUEUE_OBJECTS, sizeof(MsgObj), NULL);
	if (qidRightResponses == NULL)
		return (-1);

	if (osKernelGetState() == osKernelReady)
	{
		IntMasterEnable(); // Enable interruptions
		SetupUart();	   // Set UART configuration

		InitElevators();

		osKernelStart(); // Start thread execution
	}

	while (1)
		;
}

/*----------------------------------------------------------------------------
 *      Threads Functions
 *---------------------------------------------------------------------------*/

void ThreadController(void *argument)
{
	osStatus_t status;
	MsgObj msg;

	while (1)
	{
		status = osMessageQueueGet(qidController, &msg, 0U, osWaitForever);
		if (status == osOK)
		{
			if (msg.Command[1] == BUTTON_TRIGGER_FROM_INSIDE)
				ChangeButtonStatus(msg.Command[0], msg.Command[2], ON);

			if (msg.Command[0] == CENTRAL_ELEVATOR)
			{
				if ((msg.Size == 2) || ((msg.Size == 3) && (msg.Command[1] == '1')))
					osMessageQueuePut(qidCentralResponses, &msg, osPriorityHigh, 0U);
				else
					osMessageQueuePut(qidCentralCommands, &msg, 0U, 0U);
			}
			else if (msg.Command[0] == LEFT_ELEVATOR)
			{
				if ((msg.Size == 2) || ((msg.Size == 3) && (msg.Command[1] == '1')))
					osMessageQueuePut(qidLeftResponses, &msg, osPriorityHigh, 0U);
				else
					osMessageQueuePut(qidLeftCommands, &msg, 0U, osWaitForever);
			}
			else if (msg.Command[0] == RIGHT_ELEVATOR)
			{
				if ((msg.Size == 2) || ((msg.Size == 3) && (msg.Command[1] == '1')))
					osMessageQueuePut(qidRightResponses, &msg, osPriorityHigh, 0U);
				else
					osMessageQueuePut(qidRightCommands, &msg, 0U, osWaitForever);
			}
		}
		osThreadYield(); // suspend thread
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
	char elevator = CENTRAL_ELEVATOR;
	uint32_t status;

	while (1)
	{
		if (elevatorStatus == BUSY)
		{
			if (actualFloor == targetFloor)
			{
				StopElevator(elevator);
				ChangeButtonStatus(elevator, actualFloor, OFF);
				ChangeDoorStatus(elevator, OPEN);
				elevatorStatus = READY;
				osDelay(DelayDeComandos);
			}
			else
			{
				statusResponse = osMessageQueueGet(qidCentralResponses, &responseMsg, 0U, osWaitForever);
				if (statusResponse == osOK)
				{
					if (responseMsg.Command[1] != SIGNAL_OPEN && responseMsg.Command[1] != SIGNAL_CLOSED)
					{
						if (responseMsg.Size == 2)
							actualFloor = GetFloorCharFromFloorNumberString(responseMsg.Command[1], '0');
						else if (responseMsg.Size == 3)
							actualFloor = GetFloorCharFromFloorNumberString(responseMsg.Command[2], responseMsg.Command[1]);

						if (actualFloor == targetFloor)
							StopElevator(elevator);
					}
					else
					{
						if (responseMsg.Command[1] == SIGNAL_CLOSED)
							SetMovement(elevator, actualFloor, targetFloor);
					}
				}
			}
		}
		else
		{
			statusCommand = osMessageQueueGet(qidCentralCommands, &commandMsg, 0U, osWaitForever);
			if (statusCommand == osOK)
			{
				elevatorStatus = BUSY;

				if (commandMsg.Size == 3)
					targetFloor = commandMsg.Command[2];

				else if (commandMsg.Size == 5)
					targetFloor = GetFloorCharFromFloorNumberString(commandMsg.Command[3], commandMsg.Command[2]);

				ChangeDoorStatus(commandMsg.Command[0], CLOSED);
			}
		}
	}
}

void ThreadLeft(void *argument)
{
	osStatus_t statusCommand;
	osStatus_t statusResponse;
	MsgObj commandMsg;
	MsgObj responseMsg;
	char elevatorStatus = READY;
	char actualFloor = FLOOR_0;
	char targetFloor = FLOOR_0;
	char elevator = LEFT_ELEVATOR;

	while (1)
	{
		if (elevatorStatus == BUSY)
		{
			if (actualFloor == targetFloor)
			{
				StopElevator(elevator);
				ChangeButtonStatus(elevator, actualFloor, OFF);
				ChangeDoorStatus(elevator, OPEN);
				elevatorStatus = READY;
				osDelay(DelayDeComandos);
			}
			else
			{
				statusResponse = osMessageQueueGet(qidLeftResponses, &responseMsg, 0U, osWaitForever);
				if (statusResponse == osOK)
				{
					if (responseMsg.Command[1] != SIGNAL_OPEN && responseMsg.Command[1] != SIGNAL_CLOSED)
					{
						if (responseMsg.Size == 2)
							actualFloor = GetFloorCharFromFloorNumberString(responseMsg.Command[1], '0');
						else if (responseMsg.Size == 3)
							actualFloor = GetFloorCharFromFloorNumberString(responseMsg.Command[2], responseMsg.Command[1]);

						if (actualFloor == targetFloor)
							StopElevator(elevator);
					}
					else
					{
						if (responseMsg.Command[1] == SIGNAL_CLOSED)
							SetMovement(elevator, actualFloor, targetFloor);
					}
				}
			}
		}
		else
		{
			statusCommand = osMessageQueueGet(qidLeftCommands, &commandMsg, 0U, osWaitForever);
			if (statusCommand == osOK)
			{
				elevatorStatus = BUSY;

				if (commandMsg.Size == 3)
					targetFloor = commandMsg.Command[2];

				else if (commandMsg.Size == 5)
					targetFloor = GetFloorCharFromFloorNumberString(commandMsg.Command[3], commandMsg.Command[2]);

				ChangeDoorStatus(commandMsg.Command[0], CLOSED);
			}
		}
	}
}

void ThreadRight(void *argument)
{
	osStatus_t statusCommand;
	osStatus_t statusResponse;
	MsgObj commandMsg;
	MsgObj responseMsg;
	char elevatorStatus = READY;
	char actualFloor = FLOOR_0;
	char targetFloor = FLOOR_0;
	char elevator = RIGHT_ELEVATOR;

		while (1)
	{
		if (elevatorStatus == BUSY)
		{
			if (actualFloor == targetFloor)
			{
				StopElevator(elevator);
				ChangeButtonStatus(elevator, actualFloor, OFF);
				ChangeDoorStatus(elevator, OPEN);
				elevatorStatus = READY;
				osDelay(DelayDeComandos);
			}
			else
			{
				statusResponse = osMessageQueueGet(qidRightResponses, &responseMsg, 0U, osWaitForever);
				if (statusResponse == osOK)
				{
					if (responseMsg.Command[1] != SIGNAL_OPEN && responseMsg.Command[1] != SIGNAL_CLOSED)
					{
						if (responseMsg.Size == 2)
							actualFloor = GetFloorCharFromFloorNumberString(responseMsg.Command[1], '0');
						else if (responseMsg.Size == 3)
							actualFloor = GetFloorCharFromFloorNumberString(responseMsg.Command[2], responseMsg.Command[1]);

						if (actualFloor == targetFloor)
							StopElevator(elevator);
					}
					else
					{
						if (responseMsg.Command[1] == SIGNAL_CLOSED)
							SetMovement(elevator, actualFloor, targetFloor);
					}
				}
			}
		}
		else
		{
			statusCommand = osMessageQueueGet(qidRightCommands, &commandMsg, 0U, osWaitForever);
			if (statusCommand == osOK)
			{
				elevatorStatus = BUSY;

				if (commandMsg.Size == 3)
					targetFloor = commandMsg.Command[2];

				else if (commandMsg.Size == 5)
					targetFloor = GetFloorCharFromFloorNumberString(commandMsg.Command[3], commandMsg.Command[2]);

				ChangeDoorStatus(commandMsg.Command[0], CLOSED);
			}
		}
	}
}

void InitElevators()
{
	InitElevator(LEFT_ELEVATOR);
	InitElevator(CENTRAL_ELEVATOR);
	InitElevator(RIGHT_ELEVATOR);
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
	MovElevator(elevator, SetDirection(actualFloor, targetFloor));
}
char SetDirection(char actualFloor, char targetFloor)
{
	if ((int)targetFloor > (int)actualFloor)
		return UP;
	else if ((int)targetFloor < (int)actualFloor)
		return DOWN;

	return STOP;
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
			osMessageQueuePut(qidController, &uartMsg, 0U, 0U);
			uartMsg.Size = 0;
			strcpy(uartMsg.Command, "         ");
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