#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "intrinsic.h"
#include "userprog/syscall.h"

#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

void hex_dump (uintptr_t ofs, const void *buf_, size_t size, bool ascii);
/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	void **save_ptr;
	strtok_r(file_name," ",save_ptr);
	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_) {
	/* Clone current thread to new thread.*/
	memcpy(&thread_current()->parent_if,if_,sizeof(struct intr_frame)); // 이 코드~!
	return thread_create (name,PRI_DEFAULT, __do_fork, thread_current ());
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if(is_kern_pte(pte)) { // 커널일 때 어떻게 해야할지..?
		// newpage = palloc_get_page(PAL_USER);
		// parent_page = pml4_get_page (parent->pml4, va);
		// memcpy(newpage, parent_page, PGSIZE);
		return true; 
	}	
	va = pg_round_down(va); // va에 해당하는 페이지의 시작 주소로 슈웃 
	
	//if(!is_user_vaddr(va)) return true;
	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);
	if (!parent_page) return false;
	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page(PAL_USER);
	if (!newpage) return false;
	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy(newpage, parent_page, PGSIZE);
	writable = (*pte &  PTE_W) != 0; // 부모 페이지가 쓰기 가능한지 확인 ?
	// 0 이 아니면 True 니까

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */	
		palloc_free_page(newpage);
		return false;
	}

	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux; // 부모
	struct thread *current = thread_current (); // 자식
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if;
	bool succ = true;
	parent_if = &parent->parent_if;
	/* 1. Read the cpu context to local stack. */
	memcpy (&if_,parent_if, sizeof (struct intr_frame)); // tf 
	// memcpy(&current->tf,parent_if,sizeof(struct intr_frame));
	// current->tf = if_; //쨘
	
	// current->tf.R.rax = 0;
	
	// current->parent = parent; // 부모를 저장
	// list_push_back(&parent->child_list,&current->c_elem); //부모의 자식리스트에 현재 스레드를 저장
	
	// printf("parent: %d, child:%d, list_head : %d\n\n",parent->tid, current->tid,list_entry(list_front(&parent->child_list),struct thread,c_elem)->tid);
	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent)){
		goto error;
	}
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/
	for( int i = MIN_FD; i <= MAX_FD; i ++){ 
		if(parent->fdt[i] != NULL){
			struct file *dup_file = file_duplicate(parent->fdt[i]);
			current->fdt[i] = dup_file;
		}
	}

	// 모든 fdt를 전부 복사해야 할듯?
	process_init ();

	sema_up(&current->fork_sema);
	/* Finally, switch to the newly created process. */
	if (succ){
		if_.R.rax = 0; // 자식은 0 이어야 함 
		do_iret (&if_); // t
	}
error:
	current->exit_status = -1;
	sema_up(&current->fork_sema);
	sys_exit(-1);
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
void
argument_stack(char **argv,int cnt,struct intr_frame* _if){
	int i = 0;
	// 1. argv 값 뒤에서부터 푸쉬
	// 2. padding
	// 3. sentinel
	// 4. argv[cnt] ... argv[0] 주소 뒤에서부터 푸쉬
	// 5. argv = argv[0] 주소를 저장한 곳의 주소 푸쉬
	// 6. argc 푸쉬
	// 7. return address(fake address) : 0 푸쉬
	int *argv_addr[128];
	for(int i = cnt - 1;i >= 0;i--){
		//memcpy (void *dst_, const void *src_, size_t size)
		_if->rsp -= strlen(argv[i]) + 1;
		argv_addr[i] = _if->rsp;
		memcpy(_if->rsp, argv[i],strlen(argv[i]) + 1); // 마지막 널문자
	}
	
	while((_if->rsp % 8)){ // 패딩 -> 8 단위
		_if->rsp -= 1;
		memset(_if->rsp,0,1);
	}
	// memset

	_if->rsp -= 8;
	// memset(_if->rsp,0,8); // sentinel

	for(int i = cnt -1;i>=0;i--){
		_if->rsp -= 8;
		
		memcpy(_if->rsp,&argv_addr[i],8); // why &?
	}

	// _if->rsp -= 8;
	// memcpy(_if->rsp,argv,8);
	_if->R.rsi = _if->rsp; // 공부 
	
	// _if->rsp -= 4;
	// memcpy(_if->rsp,cnt,4);
	_if->R.rdi = cnt;
	
	// fake address
	_if->rsp -= 8;
	memset(_if->rsp,0,8);
}

