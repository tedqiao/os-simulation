/************************************************************************
 
 This code forms the base of the operating system you will
 build.  It has only the barest rudiments of what you will
 eventually construct; yet it contains the interfaces that
 allow test.c and z502.c to be successfully built together.
 
 Revision History:
 1.0 August 1990
 1.1 December 1990: Portability attempted.
 1.3 July     1992: More Portability enhancements.
 Add call to sample_code.
 1.4 December 1992: Limit (temporarily) printout in
 interrupt handler.  More portability.
 2.0 January  2000: A number of small changes.
 2.1 May      2001: Bug fixes and clear STAT_VECTOR
 2.2 July     2002: Make code appropriate for undergrads.
 Default program start is in test0.
 3.0 August   2004: Modified to support memory mapped IO
 3.1 August   2004: hardware interrupt runs on separate thread
 3.11 August  2004: Support for OS level locking
 4.0  July    2013: Major portions rewritten to support multiple threads
 ************************************************************************/

#include             "global.h"
#include             "syscalls.h"
#include             "protos.h"
#include             "string.h"
#include             "Queue.h"
#include             "z502.h"
#include             <pthread.h>

#define         ILLEGAL_PRIORITY                -3
#define         LEGAL_PRIORITY                  10
#define         DUPLICATED                      -4
#define                  DO_LOCK                     1
#define                  DO_UNLOCK                   0
#define                  SUSPEND_UNTIL_LOCKED        TRUE
#define                  DO_NOT_SUSPEND              FALSE
#define                     MAX_LENGTH              64
#define                     MAX_COUNT              20

// These loacations are global and define information about the page table
extern UINT16        *Z502_PAGE_TBL_ADDR;
extern INT16         Z502_PAGE_TBL_LENGTH;

extern void          *TO_VECTOR [];

char                 *call_names[] = { "mem_read ", "mem_write",
    "read_mod ", "get_time ", "sleep    ",
    "get_pid  ", "create   ", "term_proc",
    "suspend  ", "resume   ", "ch_prior ",
    "send     ", "receive  ", "disk_read",
    "disk_wrt ", "def_sh_ar" };




Queue *readyQueue;
Queue *timerQueue;
Queue *suspendQueue;
MsgQueue *msgQueue;
DiskQueue *diskQueue;
INT32 LockResult, LockResult2, LockResultPrinter, TimeLockResult;
INT32 currentPCBnum=1;
INT32 currenttime = 0;
PCB startPCB;
PCB currentPCB;
PCB temp;
char action[8];
UINT16 *PAGE_HEAD[10];
FRAME frame[PHYS_MEM_PGS];

INT32 start_timer(long *SleepTime);
long CreateProcess(PCB pnode);
INT32 GetIDByName(char* name);
int sendMessage(long sid,long tid,char* msg,int msglength);
int receiveMessage(long sid, char *msg,int msglength,long *actualLength, long *actualSid);
void schedule_printer();
void QueuePrinter(Queue *queue);
int  ChangePriorByID(long pid, int priority);
INT32  SuspendByID(long ID);
INT32  ResumeByID(long ID);
void initFrame();

int  ChangePriorByID(long pid, int priority){
    PCB tmp;
    if(priority > 99)
	{
		return 0;
	}
    if(pid == -1 || pid == currentPCB->pid)
	{
		currentPCB->prior = priority;
		return 1;
	}
    // check readyqueue
    tmp = readyQueue->front;
    while (tmp!=NULL)
    {
        if(tmp->pid == pid){
            //sort after priority changing
            tmp->prior = priority;
            DeleWithoutFree(readyQueue, tmp);
            EnQueueWithPrior(readyQueue, tmp);
            return 1;
        }
        tmp = tmp->next;
    }
    tmp = timerQueue->front;
    while (tmp!=NULL)
    {
        if(tmp->pid == pid){
            tmp->prior = priority;
         
            return 1;
        }
          tmp = tmp->next;
    }
    tmp = suspendQueue->front;
    while (tmp!=NULL)
    {
        if(tmp->pid == pid){
            tmp->prior = priority;
        
            return 1;
        }
          tmp = tmp->next;
    }
    
    return 0;
}

int sendMessage(long sid,long tid,char* msg,int msglength){
    MSG message;
    if (tid>99) {
        return 0;
    }
    message = (MSG)malloc(sizeof(MSG));
	message->sid = sid;
	message->tid = tid;
	message->length = msglength;
	strcpy(message->msg,msg);
    
    EnQueueMsg(msgQueue,message);
   // printf("i am here=------\n");
    //QueuePrinter(msgQueue);
    //printf("msg is: %s\nsid: %ld\ntid:%ld\n",message->msg,message->sid,message->tid);
    //printf("i am going to resume tid  == %ld-----------",tid);
    ResumeByID(tid);
    
    return 1;
}

