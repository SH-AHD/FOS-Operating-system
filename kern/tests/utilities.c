/* See COPYRIGHT for copyright information. */
#include "utilities.h"

#include <inc/mmu.h>
#include <inc/x86.h>
#include <inc/assert.h>
#include <inc/queue.h>
#include <inc/dynamic_allocator.h>

#include <kern/proc/user_environment.h>
#include "../trap/syscall.h"
#include <kern/cmd/command_prompt.h>
#include <kern/cpu/kclock.h>
#include <kern/cpu/sched.h>
#include <kern/cpu/cpu.h>
#include <kern/disk/pagefile_manager.h>
#include <kern/mem/memory_manager.h>
#include "../cons/console.h"

#include <kern/trap/fault_handler.h>

void rsttst()
{
	init_spinlock(&tstcntlock, "tstcnt lock");
	acquire_spinlock(&tstcntlock);
	{
		tstcnt = 0;
	}
	release_spinlock(&tstcntlock);
}
void inctst()
{
	acquire_spinlock(&tstcntlock);
	{
		tstcnt++;
	}
	release_spinlock(&tstcntlock);
}
uint32 gettst()
{
	return tstcnt;
}

void tst(uint32 n, uint32 v1, uint32 v2, char c, int inv)
{
	int chk = 0;
	switch (c)
	{
	case 'l':
		if (n < v1)
			chk = 1;
		else if (inv)
			chk = 1;
		break;
	case 'g':
		if (n > v1)
			chk = 1;
		else if (inv)
			chk = 1;
		break;
	case 'e':
		if (n == v1)
			chk = 1;
		else if (inv)
			chk = 1;
		break;
	case 'b':
		if (n >= v1 && n <= v2)
			chk = 1;
		break;
	}

	if (chk == 0) panic("Error!! test fails");

	acquire_spinlock(&tstcntlock);
	{
		tstcnt++ ;
	}
	release_spinlock(&tstcntlock);

	return;
}

void chktst(uint32 n)
{
	int __tstcnt;
	acquire_spinlock(&tstcntlock);
	{
		__tstcnt = tstcnt;
	}
	release_spinlock(&tstcntlock);
	if (__tstcnt == n)
		cprintf("\nCongratulations... test runs successfully\n");
	else
		panic("Error!! test fails at final");
}

inline unsigned int nearest_pow2_ceil(unsigned int x) {
	if (x <= 1) return 1;
	int power = 2;
	x--;
	while (x >>= 1) {
		power <<= 1;
	}
	return power;
}
inline unsigned int log2_ceil(unsigned int x) {
	if (x <= 1) return 1;
	//int power = 2;
	int bits_cnt = 2 ;
	x--;
	while (x >>= 1) {
		//power <<= 1;
		bits_cnt++ ;
	}
	return bits_cnt;
}

/*2023*/
void fixedPt2Str(fixed_point_t f, int num_dec_digits, char* output)
{
	int mulFactor = 1;
	for (int i = 0; i < num_dec_digits; ++i) {
		mulFactor *= 10;
	}
	int scaledVal = fix_round(fix_scale(f, mulFactor)) ;
	int integer = scaledVal/mulFactor;
	int fraction = scaledVal%mulFactor;
	char intPart[20] ; ltostr(integer, intPart);
	char fractPart[20] ; ltostr(fraction, fractPart);
	int tmp = mulFactor / 10;

	char zeros[10] = "";
	while (fraction < tmp)
	{
		strcconcat("0", zeros, zeros);
		tmp /= 10;
	}
	char fractPart2[20];
	strcconcat(zeros, fractPart, fractPart2);

	//cprintf("integer = %d, intPart = %s - fraction = %d, fractPart = %s\n", integer, intPart, fraction , fractPart2);
	strcconcat(intPart, ".", intPart);
	strcconcat(intPart, fractPart2, output);

}

int __firstTimeSleep = 1;
struct Channel __tstchan__ ;
struct spinlock __tstchan_lk__;
int __firstTimeSleepLock = 1;
struct sleeplock __tstslplk__;

