// Air reservation
// Clerk program
#include <inc/lib.h>
#include <user/air.h>

extern volatile bool printStats;
void
_main(void)
{
	//disable the print of prog stats after finishing
	printStats = 0;

	int parentenvID = sys_getparentenvid();

	// Get the shared variables from the main program ***********************************

	char _isOpened[] = "isOpened";
	char _customers[] = "customers";
	char _custCounter[] = "custCounter";
	char _flight1Counter[] = "flight1Counter";
	char _flight2Counter[] = "flight2Counter";
	char _flightBooked1Counter[] = "flightBooked1Counter";
	char _flightBooked2Counter[] = "flightBooked2Counter";
	char _flightBooked1Arr[] = "flightBooked1Arr";
	char _flightBooked2Arr[] = "flightBooked2Arr";
	char _cust_ready_queue[] = "cust_ready_queue";
	char _queue_in[] = "queue_in";
	char _queue_out[] = "queue_out";

	char _cust_ready[] = "cust_ready";
	char _custQueueCS[] = "custQueueCS";
	char _flight1CS[] = "flight1CS";
	char _flight2CS[] = "flight2CS";

	char _clerk[] = "clerk";
	char _custCounterCS[] = "custCounterCS";
	char _custTerminated[] = "custTerminated";
	char _clerkTerminated[] = "clerkTerminated";

	char _taircl[] = "taircl";
	char _taircu[] = "taircu";

	struct Customer * customers = sget(parentenvID, _customers);

	int* isOpened = sget(parentenvID, _isOpened);

	int* flight1Counter = sget(parentenvID, _flight1Counter);
	int* flight2Counter = sget(parentenvID, _flight2Counter);

	int* flight1BookedCounter = sget(parentenvID, _flightBooked1Counter);
	int* flight2BookedCounter = sget(parentenvID, _flightBooked2Counter);

	int* flight1BookedArr = sget(parentenvID, _flightBooked1Arr);
	int* flight2BookedArr = sget(parentenvID, _flightBooked2Arr);

	int* cust_ready_queue = sget(parentenvID, _cust_ready_queue);

	int* queue_out = sget(parentenvID, _queue_out);
	//cprintf("address of queue_out = %d\n", queue_out);
	// *********************************************************************************

	struct semaphore cust_ready = get_semaphore(parentenvID, _cust_ready);
	struct semaphore custQueueCS = get_semaphore(parentenvID, _custQueueCS);
	struct semaphore flight1CS = get_semaphore(parentenvID, _flight1CS);
	struct semaphore flight2CS = get_semaphore(parentenvID, _flight2CS);
	struct semaphore clerk = get_semaphore(parentenvID, _clerk);
	struct semaphore clerkTerminated = get_semaphore(parentenvID, _clerkTerminated);

	while(*isOpened)
	{
		int custId;
		//wait for a customer
		wait_semaphore(cust_ready);

		//dequeue the customer info
		wait_semaphore(custQueueCS);
		{
			//cprintf("*queue_out = %d\n", *queue_out);
			custId = cust_ready_queue[*queue_out];
			//there's no more customers for now...
			if (custId == -1)
			{
				signal_semaphore(custQueueCS);
				continue;
			}
			*queue_out = *queue_out +1;
		}
		signal_semaphore(custQueueCS);

		//try reserving on the required flight
		int custFlightType = customers[custId].flightType;
		//cprintf("custId dequeued = %d, ft = %d\n", custId, customers[custId].flightType);

		switch (custFlightType)
		{
		case 1:
		{
			//Check and update Flight1
			wait_semaphore(flight1CS);
			{
				if(*flight1Counter > 0)
				{
					*flight1Counter = *flight1Counter - 1;
					customers[custId].booked = 1;
					flight1BookedArr[*flight1BookedCounter] = custId;
					*flight1BookedCounter =*flight1BookedCounter+1;
				}
				else
				{
					cprintf("%~\nFlight#1 is FULL! Reservation request of customer#%d is rejected\n", custId);
				}
			}
			signal_semaphore(flight1CS);
		}

		break;
		case 2:
		{
			//Check and update Flight2
			wait_semaphore(flight2CS);
			{
				if(*flight2Counter > 0)
				{
					*flight2Counter = *flight2Counter - 1;
					customers[custId].booked = 1;
					flight2BookedArr[*flight2BookedCounter] = custId;
					*flight2BookedCounter =*flight2BookedCounter+1;
				}
				else
				{
					cprintf("%~\nFlight#2 is FULL! Reservation request of customer#%d is rejected\n", custId);
				}
			}
			signal_semaphore(flight2CS);
		}
		break;
		case 3:
		{
			//Check and update Both Flights
			wait_semaphore(flight1CS); wait_semaphore(flight2CS);
			{
				if(*flight1Counter > 0 && *flight2Counter >0 )
				{
					*flight1Counter = *flight1Counter - 1;
					customers[custId].booked = 1;
					flight1BookedArr[*flight1BookedCounter] = custId;
					*flight1BookedCounter =*flight1BookedCounter+1;

					*flight2Counter = *flight2Counter - 1;
					customers[custId].booked = 1;
					flight2BookedArr[*flight2BookedCounter] = custId;
					*flight2BookedCounter =*flight2BookedCounter+1;

				}
				else
				{
					cprintf("%~\nFlight#1 and/or Flight#2 is FULL! Reservation request of customer#%d is rejected\n", custId);
				}
			}
			signal_semaphore(flight1CS); signal_semaphore(flight2CS);
		}
		break;
		default:
			panic("customer must have flight type\n");
		}

		//signal finished
		char prefix[30]="cust_finished";
		char id[5]; char sname[50];
		ltostr(custId, id);
		strcconcat(prefix, id, sname);
		//sys_signalSemaphore(parentenvID, sname);
		struct semaphore cust_finished = get_semaphore(parentenvID, sname);
		signal_semaphore(cust_finished);

		//signal the clerk
		signal_semaphore(clerk);
	}

	cprintf("\nclerk is finished...........\n");
	signal_semaphore(clerkTerminated);
}