int receiveMessage(long sid, char *msg,int msglength,long *actualLength, long *actualSid){
    MSG tmp;
    //CALL(READ_MODIFY(MEMORY_INTERLOCK_BASE+1, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult));
    //currentPCB = readyQueue->front;
    if(sid>99){
        printf("--------------------------------source id is invalid\n");
        return 0;
    }
    memset(msg,'\0',msglength);
    tmp = msgQueue->front;
    while(tmp!=NULL){
        if((sid==-1||tmp->sid==sid)&&((tmp->tid==-1&&tmp->sid!=sid)||tmp->tid==currentPCB->pid)){
            if(tmp->length>msglength){
                printf("------------------------msgbuff is the way too small\n");
                return 0;
            }
            else{
                strcpy(msg, tmp->msg);
               // printf("------------------------------the msg is %s\n",msg);
                *actualLength=(long)tmp->length;
                *actualSid = tmp->sid;
                DeQueueMsg(msgQueue);
                //schedule_printer("recive", currentPCB->pid);
                //QueuePrinter(msgQueue);
                return 1;
            }
        }
        tmp = tmp->next;
    }
    
    if(timerQueue->front!=NULL){
        if (timerQueue->front!=NULL) {
            CALL(Z502Idle());
             EnQueue(readyQueue, currentPCB);
        }
       
        
    }
    else{
       // printf("currentPCB=====%ld\n",currentPCB->pid);
        //SuspendByID(currentPCB->pid);
       // QueuePrinter(readyQueue);
       // QueuePrinter(suspendQueue);
        EnQueue(suspendQueue,currentPCB);
       // QueuePrinter(readyQueue);
        //QueuePrinter(suspendQueue);
        //schedule_printer("check", currentPCB->pid);
    }
    
    
    while (readyQueue->front==NULL) {
        if(timerQueue->front==NULL&&readyQueue->front==NULL){
            Z502Halt();
        }
        CALL(Z502Idle());
        //schedule_printer("Idle", currentPCB->pid);
        
    }
    
    // CALL(QueuePrinter(suspendQueue));
    
    if(readyQueue->front!=NULL){
        currentPCB =  DeQueueWithoutFree(readyQueue);
        //printf("currentPCB=====%ld\n",currentPCB->pid);
    }
    //CALL(READ_MODIFY(MEMORY_INTERLOCK_BASE+1, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult));
   // printf("------------------------i am gere\n");
    Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &(currentPCB->context) );
    
    return receiveMessage(sid, msg, msglength, actualLength, actualSid);
    
    //return 0;
}

void schedule_printer(char* actions,INT32 tarGetID){
    PCB tmp;
    int count = 0;
	long counter = 655350;
    READ_MODIFY(MEMORY_INTERLOCK_BASE + 11, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResultPrinter);
    printf("\n");
    //strncpy(action,"inter",8);
    SP_setup_action(SP_ACTION_MODE, actions);
    if(currentPCB!=NULL){
        SP_setup(SP_RUNNING_MODE, currentPCB->pid);
    }
    else{
        SP_setup(SP_RUNNING_MODE, 99);
    }
    
    SP_setup(SP_TARGET_MODE, tarGetID);
    
    tmp = readyQueue->front;
    while (tmp!=NULL)
    {
        
        count++;
        SP_setup(SP_READY_MODE, (int)tmp->pid);
        tmp = tmp->next;
        if (count >= 10)
        {
            count = 0;
            break;
        }
    }
    tmp = timerQueue->front;
    while (tmp!=NULL) {
        
        count++;
        SP_setup(SP_TIMER_SUSPENDED_MODE, (int)tmp->pid);
        tmp = tmp->next;
        if (count >= 10)
        {
            count = 0;
            break;
        }
    }
    tmp = suspendQueue->front;
    while (tmp!=NULL)
    {
        
        count++;
        SP_setup(SP_PROCESS_SUSPENDED_MODE, (int)tmp->pid);
        tmp = tmp->next;
        if (count >= 10)
        {
            count = 0;
            break;
        }
    }
    SP_print_line();
    memset(action, '\0', 8);
    printf("\n");
    READ_MODIFY(MEMORY_INTERLOCK_BASE + 11, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResultPrinter);
}

void initFrame(){
               
               for (int i = 0; i < (int)PHYS_MEM_PGS; i++)
               {
                   frame[i].frameID = i;
                   frame[i].isAvailable = 1;
                   frame[i].pageID = -1;
               }
}