void sys_utilities(char* utilityName, int value)
{
	if (strncmp(utilityName, "__BSDSetNice@", strlen("__BSDSetNice@")) == 0)
	{
		int number_of_tokens;
		//allocate array of char * of size MAX_ARGUMENTS = 16 found in string.h
		char *tokens[MAX_ARGUMENTS];
		strsplit(utilityName, "@", tokens, &number_of_tokens) ;
		int envID = strtol(tokens[1], NULL, 10);
		struct Env* env = NULL ;
		envid2env(envID, &env, 0);
		assert(env->env_id == envID) ;
		env_set_nice(env, value);
	}
	else if (strncmp(utilityName, "__PRIRRSetPriority@", strlen("__PRIRRSetPriority@")) == 0)
	{
		int number_of_tokens;
		//allocate array of char * of size MAX_ARGUMENTS = 16 found in string.h
		char *tokens[MAX_ARGUMENTS];
		strsplit(utilityName, "@", tokens, &number_of_tokens) ;
		int envID = strtol(tokens[1], NULL, 10);
		struct Env* env = NULL ;
		envid2env(envID, &env, 0);
		assert(env->env_id == envID) ;
		env_set_priority(envID, value);
	}
	else if (strncmp(utilityName, "__CheckExitOrder@", strlen("__CheckExitOrder@")) == 0)
	{
		int* numOfInstances = (int*) value ;
		int number_of_tokens;
		//allocate array of char * of size MAX_ARGUMENTS = 16 found in string.h
		char *tokens[MAX_ARGUMENTS];
		strsplit(utilityName, "@", tokens, &number_of_tokens) ;
		char *progName = tokens[1];
		struct Env* env = NULL ;
		bool chkAscending = 1;
		int prevEnvID = -1 ;

		if (*numOfInstances < 0)
		{
			chkAscending = 0;
			*numOfInstances *= -1;
			prevEnvID = 1<<30 ;
		}
		bool success = 1;

		acquire_spinlock(&ProcessQueues.qlock);
		{
			//REVERSE LOOP ON EXIT LIST (to be the same as the queue order)
			int numOfExitEnvs = LIST_SIZE(&ProcessQueues.env_exit_queue);
			env = LIST_LAST(&ProcessQueues.env_exit_queue);
			for (int i = numOfExitEnvs; i > 0; --i, env = LIST_PREV(env))
			{
				if (strcmp(env->prog_name, progName) != 0)
					continue;
				(*numOfInstances)-- ;

				//cprintf("%s: prevID = %d, nextID = %d\n", progName, prevEnvID, env->env_id);
				if (chkAscending)
				{
					if (prevEnvID > env->env_id)
					{
						success = 0;
						break;
					}
				}
				else
				{
					if (prevEnvID < env->env_id)
					{
						success = 0;
						break;
					}
				}
				prevEnvID = env->env_id;
			}
		}
		release_spinlock(&ProcessQueues.qlock);
		if (*numOfInstances != 0 || success == 0)
		{
			cons_lock();
			{
				cprintf("###########################################\n");
				cprintf("%s: check exit order is FAILED\n", progName);
				cprintf("###########################################\n");
			}
			cons_unlock();
			*numOfInstances = 0; //to indicate the failure of test
		}
		else
		{
			cons_lock();
			{
				cprintf("####################################################\n");
				cprintf("%s: check exit order is SUCCEEDED\n", progName);
				cprintf("####################################################\n");
			}
			cons_unlock();
			*numOfInstances = 1; //to indicate the success of test
		}
	}
	else if (strncmp(utilityName, "__NthClkRepl@", strlen("__NthClkRepl@")) == 0)
	{
		int number_of_tokens;
		//allocate array of char * of size MAX_ARGUMENTS = 16 found in string.h
		char *tokens[MAX_ARGUMENTS];
		strsplit(utilityName, "@", tokens, &number_of_tokens) ;
		int type = strtol(tokens[1], NULL, 10);
		int N = value;
		if (type == 2)
			N *= -1;
		setPageReplacmentAlgorithmNchanceCLOCK(N);
		cons_lock();
		{
			cprintf("\n*********************************************************"
					"\nPAGE REPLACEMENT IS SET TO Nth Clock type = %d (N = %d)."
					"\n*********************************************************\n", type, N);
		}
		cons_unlock();
	}
	else if (strcmp(utilityName, "__Sleep__") == 0)
	{
		if (__firstTimeSleep)
		{
			__firstTimeSleep = 0;
			init_channel(&__tstchan__, "Test Channel");
			init_spinlock(&__tstchan_lk__, "Test Channel Lock");
		}
		acquire_spinlock(&__tstchan_lk__);
		sleep(&__tstchan__, &__tstchan_lk__);
		release_spinlock(&__tstchan_lk__);
	}
	else if (strcmp(utilityName, "__WakeupOne__") == 0)
	{
		wakeup_one(&__tstchan__);
	}
	else if (strcmp(utilityName, "__WakeupAll__") == 0)
	{
		wakeup_all(&__tstchan__);
	}
	else if (strcmp(utilityName, "__GetChanQueueSize__") == 0)
	{
		int* numOfProcesses = (int*) value ;
		*numOfProcesses = LIST_SIZE(&__tstchan__.queue);
	}
	else if (strcmp(utilityName, "__GetReadyQueueSize__") == 0)
	{
		int* numOfProcesses = (int*) value ;
		*numOfProcesses = LIST_SIZE(&ProcessQueues.env_ready_queues[0]);
	}
	else if (strcmp(utilityName, "__AcquireSleepLock__") == 0)
	{
		if (__firstTimeSleepLock)
		{
			__firstTimeSleepLock = 0;
			init_sleeplock(&__tstslplk__, "Test Sleep Lock");
		}
		acquire_sleeplock(&__tstslplk__);
	}
	else if (strcmp(utilityName, "__ReleaseSleepLock__") == 0)
	{
		release_sleeplock(&__tstslplk__);
	}
	else if (strcmp(utilityName, "__GetLockQueueSize__") == 0)
	{
		int* numOfProcesses = (int*) value ;
		*numOfProcesses = LIST_SIZE(&__tstslplk__.chan.queue);
	}
	else if (strcmp(utilityName, "__GetLockValue__") == 0)
	{
		int* lockVal = (int*) value ;
		*lockVal =__tstslplk__.locked;
	}
	else if (strcmp(utilityName, "__GetLockOwner__") == 0)
	{
		uint32* lockOwnerID = (uint32*) value ;
		*lockOwnerID =__tstslplk__.pid;
	}
	else if (strcmp(utilityName, "__GetConsLockedCnt__") == 0)
	{
		uint32* consLockCnt = (uint32*) value ;
		*consLockCnt = queue_size(&(conslock.chan.queue));
	}
	else if (strcmp(utilityName, "__tmpReleaseConsLock__") == 0)
	{
		if (CONS_LCK_METHOD == LCK_SLEEP)
		{
			conslock.pid = get_cpu_proc()->env_id;
			cons_unlock();
		}
	}
	else if (strcmp(utilityName, "__getKernelSBreak__") == 0)
	{
		uint32* ksbrk = (uint32*) value ;
		*ksbrk = (uint32)sbrk(0);
	}
	else if (strcmp(utilityName, "__changeInterruptStatus__") == 0)
	{
		if (value == 0)
		{
			kclock_stop();
			cli();
			struct Env * p = get_cpu_proc();
			if (p == NULL)
			{
				panic("cons_lock: no running process to block");
			}
			p->env_tf->tf_eflags &= ~FL_IF ;
			//cprintf("\nINTERRUPT WILL BE DISABLED\n");
		}
		else if (value == 1)
		{
			kclock_stop();
			cli();
			struct Env * p = get_cpu_proc();
			if (p == NULL)
			{
				panic("cons_unlock: no running process to block");
			}
			p->env_tf->tf_eflags |= FL_IF ;
			//cprintf("\nINTERRUPT WILL BE ENABLED\n");
		}
	}

	if ((int)value < 0)
	{
		if (strcmp(utilityName, "__ReplStrat__") == 0)
		{
			switch (value)
			{
			case -PG_REP_FIFO:
				cprintf("\n*************************************\nPAGE REPLACEMENT IS SET TO FIFO.\n*************************************\n");
				setPageReplacmentAlgorithmFIFO();
				break;
			case -PG_REP_LRU_LISTS_APPROX:
				cprintf("\n*************************************\nPAGE REPLACEMENT IS SET TO LRU LISTS.\n*************************************\n");
				setPageReplacmentAlgorithmLRU(PG_REP_LRU_LISTS_APPROX);
				break;
			case -PG_REP_NchanceCLOCK:
				cprintf("\n*************************************\nPAGE REPLACEMENT IS SET TO Nth Clock Normal (N=1).\n*************************************\n");
				setPageReplacmentAlgorithmNchanceCLOCK(1);
				break;
			default:
				break;
			}
		}
	}
	/*****************************************************************************************/

}
/*=======================================*/
void detect_loop_in_FrameInfo_list(struct FrameInfo_List* fi_list)
{
	struct  FrameInfo * slowPtr = LIST_FIRST(fi_list);
	struct  FrameInfo * fastPtr = LIST_FIRST(fi_list);


	while (slowPtr && fastPtr) {
		fastPtr = LIST_NEXT(fastPtr); // advance the fast pointer
		if (fastPtr == slowPtr) // and check if its equal to the slow pointer
		{
			cprintf("loop detected in modiflist\n");
			break;
		}

		if (fastPtr == NULL) {
			break; // since fastPtr is NULL we reached the tail
		}

		fastPtr = LIST_NEXT(fastPtr); //advance and check again
		if (fastPtr == slowPtr) {
			cprintf("loop detected in list\n");
			break;
		}

		slowPtr = LIST_NEXT(slowPtr); // advance the slow pointer only once
	}
	cprintf("finished  loop detection\n");
}

