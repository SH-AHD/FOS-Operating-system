/*
 * fault_handler.c
 *
 *  Created on: Oct 12, 2022
 *      Author: HP
 */

#include "trap.h"
#include <kern/proc/user_environment.h>
#include <kern/cpu/sched.h>
#include <kern/cpu/cpu.h>
#include <kern/disk/pagefile_manager.h>
#include <kern/mem/memory_manager.h>

//2014 Test Free(): Set it to bypass the PAGE FAULT on an instruction with this length and continue executing the next one
// 0 means don't bypass the PAGE FAULT
uint8 bypassInstrLength = 0;

//===============================
// REPLACEMENT STRATEGIES
//===============================
//2020
void setPageReplacmentAlgorithmLRU(int LRU_TYPE)
{
	assert(LRU_TYPE == PG_REP_LRU_TIME_APPROX || LRU_TYPE == PG_REP_LRU_LISTS_APPROX);
	_PageRepAlgoType = LRU_TYPE ;
}
void setPageReplacmentAlgorithmCLOCK(){_PageRepAlgoType = PG_REP_CLOCK;}
void setPageReplacmentAlgorithmFIFO(){_PageRepAlgoType = PG_REP_FIFO;}
void setPageReplacmentAlgorithmModifiedCLOCK(){_PageRepAlgoType = PG_REP_MODIFIEDCLOCK;}
/*2018*/ void setPageReplacmentAlgorithmDynamicLocal(){_PageRepAlgoType = PG_REP_DYNAMIC_LOCAL;}
/*2021*/ void setPageReplacmentAlgorithmNchanceCLOCK(int PageWSMaxSweeps){_PageRepAlgoType = PG_REP_NchanceCLOCK;  page_WS_max_sweeps = PageWSMaxSweeps;}

//2020
uint32 isPageReplacmentAlgorithmLRU(int LRU_TYPE){return _PageRepAlgoType == LRU_TYPE ? 1 : 0;}
uint32 isPageReplacmentAlgorithmCLOCK(){if(_PageRepAlgoType == PG_REP_CLOCK) return 1; return 0;}
uint32 isPageReplacmentAlgorithmFIFO(){if(_PageRepAlgoType == PG_REP_FIFO) return 1; return 0;}
uint32 isPageReplacmentAlgorithmModifiedCLOCK(){if(_PageRepAlgoType == PG_REP_MODIFIEDCLOCK) return 1; return 0;}
/*2018*/ uint32 isPageReplacmentAlgorithmDynamicLocal(){if(_PageRepAlgoType == PG_REP_DYNAMIC_LOCAL) return 1; return 0;}
/*2021*/ uint32 isPageReplacmentAlgorithmNchanceCLOCK(){if(_PageRepAlgoType == PG_REP_NchanceCLOCK) return 1; return 0;}

//===============================
// PAGE BUFFERING
//===============================
void enableModifiedBuffer(uint32 enableIt){_EnableModifiedBuffer = enableIt;}
uint8 isModifiedBufferEnabled(){  return _EnableModifiedBuffer ; }

void enableBuffering(uint32 enableIt){_EnableBuffering = enableIt;}
uint8 isBufferingEnabled(){  return _EnableBuffering ; }

void setModifiedBufferLength(uint32 length) { _ModifiedBufferLength = length;}
uint32 getModifiedBufferLength() { return _ModifiedBufferLength;}

//===============================
// FAULT HANDLERS
//===============================

//==================
// [1] MAIN HANDLER:
//==================
/*2022*/
uint32 last_eip = 0;
uint32 before_last_eip = 0;
uint32 last_fault_va = 0;
uint32 before_last_fault_va = 0;
int8 num_repeated_fault  = 0;