/************************************************************************
 INTERRUPT_HANDLER
 When the Z502 gets a hardware interrupt, it transfers control to
 this routine in the OS.
 ************************************************************************/
void    interrupt_handler( void ) {
    INT32              device_id;
    INT32              status;
    INT32              Index = 0;
    PCB                 tmp;
    INT32               sleepTime;
    static BOOL        remove_this_in_your_code = TRUE;   /** TEMP **/
    static INT32       how_many_interrupt_entries = 0;    /** TEMP **/
    //printf("i am here----------\n");
    // Get cause of interrupt
    MEM_READ(Z502InterruptDevice, &device_id );
    // Set this device as target of our query
    MEM_WRITE(Z502InterruptDevice, &device_id );
    // Now read the status of this device
    MEM_READ(Z502InterruptStatus, &status );
    printf( "Interrupt handler: Found device ID %d with status %d\n",device_id, status );
    if(device_id == TIMER_INTERRUPT){
     READ_MODIFY(MEMORY_INTERLOCK_BASE,DO_LOCK, SUSPEND_UNTIL_LOCKED, &TimeLockResult);
               CALL(MEM_READ(Z502ClockStatus, &currenttime));
       // printf("--------i am interrputer pointer\n");
        while (timerQueue->front!=NULL) {
            tmp = timerQueue->front;
            
            if(tmp->wakeuptime<=currenttime){
                
                EnQueueWithPrior(readyQueue,DeQueueWithoutFree(timerQueue));
                CALL(schedule_printer("Inter",tmp->pid));
                //timerQueue->front = timerQueue->front->next
            }
            else{
                break;
            }
        }
            if (timerQueue->front!= NULL)
            {
                CALL(MEM_READ(Z502ClockStatus, &currenttime));
                sleepTime = timerQueue->front->wakeuptime - currenttime;
                CALL(MEM_WRITE(Z502TimerStart, &sleepTime));
                //break;
            }

    }
    READ_MODIFY(MEMORY_INTERLOCK_BASE,DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &TimeLockResult);
    
    // Clear out this device - we're done with it
    MEM_WRITE(Z502InterruptClear, &Index );
 
}                                       /* End of interrupt_handler */
/************************************************************************
 FAULT_HANDLER
 The beginning of the OS502.  Used to receive hardware faults.
 ************************************************************************/

void QueuePrinter(Queue *queue){
    CALL(READ_MODIFY(MEMORY_INTERLOCK_BASE+1, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult));
    PCB tmp;
    tmp = queue->front;
    while (tmp!=NULL) {
        
        printf("msg-%ld  ",tmp->pid);
        tmp= tmp->next;
    }
    printf("queue size------->%d\n",queue->size);
    CALL(READ_MODIFY(MEMORY_INTERLOCK_BASE+1, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult));
}

void    fault_handler( void )
{
    INT32       device_id;
    INT32       status;
    INT32       Index = 0;
    // used to determin if the frame empty
    static BOOL  Flag = FALSE;
    int             i;
    
    // Get cause of interrupt
    MEM_READ(Z502InterruptDevice, &device_id );
    // Set this device as target of our query
    MEM_WRITE(Z502InterruptDevice, &device_id );
    // Now read the status of this device
    MEM_READ(Z502InterruptStatus, &status );
    
    printf( "Fault_handler: Found vector type %d with value %d\n",
           device_id, status );
    if(device_id == SOFTWARE_TRAP){//receive 0
		CALL(Z502Halt());
	}
	else if(device_id == CPU_ERROR){//receive 1
		CALL(Z502Halt());
	}
	else if(device_id == INVALID_MEMORY){//receive 2
       
      
        if (status >= VIRTUAL_MEM_PAGES){ //Address is larger than page table,
            
            printf("hihihihihihihihihihihihih\n");
            CALL(Z502Halt());
        }
        
        if (status<0) {
            CALL(Z502Halt());
        }
        if (Z502_PAGE_TBL_ADDR == NULL ){ //Page table doesn't exist,
            
			Z502_PAGE_TBL_LENGTH = 1024;
			Z502_PAGE_TBL_ADDR = (UINT16 *)calloc( sizeof(UINT16), Z502_PAGE_TBL_LENGTH );
            PAGE_HEAD[currentPCB->pid] = Z502_PAGE_TBL_ADDR;
		}
        
        if (!Flag) {
            for (i=0; i<PHYS_MEM_PGS; i++) {
                if(frame[i].isAvailable==1){
                    frame[i].isAvailable=0;
                    frame[i].pageID = status;
					frame[i].pid = currentPCB->pid;
                    Z502_PAGE_TBL_ADDR[status] = (UINT16)frame[i].frameID | 0x8000;
                    printf("-------------------frame[%d] occupied by page %d\n",frame[i].frameID
                           ,frame[i].pageID);
                    break;
                }
                
            }
            if (i==PHYS_MEM_PGS) {
                Flag = TRUE;
            }
           
        }
        
        //mark this page table slot as valid
        
       
        //printf("----------------%d\n",0x8000);
        
        
        if (status >= Z502_PAGE_TBL_LENGTH){//Address is larger than page table,
            
			CALL(Z502Halt());
		}
        
        //CALL(Z502Halt());
        
    
	}
	else if(device_id == INVALID_PHYSICAL_MEMORY){//receive 3
        
		CALL(Z502Halt());
	}
	else if(device_id == PRIVILEGED_INSTRUCTION){//receive 4
		CALL(Z502Halt());
	}
    // Clear out this device - we're done with it
    MEM_WRITE(Z502InterruptClear, &Index );
}                                       /* End of fault_handler */