void scarce_memory()
{
	uint32 total_size_tobe_allocated = ((100 - memory_scarce_threshold_percentage)*number_of_frames)/100;
	//	cprintf("total_size_tobe_allocated %d\n", number_of_frames);
	if (((100 - memory_scarce_threshold_percentage)*number_of_frames) % 100 > 0)
		total_size_tobe_allocated++;

	int fflSize = 0;
	acquire_spinlock(&MemFrameLists.mfllock);
	{
		fflSize = LIST_SIZE(&MemFrameLists.free_frame_list);

		uint32 size_of_already_allocated = number_of_frames - fflSize ;
		uint32 size_tobe_allocated = total_size_tobe_allocated - size_of_already_allocated;
		//	cprintf("size_of_already_allocated %d\n", size_of_already_allocated);
		//	cprintf("size to be allocated %d\n", size_tobe_allocated);
		int i = 0 ;
		struct FrameInfo* ptr_tmp_FI ;
		for (; i <= size_tobe_allocated ; i++)
		{
			allocate_frame(&ptr_tmp_FI) ;
		}
	}
	release_spinlock(&MemFrameLists.mfllock);

}

uint32 calc_no_pages_tobe_removed_from_ready_exit_queues(uint32 WS_or_MEMORY_flag)
{
	uint32 no_of_pages_tobe_removed_from_ready = 0;
	uint32 no_of_pages_tobe_removed_from_exit = 0;
	uint32 no_of_pages_tobe_removed_from_curenv = 0;
	struct Env* cur_env = get_cpu_proc();
	assert(cur_env != NULL);
	if(WS_or_MEMORY_flag == 1)	// THEN MEMORY SHALL BE FREED
	{
		acquire_spinlock(&ProcessQueues.qlock);
		{
			for(int i = 0; i < num_of_ready_queues; i++)
			{
				struct Env * ptr_ready_env = NULL;
				LIST_FOREACH(ptr_ready_env, &(ProcessQueues.env_ready_queues[i]))
				{
#if USE_KHEAP
					int num_of_pages_in_WS = LIST_SIZE(&(ptr_ready_env->page_WS_list));
#else
					int num_of_pages_in_WS = env_page_ws_get_size(ptr_ready_env);
#endif
					int num_of_pages_to_be_removed = cur_env->percentage_of_WS_pages_to_be_removed * num_of_pages_in_WS / 100;
					if ((cur_env->percentage_of_WS_pages_to_be_removed * num_of_pages_in_WS) % 100 > 0)
						num_of_pages_to_be_removed++;
					no_of_pages_tobe_removed_from_ready += num_of_pages_to_be_removed;
				}
			}

			struct Env * ptr_exit_env = NULL;
			LIST_FOREACH(ptr_exit_env, &ProcessQueues.env_exit_queue)
			{
#if USE_KHEAP
				int num_of_pages_in_WS = LIST_SIZE(&(ptr_exit_env->page_WS_list));
#else
				int num_of_pages_in_WS = env_page_ws_get_size(ptr_exit_env);
#endif
				no_of_pages_tobe_removed_from_exit += num_of_pages_in_WS;
			}
		}
		release_spinlock(&ProcessQueues.qlock);
		if(cur_env != NULL)
		{
#if USE_KHEAP
			int num_of_pages_in_WS = LIST_SIZE(&(cur_env->page_WS_list));
#else
			int num_of_pages_in_WS = env_page_ws_get_size(cur_env);
#endif
			int num_of_pages_to_be_removed = cur_env->percentage_of_WS_pages_to_be_removed * num_of_pages_in_WS / 100;
			if ((cur_env->percentage_of_WS_pages_to_be_removed * num_of_pages_in_WS) % 100 > 0)
				num_of_pages_to_be_removed++;
			no_of_pages_tobe_removed_from_curenv = num_of_pages_to_be_removed;
		}
	}
	else	// THEN RAPID PROCESS SHALL BE FREED ONLY
	{
#if USE_KHEAP
		int num_of_pages_in_WS = LIST_SIZE(&(cur_env->page_WS_list));
#else
		int num_of_pages_in_WS = env_page_ws_get_size(cur_env);
#endif
		int num_of_pages_to_be_removed = cur_env->percentage_of_WS_pages_to_be_removed * num_of_pages_in_WS / 100;
		if ((cur_env->percentage_of_WS_pages_to_be_removed * num_of_pages_in_WS) % 100 > 0)
			num_of_pages_to_be_removed++;
		no_of_pages_tobe_removed_from_curenv = num_of_pages_to_be_removed;
	}

	return no_of_pages_tobe_removed_from_curenv + no_of_pages_tobe_removed_from_ready + no_of_pages_tobe_removed_from_exit;
}


