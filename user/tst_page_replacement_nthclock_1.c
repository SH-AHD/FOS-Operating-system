/* *********************************************************** */
/* MAKE SURE PAGE_WS_MAX_SIZE = 11 */
/* *********************************************************** */

#include <inc/lib.h>

char __arr__[PAGE_SIZE*12];
char* __ptr__ = (char* )0x0801000 ;
char* __ptr2__ = (char* )0x0804000 ;
uint32 expectedInitialVAs[11] = {
		0x200000, 0x201000, 0x202000, 0x203000, 0x204000, 0x205000, 					//Unused
		0x800000, 0x801000, 0x802000, 0x803000,											//Code & Data
		0xeebfd000, /*0xedbfd000 will be created during the call of sys_check_WS_list*/ //Stack
} ;

uint32 expectedFinalVAs[11] = {
		0xeebfd000, /*will be created during the call of sys_check_WS_list*/ //Stack
		0x80a000, 0x804000, 0x80b000, 0x80c000,0x807000,0x800000,0x801000,0x808000,0x809000,0x803000,	//Code & Data
} ;

void fillPage(char* __arr__, int pageIdx, char val)
{
	for (int i = pageIdx*PAGE_SIZE; i < (pageIdx+1)*PAGE_SIZE; ++i)
	{
		__arr__[i] = val;
	}
}

void _main(void)
{

	//("STEP 0: checking Initial WS entries ...\n");
	bool found ;

#if USE_KHEAP
	{
		found = sys_check_WS_list(expectedInitialVAs, 11, 0x200000, 1);
		if (found != 1) panic("INITIAL PAGE WS entry checking failed! Review size of the WS!!\n*****IF CORRECT, CHECK THE ISSUE WITH THE STAFF*****");
	}
#else
	panic("make sure to enable the kernel heap: USE_KHEAP=1");
#endif

	int freePages = sys_calculate_free_frames();
	int usedDiskPages = sys_pf_calculate_allocated_pages();

	//Reading (Not Modified)
	char garbage1 = __arr__[PAGE_SIZE*11-1] ;
	char garbage2 = __arr__[PAGE_SIZE*12-1] ;
	char garbage4,garbage5;

	//Writing (Modified)
	int i ;
	for (i = 0 ; i < PAGE_SIZE*10 ; i+=PAGE_SIZE/2)
	{
		__arr__[i] = 'A' ;
		/*2016: this BUGGY line is REMOVED el7! it overwrites the KERNEL CODE :( !!!*/
		//*__ptr__ = *__ptr2__ ;
		/*==========================================================================*/
		//always use pages at 0x801000 and 0x804000
		garbage4 = *__ptr__ ;
		if (i % PAGE_SIZE == 0)
			garbage5 = *__ptr2__ ;

//		if (((i/PAGE_SIZE) + 1) % 3 == 0)
//		{
//			cprintf("BEFORE FILL...\n");
//			fillPage(__arr__, (i/PAGE_SIZE) - 2, 'A');
//			cprintf("AFTER FILL\n");
//		}
	}

	//===================

	//cprintf("Checking PAGE nth clock algorithm... \n");
	{
		found = sys_check_WS_list(expectedFinalVAs, 11, 0x807000, 1);
		if (found != 1) panic("Page Nth clock algo failed.. trace it by printing WS before and after page fault");
	}
	{
		if (garbage4 != *__ptr__) panic("test failed!");
		if (garbage5 != *__ptr2__) panic("test failed!");
	}

	//cprintf("Congratulations!! test PAGE replacement [Nth clock Alg.] is completed successfully.\n");
	atomic_cprintf("%~\nCongratulations!!... test is completed.\n");

	return;
}