/************************************************************************
 SVC
 The beginning of the OS502.  Used to receive software interrupts.
 All system calls come to this point in the code and are to be
 handled by the student written code here.
 The variable do_print is designed to print out the data for the
 incoming calls, but does so only for the first ten calls.  This
 allows the user to see what's happening, but doesn't overwhelm
 with the amount of data.
 ************************************************************************/
INT32 start_timer(long *SleepTime){
    
    INT32 Time=0;
    PCB tmp = currentPCB;
    if(SleepTime<0){
        printf("Illegal sleeptime");
        return -1;
    }
    
    //get current absolute time, and set wakeUpTime attribute for currentPCBNode
    INT32 wakeuptime;
    CALL(MEM_READ(Z502ClockStatus, &Time));
    wakeuptime = Time + (INT32)SleepTime;
    
   
    tmp ->wakeuptime = wakeuptime;
    
    CALL(READ_MODIFY(MEMORY_INTERLOCK_BASE+6, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult));
    
    // add current node into timer queue
    CALL(EnQueueWithwakeUpTime(timerQueue, tmp));
  
    
    CALL(MEM_WRITE(Z502TimerStart, &SleepTime));
    
    while (readyQueue->front==NULL) {
            CALL(Z502Idle());
        schedule_printer("Idle", currentPCB->pid);
    }
    
   // CALL(QueuePrinter(suspendQueue));
    
    if(readyQueue->front!=NULL){
        //printf("readyqueue->front %s\nreadyqueue->rear %s\n",
        //  readyQueue->front->name,readyQueue->rear->name);
        currentPCB =  DeQueueWithoutFree(readyQueue);
        tmp = currentPCB;
        //currentPCBnum--;
    }
  //printf("-----------%d",(*(Z502CONTEXT **)(currentPCB->context))->structure_id);
    
    schedule_printer("sleep",currentPCB->pid);
    CALL(READ_MODIFY(MEMORY_INTERLOCK_BASE+6, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult));

    
    
    CALL(Z502SwitchContext(SWITCH_CONTEXT_SAVE_MODE, &(tmp->context)));
    
    return 0;
}

long CreateProcess(PCB pnode){
    
    PCB tmp = (PCB)malloc(sizeof(Node));
    
    if (pnode->prior == ILLEGAL_PRIORITY ) {
        
        return ILLEGAL_PRIORITY;
    }
    
    tmp = readyQueue->front;
    

    while (tmp!=NULL) {
        
        if (strcmp(tmp->name,pnode->name)==0) {
            
            printf("this is %d\n",pnode->prior);
            return DUPLICATED;
        }
       // printf("111111111111\n");
        tmp = tmp->next;
    }
        pnode->pid = (long)currentPCBnum;
    
    EnQueueWithPrior(readyQueue, pnode);
    currentPCBnum++;
    
    return pnode->pid;
}

INT32 GetIDByName(char* name){
    
    if (strcmp(name, "")==0) {
        return (INT32)currentPCB->pid;
    }
    
    PCB tmp = (PCB)malloc(sizeof(Node));
    
    // check readqueue
    tmp = readyQueue->front;
    
    while (tmp!=NULL) {
        if (strcmp(tmp->name,name)==0) {
            printf("\n");
            return (INT32)tmp->pid;
        }
        //printf("11111111111\n");
        tmp = tmp->next;
    }
    
    // check timerqueue
    tmp = timerQueue->front;
    
    while (tmp!=NULL) {
        if (strcmp(tmp->name,name)==0) {
            printf("\n");
            return (INT32)tmp->pid;
        }
        //printf("222222222222\n");
        tmp = tmp->next;
    }
    
    return 11;
}