void schenv()
{

	__nl = 0;
	__ne = NULL;
	acquire_spinlock(&ProcessQueues.qlock);
	{
		for (int i = 0; i < num_of_ready_queues; ++i)
		{
			if (queue_size(&(ProcessQueues.env_ready_queues[i])))
			{
				__ne = LIST_LAST(&(ProcessQueues.env_ready_queues[i]));
				__nl = i;
				break;
			}
		}
	}
	release_spinlock(&ProcessQueues.qlock);
	struct Env* cur_env = get_cpu_proc();
	if (cur_env != NULL)
	{
		if (__ne != NULL)
		{
			if ((__pl + 1) < __nl)
			{
				__ne = cur_env;
				__nl = __pl < num_of_ready_queues-1? __pl + 1 : __pl ;
			}
		}
		else
		{
			__ne = cur_env;
			__nl = __pl < num_of_ready_queues-1? __pl + 1 : __pl ;
		}
	}
}

void chksch(uint8 onoff)
{
	/*TEST TRADITIONAL MLFQ*/
	//	{
	//		__pe = NULL;
	//		__ne = NULL;
	//		__pl = 0 ;
	//		__nl = 0 ;
	//	}

	/*TEST BSD*/
	if (isSchedMethodBSD())
	{
		__histla = __pla = get_load_average();
		acquire_spinlock(&ProcessQueues.qlock);
		{
			__pnexit = LIST_SIZE(&ProcessQueues.env_exit_queue) ;
		}
		release_spinlock(&ProcessQueues.qlock);
		__firsttime = 1;
	}
	__chkstatus = onoff;
}
void chk1()
{
	/*TEST TRADITIONAL MLFQ*/
	//	{
	//		if (__chkstatus == 0)
	//			return ;
	//		__pe = get_cpu_proc();
	//		__pl = __nl ;
	//		if (__pe == NULL)
	//		{
	//			__pl = 0;
	//		}
	//		//cprintf("chk1: current = %s @ level %d\n", __pe == NULL? "NULL" : __pe->prog_name, __pl);
	//		schenv();
	//	}
}
void chk2(struct Env* __se)
{
	if (__chkstatus == 0)
		return ;

	/*TEST BSD*/
	if (isSchedMethodBSD())
	{
		__nla = get_load_average();
		acquire_spinlock(&ProcessQueues.qlock);
		{
			__nnexit = LIST_SIZE(&ProcessQueues.env_exit_queue);
		}
		release_spinlock(&ProcessQueues.qlock);

		if (__firsttime)
		{
			acquire_spinlock(&ProcessQueues.qlock);
			{
				//Cnt #Processes
				__nproc = __se != NULL? 1 : 0;
				for (int l = num_of_ready_queues-1; l >= 0; --l)
				{
					__nproc += LIST_SIZE(&(ProcessQueues.env_ready_queues[l]));
				}
				__firsttime = 0;
			}
			release_spinlock(&ProcessQueues.qlock);
		}
		else
		{
			if (__pnexit != __nnexit)
			{
				acquire_spinlock(&ProcessQueues.qlock);
				{
					//Cnt #Processes
					__nproc = __se != NULL? 1 : 0;
					for (int l = num_of_ready_queues-1; l >= 0; --l)
					{
						__nproc += LIST_SIZE(&(ProcessQueues.env_ready_queues[l]));
					}
				}
				release_spinlock(&ProcessQueues.qlock);
			}

			//Make sure that the la is changed over long period of time
			if (timer_ticks() % 1000 == 0)
			{
				assert_endall(__histla != __nla) ;
				__histla = __nla;
			}

			//check every 1 sec, assuming quantum >= 10
			if (timer_ticks() % 100 == 0)
			{
				int plaint = __pla / 100 ;
				int plafrc = __pla % 100 ;

				int nlaint = __nla / 100 ;
				int nlafrc = __nla % 100 ;

				//Check at steady state of nproc (include equality)
				if (__nnexit == __pnexit)
				{
					//cprintf("++++++++++++++++++# processes = %d, prev la = %d.%d, next la = %d.%d\n", __nproc, plaint, plafrc, nlaint, nlafrc);
					if (__nproc > plaint)
					{
						cprintf("++++++++++++++++++# processes = %d, prev la = %d.%d, next la = %d.%d\n", __nproc, plaint, plafrc, nlaint, nlafrc);
						//assert_endall(__nla > __pla);
						assert_endall((nlaint > plaint) || ((nlaint == plaint) && (nlafrc >= plafrc)));
					}
					else if (__nproc < plaint)
					{
						cprintf("------------------# processes = %d, prev la = %d.%d, next la = %d.%d\n", __nproc, plaint, plafrc, nlaint, nlafrc);
						//assert_endall(__nla < __pla);
						assert_endall((nlaint < plaint) || ((nlaint == plaint) && (nlafrc <= plafrc)));
					}
					else if (__nproc == plaint)
					{
						assert_endall((nlaint == plaint));
					}
				}
				__pla = __nla;
			}
		}
		__pnexit = __nnexit;

	}
	/*TEST TRADITIONAL MLFQ*/
	//	{
	//		//cprintf("chk2: next = %s @ level %d\n", __ne == NULL? "NULL" : __ne->prog_name, __nl);
	//
	//		assert_endall(__se == __ne);
	//		//cprintf("%d - %d\n", kclock_read_cnt0_latch() , TIMER_DIV((1000/quantums[__nl])));
	//
	//		if (__ne != NULL)
	//		{
	//			uint16 upper = TIMER_DIV((1000/quantums[__nl])) ;
	//			upper = upper % 2 == 1? upper+1 : upper ;
	//			uint16 lower = 90 * upper / 100 ;
	//			uint16 current = kclock_read_cnt0();
	//			//cprintf("current = %d, lower = %d, upper = %d\n", current, lower, upper);
	//			assert_endall(current > lower && current <= upper) ;
	//
	//			for (int i = 0; i < num_of_ready_queues; ++i)
	//			{
	//				assert_endall(find_env_in_queue(&(env_ready_queues[i]), __ne->env_id) == NULL);
	//			}
	//		}
	//		if (__pe != NULL && __pe != __ne)
	//		{
	//			uint8 __tl = __pl == num_of_ready_queues-1 ? __pl : __pl + 1 ;
	//			assert_endall(find_env_in_queue(&(env_ready_queues[__tl]), __pe->env_id) != NULL);
	//			for (int i = 0; i < num_of_ready_queues; ++i)
	//			{
	//				if (i == __tl) continue;
	//				assert_endall(find_env_in_queue(&(env_ready_queues[i]), __pe->env_id) == NULL) ;
	//			}
	//		}
	//	}
}