struct Env* last_faulted_env = NULL;
void fault_handler(struct Trapframe *tf)
{
	/******************************************************/
	// Read processor's CR2 register to find the faulting address
	uint32 fault_va = rcr2();
	//	cprintf("\n************Faulted VA = %x************\n", fault_va);
	//	print_trapframe(tf);
	/******************************************************/

	//If same fault va for 3 times, then panic
	//UPDATE: 3 FAULTS MUST come from the same environment (or the kernel)
	struct Env* cur_env = get_cpu_proc();
	if (last_fault_va == fault_va && last_faulted_env == cur_env)
	{
		num_repeated_fault++ ;
		if (num_repeated_fault == 3)
		{
			print_trapframe(tf);
			panic("Failed to handle fault! fault @ at va = %x from eip = %x causes va (%x) to be faulted for 3 successive times\n", before_last_fault_va, before_last_eip, fault_va);
		}
	}
	else
	{
		before_last_fault_va = last_fault_va;
		before_last_eip = last_eip;
		num_repeated_fault = 0;
	}
	last_eip = (uint32)tf->tf_eip;
	last_fault_va = fault_va ;
	last_faulted_env = cur_env;
	/******************************************************/
	//2017: Check stack overflow for Kernel
	int userTrap = 0;
	if ((tf->tf_cs & 3) == 3) {
		userTrap = 1;
	}
	if (!userTrap)
	{
		struct cpu* c = mycpu();
		//cprintf("trap from KERNEL\n");
		if (cur_env && fault_va >= (uint32)cur_env->kstack && fault_va < (uint32)cur_env->kstack + PAGE_SIZE)
			panic("User Kernel Stack: overflow exception!");
		else if (fault_va >= (uint32)c->stack && fault_va < (uint32)c->stack + PAGE_SIZE)
			panic("Sched Kernel Stack of CPU #%d: overflow exception!", c - CPUS);
#if USE_KHEAP
		if (fault_va >= KERNEL_HEAP_MAX)
			panic("Kernel: heap overflow exception!");
#endif
	}
	//2017: Check stack underflow for User
	else
	{
		//cprintf("trap from USER\n");
		if (fault_va >= USTACKTOP && fault_va < USER_TOP)
			panic("User: stack underflow exception!");
	}

	//get a pointer to the environment that caused the fault at runtime
	//cprintf("curenv = %x\n", curenv);
	struct Env* faulted_env = cur_env;
	if (faulted_env == NULL)
	{
		print_trapframe(tf);
		panic("faulted env == NULL!");
	}
	//check the faulted address, is it a table or not ?
	//If the directory entry of the faulted address is NOT PRESENT then
	if ( (faulted_env->env_page_directory[PDX(fault_va)] & PERM_PRESENT) != PERM_PRESENT)
	{
		// we have a table fault =============================================================
		//		cprintf("[%s] user TABLE fault va %08x\n", curenv->prog_name, fault_va);
		//		print_trapframe(tf);

		faulted_env->tableFaultsCounter ++ ;

		table_fault_handler(faulted_env, fault_va);
	}
	else
	{
		if (userTrap)
		{




			///OURCODE
			 uint32 curadd = fault_va - USER_HEAP_START;
			      uint32 index = curadd / PAGE_SIZE;
			      uint32 *ptr_page_table = NULL;
			      int p = pt_get_page_permissions(faulted_env->env_page_directory, fault_va);

			      if ((fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX)&&(!(p & PERM_MARKED)))
			      {
			    	//  cprintf("if ((fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX)&&(!(p & PERM_MARKED)))\n");
			    	  env_exit();}
			      if((p & PERM_PRESENT) && !(p & PERM_WRITEABLE)){
			    	  //cprintf(" if((p & PERM_PRESENT) && !(p & PERM_WRITEABLE))\n");
			    	  	 env_exit();}
			      if(fault_va>=USER_LIMIT){
			    	  //cprintf("fault_va>=USER_LIMIT\n");
			    	  env_exit();
			      }

			/*============================================================================================*/
		}

		/*2022: Check if fault due to Access Rights */
		int perms = pt_get_page_permissions(faulted_env->env_page_directory, fault_va);
		if (perms & PERM_PRESENT)
			panic("Page @va=%x is exist! page fault due to violation of ACCESS RIGHTS\n", fault_va) ;
		/*============================================================================================*/


		// we have normal page fault =============================================================
		faulted_env->pageFaultsCounter ++ ;



		if(isBufferingEnabled())
		{
			__page_fault_handler_with_buffering(faulted_env, fault_va);
		}
		else
		{
			//page_fault_handler(faulted_env, fault_va);
			page_fault_handler(faulted_env, fault_va);
		}


	}

	/*************************************************************/
	//Refresh the TLB cache
	tlbflush();
	/*************************************************************/
}

//=========================
// [2] TABLE FAULT HANDLER:
//=========================
void table_fault_handler(struct Env * curenv, uint32 fault_va)
{
	//panic("table_fault_handler() is not implemented yet...!!");
	//Check if it's a stack page
	uint32* ptr_table;
#if USE_KHEAP
	{
		ptr_table = create_page_table(curenv->env_page_directory, (uint32)fault_va);
	}
#else
	{
		__static_cpt(curenv->env_page_directory, (uint32)fault_va, &ptr_table);
	}
#endif
}

//=========================
// [3] PAGE FAULT HANDLER:
//=========================