INT32  SuspendByID(long ID){
    
   // printf("-----------------this ID is %ld\n",ID);
    //CALL(READ_MODIFY(MEMORY_INTERLOCK_BASE+6, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult));
    if(ID == -1)
	{
        printf("----------Dont allow to suspend self\n");
		return 0;
	}
    
   // printf("-------i here iam suspend-----\n");
    PCB p1;
    PCB p2;
    PCB tmp;
    if(ID>100){
        printf("----------Illegal ID\n");
        return 0;
        
    }
    
    tmp = suspendQueue->front;
    while (tmp!=NULL ) {
        
        if(tmp->pid == ID){
            printf("----------its already in suspendqueue\n");
            return 0;
        }
        tmp = tmp->next;
    }
    
    
    if (!IsEmpty(readyQueue)) {
    p1 = readyQueue->front;
        
    while (p1->pid!=ID&&p1->next!=NULL) {
        p2=p1;
        p1 = p1->next;
    }
        
    if(p1->pid==ID){
        EnQueue(suspendQueue, DeleWithoutFree(readyQueue, p1));
        return 1;
    }
    //printf("wo shi suspendqueue--->%s\n-----rear%s",suspendQueue->front->name,suspendQueue->rear->name);
        
    }
    
    
    if (!IsEmpty(timerQueue)) {
    p1 = timerQueue->front;
    while (p1->pid!=ID&&p1->next!=NULL) {
        p2=p1;
        p1 = p1->next;
    }
    if(p1->pid==ID){
        EnQueue(suspendQueue, DeleWithoutFree(timerQueue, p1));
         return 1;
    }
    //printf("wo shi suspendqueue--->%s\n",suspendQueue->front->name);
       
    }
    
    return 0;
}

INT32  ResumeByID(long ID){
    PCB p1;
    PCB p2;
    PCB tmp;
    if(ID>100){
        printf("----------Illegal ID\n");
        return 0;
        
    }
    tmp = suspendQueue->front;
    if(ID==-1&&readyQueue->front==NULL){
        EnQueueWithPrior(readyQueue, DeQueueWithoutFree(suspendQueue));
    }
    
    
    while (tmp!=NULL ) {
        if(tmp->pid == ID){
           // printf("----------Found the ID\n");
            EnQueueWithPrior(readyQueue, DeleWithoutFree(suspendQueue, tmp));
            return 1;
        }
        tmp = tmp->next;
    }
    
    
    
   // printf("Not be able to Found the ID\n");
    return 0;
}