int
process_exec (void *f_name) {
	/* f_name 파싱 -> 인자로 넘겨줘야 함.. */
	
	// char *save_ptr;
	// // char *token = strtok_r(f_name, " ", &save_ptr); // token = 'echo' 
	// char *argv[128];

	// int cnt = 0; 
	// while (token != NULL) {
	// 	argv[cnt] = token;
	// 	token = strtok_r(NULL, " ", &save_ptr); // token = 'x'
	// 	cnt++;
	// }
	//printf("\n\n%s\n\n",argv[0]);

	// int argc = cnt; // 인자의 개수

	//char *file_name = f_name;

	bool success;
	
	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG; 
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;
	
	/* We first kill the current context */
	process_cleanup ();
	char *file_name = f_name;
	success = load (file_name, &_if);

	/* And then load the binary */
	
	// hex_dump(); // ?
	//hex_dump(_if.rsp, _if.rsp,(USER_STACK - _if.rsp), true);

	//strlcpy(thread_current()->p_name,argv[0],sizeof(thread_current()->p_name) + 1);
	// printf("cur thread process name : %s\n",thread_current()->p_name);
	// printf("argv[0] = %s\n\n",argv[0]);
	/* If load failed, quit. */
	palloc_free_page (file_name); 
	if (!success)
		return -1;

	/* Start switched process. */
	do_iret (&_if);
	NOT_REACHED ();
}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
struct thread*
get_child_process2(tid_t pid){
	struct thread * curr = thread_current();
	struct list_elem *e;
	struct thread * child;
	
	//printf("get_child_process: list head : %d\n\n",list_entry(list_front(&curr->child_list),struct thread,c_elem)->tid);
	
	for (e = list_begin (&curr->child_list); e != list_end (&curr->child_list); e = list_next (e)){
		child = list_entry(e,struct thread,c_elem);
		if(child->tid == pid){ 
			return child;
		}
	}
	return NULL; // not exist!
}

// struct thread*
// get_killed_child(int child_tid){
// 	struct thread * curr = thread_current();
// 	struct list_elem *e;
// 	struct thread * child;
	
// 	//printf("get_child_process: list head : %d\n\n",list_entry(list_front(&curr->child_list),struct thread,c_elem)->tid);
	
// 	for (e = list_begin (&curr->killed_list); e != list_end (&curr->killed_list); e = list_next (e)){
// 		child = list_entry(e,struct thread,k_elem);
// 		if(child->tid == child_tid){ 
// 			return child;
// 		}
// 	}
// 	return NULL; // not exist!
// }

int
process_wait (tid_t child_tid UNUSED) {
	
	struct thread * cur = thread_current();
	// printf("get before\n\n");
	cur->waiting_child = child_tid;
	struct thread * target = get_child_process2(child_tid);
	if(!target) {
		//printf("no target!\n");
		return -1;
	}
	// kill_list -> 이미 wait이 호출되었으면서 아직 exit안한 애들 -> 호출이 또되면 -1

	// if (get_killed_child(child_tid)){
	// 	return -1;
	// }
	//list_push_back(&cur->killed_list,&target->k_elem);

	sema_down(&target->wait_sema);
	
	int exit_s = target->exit_status;
	
	list_remove(&target->c_elem);
	// list_remove(&target->k_elem);
	target->parent = NULL;

	// sema_up(&target->exit_sema);
	
	return exit_s; 
	//return thread_current()->exit_status;// child의 exit_status를 리턴하도록 하기 
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process term	ination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */
	// if(curr->parent && curr->parent->waiting_child == curr ->tid){
		// list_push_back(&ready_list,&thread_current()->parent->elem);
		// thread_yield();
		// curr->parent->exit_status = curr->exit_status;
	// curr->parent->child_exit_status = curr->exit_status;

	for (int i = MIN_FD; i <= MAX_FD; i++)
    	sys_close(i);

	if(curr->loaded_file) file_close(curr->loaded_file);

	struct exit_info *my_info;
    my_info = (struct exit_info *)malloc(sizeof(struct exit_info));
    my_info->pid = curr->tid;
	my_info->exit_status = curr->exit_status;
	list_push_back(&curr->parent->exit_child_list,&my_info->p_elem);
	
	
	//list_remove(&curr->c_elem);
	// for (int i = 2; i < 63; i++)
	// 	sys_close(i);
	// if(curr->loaded_file) {
	// 	printf("loaded file released! pid == %d\n ",curr->tid);
	// 	file_close(curr->loaded_file);
	// }
	
	sema_up(&curr->wait_sema);
	// sema_down(&curr->exit_sema);
	while(!list_empty(&curr->exit_child_list)){
		//printf("cur: %d , freed pid : %d \n",curr->tid,list_entry(list_back(&curr->exit_child_list),struct exit_info,p_elem)->pid);
		free(list_entry(list_pop_back(&curr->exit_child_list),struct exit_info,p_elem));
	}
	process_cleanup ();

}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/*let's parsing*/
	static char *argv[LOADER_ARGS_LEN / 2 + 1];
	char *token, *save_ptr;
	int argc = 0;

	//char *strtok_r(char *s, const char *delimiters, char **saveptr);
	for (token = strtok_r (file_name, " ", &save_ptr); token != NULL; token = strtok_r (NULL, " ", &save_ptr)){
		argv[argc] = token;
		argc++;
	}
	if( t->loaded_file != NULL){
		file_allow_write(t->loaded_file);
		t->loaded_file = NULL;
	}

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	//lock_acquire(&sysfile_lock);
	/* Open executable file. */
	file = filesys_open (file_name);
	if (file == NULL) {
		//lock_release(&sysfile_lock);
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	file_deny_write(file);
	t->loaded_file = file;
	//lock_release(&sysfile_lock);

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	//void argument_stack(char **parse ,int count ,struct intr_frame *if) struct intr_frame로 변경
	argument_stack(argv, argc, if_);

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	//file_close (file);
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	return success;
}
#endif /* VM */