//
// Checks that the kernel part of virtual address space
// has been setup roughly correctly(by initialize_kernel_VM()).
//
// This function doesn't test every corner case,
// in fact it doesn't test the permission bits at all,
// but it is a pretty good check.
//
uint32 check_va2pa(uint32 *ptr_page_directory, uint32 va);

void check_boot_pgdir()
{
	uint32 i, n;

	// check frames_info array
	//2016: READ_ONLY_FRAMES_INFO not valid any more since it can't fit in 4 MB space
	//	n = ROUNDUP(number_of_frames*sizeof(struct Frame_Info), PAGE_SIZE);
	//	for (i = 0; i < n; i += PAGE_SIZE)
	//	{
	//		//cprintf("i = %x, arg 1  = %x, arg 2 = %x \n",i, check_va2pa(ptr_page_directory, READ_ONLY_FRAMES_INFO + i), STATIC_KERNEL_PHYSICAL_ADDRESS(frames_info) + i);
	//		assert(check_va2pa(ptr_page_directory, READ_ONLY_FRAMES_INFO + i) == STATIC_KERNEL_PHYSICAL_ADDRESS(frames_info) + i);
	//	}

	//2016
	// check phys mem
#if USE_KHEAP
	{
		for (i = 0; KERNEL_BASE + i < (uint32)ptr_free_mem; i += PAGE_SIZE)
			assert(check_va2pa(ptr_page_directory, KERNEL_BASE + i) == i);
	}
#else
	{
		for (i = 0; KERNEL_BASE + i != 0; i += PAGE_SIZE)
			assert(check_va2pa(ptr_page_directory, KERNEL_BASE + i) == i);
	}
#endif
	// check scheduler kernel stack
	for (i = 0; i < NCPUS*KERNEL_STACK_SIZE; i += PAGE_SIZE)
	{
		//skip GUARD page of each CPU Stack
		if (i%KERNEL_STACK_SIZE == 0)
			continue;
		assert(check_va2pa(ptr_page_directory, KERN_STACK_TOP - NCPUS*KERNEL_STACK_SIZE + i) == STATIC_KERNEL_PHYSICAL_ADDRESS(ptr_stack_bottom) + i);
	}
	// check for zero/non-zero in PDEs
	for (i = 0; i < NPDENTRIES; i++) {
		switch (i) {
		case PDX(VPT):
		case PDX(UVPT):
		case PDX(KERN_STACK_TOP-1):
		case PDX(UENVS):
		//2016: READ_ONLY_FRAMES_INFO not valid any more since it can't fit in 4 MB space
		//case PDX(READ_ONLY_FRAMES_INFO):
		assert(ptr_page_directory[i]);
		break;
		default:
			if (i >= PDX(KERNEL_BASE))
				assert(ptr_page_directory[i]);
			else
				assert(ptr_page_directory[i] == 0);
			break;
		}
	}
	cprintf("*	check_boot_pgdir() succeeded!\n");
}