void    svc( SYSTEM_CALL_DATA *SystemCallData ) {
    short               call_type;
    static short        do_print = 10;
    short               i;
    INT32               Time;
    PCB                 pnode;
    long                pid;
    long				diskID;
	long				sectorID;
    int                 count=0;
    PCB                 temp;
    INT32               priority;
    int                 result;
    INT32               messageLength;
   static INT32               messageCounter=0;
    char                message[100];
    INT32               diskStatus;
    
    PCB tmp =(PCB)malloc(sizeof(Node));
    
    call_type = (short)SystemCallData->SystemCallNumber;
    if ( do_print > 0 ) {
        printf( "SVC handler: %s\n", call_names[call_type]);
        for (i = 0; i < SystemCallData->NumberOfArguments - 1; i++ ){
            //Value = (long)*SystemCallData->Argument[i];
            printf( "Arg %d: Contents = (Decimal) %8ld,  (Hex) %8lX\n", i,
                   (unsigned long )SystemCallData->Argument[i],
                   (unsigned long )SystemCallData->Argument[i]);
        }
        do_print--;
    }
    
    switch (call_type) {
        case SYSNUM_GET_TIME_OF_DAY://this vakue is found in syscalls
            CALL(MEM_READ(Z502ClockStatus, &Time));
            *(INT32 *)SystemCallData->Argument[0]= Time;
            
            break;
            
            // terminate system call
            case SYSNUM_TERMINATE_PROCESS:
            
           // printf("this is DATA %ld",SystemCallData->Argument[0]);
             //
            if((INT32)SystemCallData->Argument[0] == -2)
			{
                *(long *)SystemCallData->Argument[1] = ERR_BAD_PARAM;
                schedule_printer("Finish",currentPCB->pid);
               
                Z502Halt();
				
                
			}
            else if((INT32)SystemCallData->Argument[0] == -1)
            {
                READ_MODIFY(MEMORY_INTERLOCK_BASE+3, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
              
                *(long *)SystemCallData->Argument[1] = ERR_SUCCESS;
                
                //temp = currentPCB;
                CALL(TerminateSelf(readyQueue,currentPCB));
                CALL(TerminateSelf(timerQueue,currentPCB));
                
                CALL( schedule_printer("Term",currentPCB->pid));
                while(readyQueue->front==NULL){
                    if(timerQueue->front==NULL){
                        Z502Halt();
                    }
                    CALL(Z502Idle());
                    CALL(schedule_printer("Idle", currentPCB->pid));
                }
                
                if(!IsEmpty(readyQueue)){
                    CALL(currentPCB = DeQueueWithoutFree(readyQueue));
                    temp = currentPCB;
                }
                
                //printf("i am here---------iam sleep1  %ld\n",temp->pid);
                READ_MODIFY(MEMORY_INTERLOCK_BASE+3, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);
                
                
        
               CALL (Z502SwitchContext(SWITCH_CONTEXT_SAVE_MODE, &(temp->context)));
                
            }
            else
            {
                
                tmp = readyQueue->front;
                
                while (tmp!=NULL) {
                    if (tmp->pid == SystemCallData->Argument[0]) {
                        DeQueue(readyQueue);
                        *(long *)SystemCallData->Argument[1] =ERR_SUCCESS;
                        currentPCBnum--;
                    }
                    tmp = tmp->next;
                }
            }
            
            break;
            
            //sleep system call
            case SYSNUM_SLEEP:
            
            
           // printf("======sleep called\n");
            start_timer(SystemCallData->Argument[0]);
      
            break;
            
            case SYSNUM_CREATE_PROCESS:
            
            pnode = (PCB)malloc(sizeof(Node));
            
            InitPCB2(SystemCallData, pnode);
            
            pid = CreateProcess(pnode);
            
            if (pid==ILLEGAL_PRIORITY) {
                printf("ILLEGAL_PRIORITY\n\n");
                *(long *)SystemCallData->Argument[3] = pid;
                *(long *)SystemCallData->Argument[4] = ERR_BAD_PARAM;
            }
            else if(pid==DUPLICATED){
                printf("DUPLICATED NAME\n\n");
                *(long *)SystemCallData->Argument[3] = pid;
                *(long *)SystemCallData->Argument[4] = ERR_BAD_PARAM;
            }
            else {
                printf("------------------\nPCB_%ld pid=%ld enqueued \n\n-------------------\n",readyQueue->rear->pid,readyQueue->rear->pid);
               // printf("11111111111111");
                *(long *)SystemCallData->Argument[3] = pid;
                *(long *)SystemCallData->Argument[4] = ERR_SUCCESS;
            }
            
            if(currentPCBnum > 15)
			{
				*(long *)SystemCallData->Argument[4] = ERR_BAD_PARAM;
			}
            
            schedule_printer("Create", pid);
           
            break;
            
        case SYSNUM_GET_PROCESS_ID:
            pid = GetIDByName((char*)SystemCallData->Argument[0]);
            //printf("==========%ld\n",pid);
            if (pid<10) {
                *(long *)SystemCallData->Argument[1] = pid;

                *(long *)SystemCallData->Argument[2] = ERR_SUCCESS;
                //printf("\n=--------found ID\n");
            }
            else{
                *(long *)SystemCallData->Argument[1] = pid;
                
                *(long *)SystemCallData->Argument[2] = ERR_BAD_PARAM;
                printf("\n can not find %s\n",(char*)SystemCallData->Argument[0]);
            }
            
            
            
            break;
            
        case SYSNUM_SUSPEND_PROCESS:
            printf("suspend called\n");
           schedule_printer("suspend",SystemCallData->Argument[0]);
            pid =(long)SystemCallData->Argument[0];
			//returnStatus = suspend_by_PID(pid);
            
            //SuspendByID(pid);
			if(SuspendByID(pid))
			{
				*(long *)SystemCallData->Argument[1] = ERR_SUCCESS;
			}
			else
			{
				*(long *)SystemCallData->Argument[1] = ERR_BAD_PARAM;
			}
   
			
			
            break;
            
            case SYSNUM_RESUME_PROCESS:
            
            printf("resume called\n");
            schedule_printer("resume",SystemCallData->Argument[0]);
            pid =(long)SystemCallData->Argument[0];
            //ResumeByID(pid);
            if(ResumeByID(pid))
			{
				*(long *)SystemCallData->Argument[1] = ERR_SUCCESS;
                //schedule_printer("resume",SystemCallData->Argument[0]);
			}
			else
			{
				*(long *)SystemCallData->Argument[1] = ERR_BAD_PARAM;
			}

            break;
            
            case SYSNUM_CHANGE_PRIORITY:
            
            //printf("-------------iam here");
            pid = (long)SystemCallData->Argument[0];
            priority = (int)SystemCallData->Argument[1];
            
            result = ChangePriorByID(pid, priority);
            if(result == 1)
			{
				*(long *)SystemCallData->Argument[2] = ERR_SUCCESS;
			}
			else
			{
				*(long *)SystemCallData->Argument[2] = ERR_BAD_PARAM;
			}
            
            schedule_printer("ch_prior",pid);
            break;
            
        case SYSNUM_SEND_MESSAGE:
            messageCounter++;
            pid = (long)SystemCallData->Argument[0];
            messageLength =(INT32)SystemCallData->Argument[2];
            //printf("this is  msgcounter%d\n",messageCounter);
            //sendMessage();
            if (messageCounter>MAX_COUNT) {
                //printf("i am here-----\n");
                messageCounter=0;
                *(long *)SystemCallData->Argument[3] = ERR_BAD_PARAM;
				break;
            }
            if(messageLength>MAX_LENGTH){
                
                *(long *)SystemCallData->Argument[3] = ERR_BAD_PARAM;
				break;
            }
            else{
                
                strcpy(message,(char*)SystemCallData->Argument[1]);
                if(sendMessage(currentPCB->pid,pid,message,messageLength))
				{
					*(long *)SystemCallData->Argument[3] = ERR_SUCCESS;
                    // schedule_printer("send", (INT32)SystemCallData->Argument[0]);
				}
				else
				{
                  
					*(long *)SystemCallData->Argument[3] = ERR_BAD_PARAM;
				}
            }
            
           
            
            
            break;
            
        case SYSNUM_RECEIVE_MESSAGE:
            pid = (long)SystemCallData->Argument[0];
            
            messageLength =(INT32)SystemCallData->Argument[2];
            
            if(messageLength>MAX_LENGTH){
                printf("---------------------beyonged the max length\n");
                *(long *)SystemCallData->Argument[5] = ERR_BAD_PARAM;
            }
            else{
                if(receiveMessage(pid, (char*)SystemCallData->Argument[1], messageLength, SystemCallData->Argument[3], SystemCallData->Argument[4])){
                    *(long *)SystemCallData->Argument[5] = ERR_SUCCESS;
                
                }else{
                    *(long *)SystemCallData->Argument[5] = ERR_BAD_PARAM;
                }
                    
            }
            break;
            
            
        case SYSNUM_DISK_READ:
			
            
			diskID = SystemCallData->Argument[0];
			sectorID = SystemCallData->Argument[1];
            //setup a new disk
            DISK mydisk = (DISK)malloc(sizeof(disk));
            mydisk->diskID = diskID;
            mydisk->sectorID = sectorID;
            //mydisk->readOrWrite = readOrWrite;
            mydisk->alreadyGetDisk = 1;
            mydisk->PCB = currentPCB;
            MEM_WRITE(Z502DiskSetID, &diskID);
            MEM_READ(Z502DiskStatus, &diskStatus);
			
            if(diskStatus==DEVICE_FREE){
            
            }
            else{
                while(diskStatus==DEVICE_IN_USE){
                    EnQueueDisk(diskQueue, mydisk);
                }
            }
            
            
			break;
            
        default:
            printf("Error! call_type not recognized!\n");
            printf("call_type is %i\n",call_type);
            break;
    }
}                                               // End of svc



/************************************************************************
 osInit
 This is the first routine called after the simulation begins.  This
 is equivalent to boot code.  All the initial OS components can be
 defined and initialized here.
 ************************************************************************/

void    osInit( int argc, char *argv[]  ) {
    void                *next_context;
    INT32               i;
    
    initFrame();
    readyQueue = InitQueue();
    timerQueue = InitQueue();
    suspendQueue = InitQueue();
    msgQueue = InitMsgQueue();
    diskQueue = InitDiskQueue();
    /* Demonstrates how calling arguments are passed thru to here       */
    
    printf( "Program called with %d arguments:", argc );
    for ( i = 0; i < argc; i++ )
        printf( " %s", argv[i] );
    printf( "\n" );
    printf( "Calling with argument 'sample' executes the sample program.\n" );
    
    /*          Setup so handlers will come to code in base.c           */
    
    TO_VECTOR[TO_VECTOR_INT_HANDLER_ADDR]   = (void *)interrupt_handler;
    TO_VECTOR[TO_VECTOR_FAULT_HANDLER_ADDR] = (void *)fault_handler;
    TO_VECTOR[TO_VECTOR_TRAP_HANDLER_ADDR]  = (void *)svc;
    
    /*  Determine if the switch was set, and if so go to demo routine.  */
    
    if (( argc > 1 ) && ( strcmp( argv[1], "sample" ) == 0 ) ) {
        Z502MakeContext( &next_context, (void *)sample_code, KERNEL_MODE );
    }
    else if(( argc > 1 ) && ( strcmp( argv[1], "test1a" ) == 0 ) ){
        
            Z502MakeContext( &next_context, (void *)test1a, USER_MODE );
        
    }
    else if(( argc > 1 ) && ( strcmp( argv[1], "test1b" ) == 0 ) ){
        
        Z502MakeContext( &next_context, (void *)test1b, USER_MODE );
        
    }else if(( argc > 1 ) && ( strcmp( argv[1], "test1c" ) == 0 ) ){
        
        Z502MakeContext( &next_context, (void *)test1c, USER_MODE );
        
    }else if(( argc > 1 ) && ( strcmp( argv[1], "test1d" ) == 0 ) ){
        
        Z502MakeContext( &next_context, (void *)test1d, USER_MODE );
        
    }else if(( argc > 1 ) && ( strcmp( argv[1], "test0" ) == 0 ) ){
        
        Z502MakeContext( &next_context, (void *)test0, USER_MODE );
        
    }
    else if(( argc > 1 ) && ( strcmp( argv[1], "test1e" ) == 0 ) ){
        
        Z502MakeContext( &next_context, (void *)test1e, USER_MODE );
        
    }
    else if(( argc > 1 ) && ( strcmp( argv[1], "test1f" ) == 0 ) ){
        
        Z502MakeContext( &next_context, (void *)test1f, USER_MODE );
        
    }else if(( argc > 1 ) && ( strcmp( argv[1], "test1m" ) == 0 ) ){
        
        Z502MakeContext( &next_context, (void *)test1m, USER_MODE );
        
    }
    else if(( argc > 1 ) && ( strcmp( argv[1], "test1g" ) == 0 ) ){
        
        Z502MakeContext( &next_context, (void *)test1g, USER_MODE );
        
    }else if(( argc > 1 ) && ( strcmp( argv[1], "test1h" ) == 0 ) ){
        
        Z502MakeContext( &next_context, (void *)test1h, USER_MODE );
        
    }
    else if(( argc > 1 ) && ( strcmp( argv[1], "test1j" ) == 0 ) ){
        
        Z502MakeContext( &next_context, (void *)test1j, USER_MODE );
        
    }else if(( argc > 1 ) && ( strcmp( argv[1], "test1i" ) == 0 ) ){
        
        Z502MakeContext( &next_context, (void *)test1i, USER_MODE );
        
    }else if(( argc > 1 ) && ( strcmp( argv[1], "test1l" ) == 0 ) ){
        
        Z502MakeContext( &next_context, (void *)test1l, USER_MODE );
        
    }
    else if(( argc > 1 ) && ( strcmp( argv[1], "test1k" ) == 0 ) ){
        
        Z502MakeContext( &next_context, (void *)test1k, USER_MODE );
        
    } else if(( argc > 1 ) && ( strcmp( argv[1], "test2a" ) == 0 ) ){
        
        Z502MakeContext( &next_context, (void *)test2a, USER_MODE );
        
    }
    else if(( argc > 1 ) && ( strcmp( argv[1], "test2b" ) == 0 ) ){
        
        Z502MakeContext( &next_context, (void *)test2b, USER_MODE );
        
    }
    else if(( argc > 1 ) && ( strcmp( argv[1], "test2c" ) == 0 ) ){
        
        Z502MakeContext( &next_context, (void *)test2c, USER_MODE );
        
    }
    else if(( argc > 1 ) && ( strcmp( argv[1], "test2g" ) == 0 ) ){
        
        Z502MakeContext( &next_context, (void *)test2g, USER_MODE );
        
    }
    //Init the first PCB
    //PCB temp =(PCB)malloc(sizeof(Node));
    currentPCB = (PCB)malloc(sizeof(Node));
    startPCB = (PCB)malloc(sizeof(Node));
    startPCB-> pid = 0;
    startPCB->context = next_context;
    startPCB->prior = 1;
    startPCB->next = NULL;
    strcpy(startPCB->name, "StartPCD");
    currentPCB = startPCB;
    
    
    /* This routine should never return!!           */
    
    /*  This should be done by a "os_make_process" routine, so that
     test0 runs on a process recognized by the operating system.    */
    Z502SwitchContext( SWITCH_CONTEXT_KILL_MODE, &(currentPCB->context) );
}                                               // End of osInit