void page_fault_handler(struct Env *faulted_env, uint32 fault_va)
{

#if USE_KHEAP
    struct WorkingSetElement *victimWSElement = NULL;
    uint32 wsSize = LIST_SIZE(&(faulted_env->page_WS_list));
#else
    int iWS = faulted_env->page_last_WS_index;
    uint32 wsSize = env_page_ws_get_size(faulted_env);
#endif

if(isPageReplacmentAlgorithmNchanceCLOCK())
{

    if (wsSize < faulted_env->page_WS_max_size) {
			//cprintf("inside1\n");
			uint32 *ptr_page_table = NULL;
			struct FrameInfo *allocframe = NULL;
			bool place_in_memory = 0;
			bool stack_heap = 0;
			allocate_frame(&allocframe);
			map_frame(faulted_env->env_page_directory, allocframe, fault_va, PERM_WRITEABLE | PERM_USER | PERM_MARKED);
			int ret = pf_read_env_page(faulted_env, &fault_va);
		   // cprintf("ret is : %d\n",ret);
		   // cprintf("fault_va is : %x\n",fault_va);
			if (ret == E_PAGE_NOT_EXIST_IN_PF) {
				if ((fault_va >= USTACKBOTTOM && fault_va < USTACKTOP) ||
					(fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX)){
				 // cprintf("Stack or heap\n");
				  place_in_memory = 1;
				}

			}  else {
				place_in_memory = 1;
			}

			if (place_in_memory) {
				//cprintf("founded\n");
				struct WorkingSetElement *workingsetelement = env_page_ws_list_create_element(faulted_env, fault_va);
				LIST_INSERT_TAIL(&(faulted_env->page_WS_list), workingsetelement);

				uint32 ws_size = LIST_SIZE(&(faulted_env->page_WS_list));

				// noooooooooooooooooooooooooootttttttttttttttttttttttttttteeeeeeeeeeeeeeeeeeeee if working set is full, make the last point to the first
				if (ws_size == faulted_env->page_WS_max_size) {
					faulted_env->page_last_WS_element = (struct WorkingSetElement *)LIST_FIRST(&(faulted_env->page_WS_list));
					if (faulted_env->page_last_WS_element != NULL &&
						faulted_env->page_last_WS_element != faulted_env->page_WS_list.lh_first) {
						// ------------------the tail point to the head-----------------------
						faulted_env->page_WS_list.lh_last->prev_next_info.le_next = faulted_env->page_WS_list.lh_first;
						// ------------------ the head's prev point to the tail-----------------------------
						faulted_env->page_WS_list.lh_first->prev_next_info.le_prev = faulted_env->page_WS_list.lh_last;
						// i will set the FIFO element's previous next to NULL
						faulted_env->page_last_WS_element->prev_next_info.le_prev->prev_next_info.le_next = NULL;
						// we should update the tail to the previous FIFO element
						faulted_env->page_WS_list.lh_last = faulted_env->page_last_WS_element->prev_next_info.le_prev;
						// set the FIFO element's prev to NULL
						faulted_env->page_last_WS_element->prev_next_info.le_prev = NULL;
						// we should update the head to the FIFO element
						faulted_env->page_WS_list.lh_first = faulted_env->page_last_WS_element;
					}
				} else {
					//cprintf("inside\n");
					faulted_env->page_last_WS_element = NULL;
								}

			   // cprintf("Working set size: %d\n", ws_size);
			}
			else if(place_in_memory==0 &&stack_heap==0){
				//cprintf("insideexist\n");
				unmap_frame(faulted_env->env_page_directory, fault_va);
				env_exit();
			}

		}


    else { //REP
				uint32 ws_size = LIST_SIZE(&(faulted_env->page_WS_list));
			   struct WorkingSetElement *last = faulted_env->page_last_WS_element;  // point to the next location in the WS after the last set one if list is full
			   struct WorkingSetElement *beforevictim = LIST_PREV(faulted_env->page_last_WS_element);
			   struct WorkingSetElement *victim=NULL;
			   bool victimFlag=0;

		if (page_WS_max_sweeps >= 0) { //NORMAL

			while(victimFlag==0){

			//cprintf(" page_WS_max_sweeps is : %d \n",page_WS_max_sweeps);
			int perm = pt_get_page_permissions(faulted_env->env_page_directory, last->virtual_address);
			if ((perm & PERM_USED))
			{ // USED

				 last->sweeps_counter = 0;
					// cprintf(" last counter IS : %d\n",last->sweeps_counter);
				pt_set_page_permissions(faulted_env->env_page_directory, last->virtual_address, 0, PERM_USED);  // clear use bit

			}
			else
			{ // NOT USED

			 last->sweeps_counter++;

			 if(last->sweeps_counter>=page_WS_max_sweeps){
				 	 	 victim=last;
						 victimFlag=1;
						 break;
				 }

	  }



			 if (last == LIST_LAST(&(faulted_env->page_WS_list))) { // if it's the last one in WS go to head
				   beforevictim=last;
				   last = LIST_FIRST(&(faulted_env->page_WS_list));  // wrap around to the first page
					  }
				   else {
					   beforevictim=last;

						 last=LIST_NEXT(last);
				   }// if used -> MOVE TO NEXT PAGE else will break ^^
			}


		}

	   else if (page_WS_max_sweeps < 0) {  // MODIFIED
		int num=-1;
		int max =(num*page_WS_max_sweeps);
		while(victimFlag==0){
			//cprintf("MODIFIY\n");
		  int perm = pt_get_page_permissions(faulted_env->env_page_directory, last->virtual_address);
		  if ((perm & PERM_MODIFIED))  {// PAGE MODIFIED
			if ((perm & PERM_USED))
			{ // USED
					last->sweeps_counter = 0;
					 pt_set_page_permissions(faulted_env->env_page_directory, last->virtual_address, 0, PERM_USED);  // clear use bit
			}
			else
			{ // NOT USED
				last->sweeps_counter++;
				if(last->sweeps_counter>=max+1){
					//cprintf("VICTIM IS : %p\n",last);
					 victim=last;
					victimFlag=1;
						 break;
				}
			}


		 if (last == LIST_LAST(&(faulted_env->page_WS_list))) { // if it's the last one in WS go to head
							beforevictim=last;
							 last = LIST_FIRST(&(faulted_env->page_WS_list));  // wrap around to the first page
						}
						else {
							beforevictim=last;
							last=LIST_NEXT(last);} // if used -> MOVE TO NEXT PAGE else will break ^^

		  }
		 else if (!(perm & PERM_MODIFIED))  {// PAGE NON-MODIFIED
			if ((perm & PERM_USED))
			{ // USED
					last->sweeps_counter = 0;
					 pt_set_page_permissions(faulted_env->env_page_directory, last->virtual_address, 0, PERM_USED);  // clear use bit
			}
			else
			{ // NOT USED
				last->sweeps_counter++;
				if(last->sweeps_counter>=max){
					//cprintf("VICTIM IS : %p\n",last);
					 victim=last;
					victimFlag=1;
						 break;
				}
			}


		 if (last == LIST_LAST(&(faulted_env->page_WS_list))) { // if it's the last one in WS go to head
							beforevictim=last;
							 last = LIST_FIRST(&(faulted_env->page_WS_list));  // wrap around to the first page
						}
						else {
							beforevictim=last;
							last=LIST_NEXT(last);
						} // if used -> MOVE TO NEXT PAGE else will break ^^

		 }
		}

	   }

		// Handle page replacement
		if(victim!=NULL)
		{

		 uint32 *ptrPageTable = NULL;
		 struct FrameInfo *allocframe = get_frame_info(faulted_env->env_page_directory, last->virtual_address, &ptrPageTable);
	   // if the page is modified, update it in the page file
		if (pt_get_page_permissions(faulted_env->env_page_directory, last->virtual_address) & PERM_MODIFIED) {
			pf_update_env_page(faulted_env, last->virtual_address, allocframe);
			}

		  int p=pt_get_page_permissions(faulted_env->env_page_directory,  last->virtual_address);
		 env_page_ws_invalidate(faulted_env,last->virtual_address);


			   // allocate a new frame for the page that caused the fault
				 struct FrameInfo *new_frame = NULL;
				 allocate_frame(&new_frame);
				 map_frame(faulted_env->env_page_directory, new_frame, fault_va, PERM_USER |PERM_MARKED |PERM_WRITEABLE);

	bool exitFlag=0;

		// read the faulted page from the page file
			int ret=pf_read_env_page(faulted_env, (void *)fault_va);
			if (ret == E_PAGE_NOT_EXIST_IN_PF) {
				if (!(fault_va >= USTACKBOTTOM && fault_va < USTACKTOP) &&
				   !(fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX)){
						exitFlag=1;
						//env_exit();
				   }
			}

			if(exitFlag==1)env_exit();

			   struct WorkingSetElement *new_page = env_page_ws_list_create_element(faulted_env, fault_va);
			   new_page->sweeps_counter=0;


			   if(beforevictim==NULL){
				   LIST_INSERT_HEAD(&(faulted_env->page_WS_list), new_page);
			   }

			   else LIST_INSERT_AFTER(&(faulted_env->page_WS_list),beforevictim, new_page);

			   if(last == LIST_LAST(&faulted_env->page_WS_list)){
							   faulted_env->page_last_WS_element=LIST_FIRST(&faulted_env->page_WS_list);
							  }
							   else faulted_env->page_last_WS_element=LIST_NEXT(last);

		return;
				}

			}
		 }

}



void __page_fault_handler_with_buffering(struct Env * curenv, uint32 fault_va)
{
	//[PROJECT] PAGE FAULT HANDLER WITH BUFFERING
	// your code is here, remove the panic and write your code
	panic("__page_fault_handler_with_buffering() is not implemented yet...!!");
}