// This function returns the physical address of the page containing 'va',
// defined by the page directory 'ptr_page_directory'.  The hardware normally performs
// this functionality for us!  We define our own version to help check
// the check_boot_pgdir() function; it shouldn't be used elsewhere.

uint32 check_va2pa(uint32 *ptr_page_directory, uint32 va)
{
	uint32 *p;

	uint32* dirEntry = &(ptr_page_directory[PDX(va)]);

	//LOG_VARS("dir table entry %x", *dirEntry);

	if (!(*dirEntry & PERM_PRESENT))
		return ~0;
	p = (uint32*) STATIC_KERNEL_VIRTUAL_ADDRESS(EXTRACT_ADDRESS(*dirEntry));

	//LOG_VARS("ptr to page table  = %x", p);

	if (!(p[PTX(va)] & PERM_PRESENT))
		return ~0;

	//LOG_VARS("page phys addres = %x",EXTRACT_ADDRESS(p[PTX(va)]));
	return EXTRACT_ADDRESS(p[PTX(va)]);
}

/*
void page_check()
{
	struct Frame_Info *pp, *pp0, *pp1, *pp2;
	struct Linked_List fl;

	// should be able to allocate three frames_info
	pp0 = pp1 = pp2 = 0;
	assert(allocate_frame(&pp0) == 0);
	assert(allocate_frame(&pp1) == 0);
	assert(allocate_frame(&pp2) == 0);

	assert(pp0);
	assert(pp1 && pp1 != pp0);
	assert(pp2 && pp2 != pp1 && pp2 != pp0);

	// temporarily steal the rest of the free frames_info
	fl = free_frame_list;
	LIST_INIT(&free_frame_list);

	// should be no free memory
	assert(allocate_frame(&pp) == E_NO_MEM);

	// there is no free memory, so we can't allocate a page table
	assert(map_frame(ptr_page_directory, pp1, 0x0, 0) < 0);

	// free pp0 and try again: pp0 should be used for page table
	free_frame(pp0);
	assert(map_frame(ptr_page_directory, pp1, 0x0, 0) == 0);
	assert(EXTRACT_ADDRESS(ptr_page_directory[0]) == to_physical_address(pp0));
	assert(check_va2pa(ptr_page_directory, 0x0) == to_physical_address(pp1));
	assert(pp1->references == 1);
	assert(pp0->references == 1);

	// should be able to map pp2 at PAGE_SIZE because pp0 is already allocated for page table
	assert(map_frame(ptr_page_directory, pp2, (void*) PAGE_SIZE, 0) == 0);
	assert(check_va2pa(ptr_page_directory, PAGE_SIZE) == to_physical_address(pp2));
	assert(pp2->references == 1);

	// should be no free memory
	assert(allocate_frame(&pp) == E_NO_MEM);

	// should be able to map pp2 at PAGE_SIZE because it's already there
	assert(map_frame(ptr_page_directory, pp2, (void*) PAGE_SIZE, 0) == 0);
	assert(check_va2pa(ptr_page_directory, PAGE_SIZE) == to_physical_address(pp2));
	assert(pp2->references == 1);

	// pp2 should NOT be on the free list
	// could happen in ref counts are handled sloppily in map_frame
	assert(allocate_frame(&pp) == E_NO_MEM);

	// should not be able to map at PTSIZE because need free frame for page table
	assert(map_frame(ptr_page_directory, pp0, (void*) PTSIZE, 0) < 0);

	// insert pp1 at PAGE_SIZE (replacing pp2)
	assert(map_frame(ptr_page_directory, pp1, (void*) PAGE_SIZE, 0) == 0);

	// should have pp1 at both 0 and PAGE_SIZE, pp2 nowhere, ...
	assert(check_va2pa(ptr_page_directory, 0) == to_physical_address(pp1));
	assert(check_va2pa(ptr_page_directory, PAGE_SIZE) == to_physical_address(pp1));
	// ... and ref counts should reflect this
	assert(pp1->references == 2);
	assert(pp2->references == 0);

	// pp2 should be returned by allocate_frame
	assert(allocate_frame(&pp) == 0 && pp == pp2);

	// unmapping pp1 at 0 should keep pp1 at PAGE_SIZE
	unmap_frame(ptr_page_directory, 0x0);
	assert(check_va2pa(ptr_page_directory, 0x0) == ~0);
	assert(check_va2pa(ptr_page_directory, PAGE_SIZE) == to_physical_address(pp1));
	assert(pp1->references == 1);
	assert(pp2->references == 0);

	// unmapping pp1 at PAGE_SIZE should free it
	unmap_frame(ptr_page_directory, (void*) PAGE_SIZE);
	assert(check_va2pa(ptr_page_directory, 0x0) == ~0);
	assert(check_va2pa(ptr_page_directory, PAGE_SIZE) == ~0);
	assert(pp1->references == 0);
	assert(pp2->references == 0);

	// so it should be returned by allocate_frame
	assert(allocate_frame(&pp) == 0 && pp == pp1);

	// should be no free memory
	assert(allocate_frame(&pp) == E_NO_MEM);

	// forcibly take pp0 back
	assert(EXTRACT_ADDRESS(ptr_page_directory[0]) == to_physical_address(pp0));
	if(USE_KHEAP)
	{
		kfree((void*)kheap_virtual_address(EXTRACT_ADDRESS(ptr_page_directory[0])));
	}
	else
	{
		ptr_page_directory[0] = 0;
		assert(pp0->references == 1);
		pp0->references = 0;
		free_frame(pp0);
	}
	// give free list back
	free_frame_list = fl;

	// free the frames_info we took
	free_frame(pp1);
	free_frame(pp2);

	cprintf("page_check() succeeded!\n");
}
 */

//
