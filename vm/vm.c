/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include <hash.h> // 해시 테이블로 spt 구현
#include "threads/mmu.h" // page

/* project 3 : Swap in&out */
struct list frame_table;
struct list_elem *start;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
/* 처음 가상 메모리를 초기화 할 때 frame table을 여기서 함께 초기화 해줘야 함 */
void 
vm_init(void)
{
	vm_anon_init();
	vm_file_init();
#ifdef EFILESYS /* For project 4 */
	pagecache_init();
#endif
	register_inspect_intr();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_table); // 추가
	start = list_begin(&frame_table); // 추가
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type(struct page *page)
{
	int ty = VM_TYPE(page->operations->type);
	switch (ty)
	{
	case VM_UNINIT:
		return VM_TYPE(page->uninit.type);
	default:
		return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/* 함수의 목적: 메모리 할당의 준비 단계(커널이 새 page request를 받으면 호출)
 * 1. 페이지 할당과 초기화: 이 함수는 새로운 페이지를 할당하고, 
 * 		페이지 타입에 따라 적절한 초기화 함수를 설정합니다. 
 * 		이는 페이지를 물리 메모리에 매핑하기 전에 필요한 사전 준비 단계를 수행합니다.
 * 2. Lazy Loading 준비: 함수는 페이지에 대한 구조체를 생성하고 초기화하지만, 
 * 		실제 물리 메모리 할당은 지연시킵니다. 
 * 		이는 페이지가 실제로 필요할 때까지 물리 메모리 할당을 미루는 것을 의미합니다.
 * 3. 페이지 타입에 따른 처리: 다양한 페이지 타입(VM_ANON, VM_FILE 등)에 대한 처리를 준비합니다. 
 * 		각 타입에 따라 페이지를 다르게 초기화하거나 관리할 수 있습니다. */
/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. 
/* 함수 동작 과정
 * 1. 새로운 page를 할당하고
 * 2. 각 페이지 타입에 맞는 initializer를 셋팅하고
 * 3. 유저 프로그램에게 다시 control을 넘긴다. */
bool 
vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
																		vm_initializer *init, void *aux)
{

	ASSERT(VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current()->spt;

	/* Check wheter the upage is already occupied or not.
	 * upage가 이미 사용 중인지 확인
	 * 해당 페이지가 없어야 초기화를 해준다. */
	if (spt_find_page(spt, upage) == NULL)
	{
		/* TODO: Create the page, fetch the initializer according to the VM type, -> 페이지를 생성하고, VM 타입에 따라 initialier를 가져옵니다.
		 * TODO: and then create "uninit" page struct by calling uninit_new. -> 그런 다음 uninit_new를 호출하여 "uninit" 페이지 구조체를 생성합니다
		 * TODO: You should modify the field after calling the uninit_new. -> uninit_new를 호출한 후 필드를 수정해야 합니다. */

		/* TODO: Insert the page into the spt. -> 페이지를 spt에 삽입*/
		struct page *page = (struct page *)malloc(sizeof(struct page));

		switch(VM_TYPE(type))
		{
			case VM_ANON:
				/* Fetch first, page_initialize may overwrite the values 
				 * uninit_new(): 내 인자를 통해 정보를 새로 만들 페이지에 넣어준다 */
				uninit_new(page, pg_round_down(upage), init, type, aux, anon_initializer);
				break;
			case VM_FILE:	
				uninit_new(page, pg_round_down(upage), init, type, aux, file_backed_initializer);
			break;
		}
		page->writable = writable;

		return spt_insert_page(spt, page);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL.
 * 인자로 넘겨진 보조 페이지 테이블에서로부터
 * 가상 주소(va)와 대응되는 페이지 구조체를 찾아서 반환합니다. 
 * 실패했을 경우 NULL를 반환합니다.
 * 
 * spt 설계 후 구현해야할 함수 2 */
struct page *
spt_find_page(struct supplemental_page_table *spt, void *va)
{
	/* TODO: Fill this function. */
	struct page *page = page_lookup(&spt->spt_hash, va); // pg_round_down?

	if (page)
		return page;

	return NULL;
}

/* Insert PAGE into spt with validation. 
 * 인자로 주어진 보조 페이지 테이블에 페이지 구조체를 삽입합니다.
 * 이 함수에서 주어진 보충 테이블(spt)에서 가상 주소가 존재하지 않는지 검사해야 합니다.
 * (같은 주소값을 가지는 페이지가 해시 테이블에 있는지 확인하기)
 * 선빈과 다름
 * 
 * spt 설계 후 구현해야할 함수 3 */
bool 
spt_insert_page(struct supplemental_page_table *spt,
										 struct page *page)
{
	int success = true;
	/* TODO: Fill this function. */
	// spt에 struct page 삽입
	if (hash_insert(&spt->spt_hash, &page->hash_elem) == NULL)
		return success = false;	

	return success;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page)
{
	vm_dealloc_page(page);
	return true;
}

/* Get the struct frame, that will be evicted.
 * 쫓아낼 페이지(frame) 찾기: LRU 알고리즘, 선형 탐색 
 * access bit가 false인 pte 찾기 */
static struct frame *
vm_get_victim(void)
{
	struct frame *victim = NULL;
	/* TODO: The policy for eviction is up to you. */
	struct thread *curr = thread_current();
	struct list_elem *e, *start;

	for (start = e; start != list_end(&frame_table); start = list_next(start))
	{
		victim = list_entry(start, struct frame, frame_elem);
		if (pml4_is_accessed(curr->pml4, victim->page->va))
			pml4_set_accessed(curr->pml4, victim->page->va, false);
		else
			return victim;
	}

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.
 * page에 연결된 frame 안의 데이터를 디스크로 내린다. */
static struct frame *
vm_evict_frame(void)
{
	struct frame *victim = vm_get_victim();  // 비우고자 하는 프레임: victim // 실제 제거 일어남
	/* TODO: swap out the victim and return the evicted frame. */
	swap_out(victim->page); // 추가

	return NULL;
}

/* palloc()을 호출하여 프레임을 얻고, 사용 가능한 페이지가 없는 경우 페이지를 교체하고
 * 반환합니다. 이 함수는 항상 유효한 주소를 반환합니다. 즉, 사용자 풀 메모리가 가득 찬 경우,
 * 이 함수는 프레임을 교체하여 사용 가능한 메모리 공간을 얻습니다.(스왑?) */
/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
/* palloc_get_page를 호출하여 사용자 풀로부터 새로운 물리 페이지를 가져옵니다.
 * 사용자 풀에서 페이지를 성공적으로 가져온 경우, 프레임을 할당하고 그 멤버를 초기화한 후 반환합니다.
 * vm_get_frame을 구현한 후에는 모든 사용자 공간 페이지(PALLOC_USER)를 이 함수를 통해 할당해야 합니다.
 * 페이지 할당 실패 시 교체(swap out) 처리는 지금 당장은 필요 없습니다.
 * 현재로서는 그러한 경우에 'PANIC("todo")'로 표시하세요. */
static struct frame *
vm_get_frame(void)
{
	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	frame = (struct frame *)malloc(sizeof(struct frame));

	// user pool에서 page 하나 할당(함수 안에 mutex 락 존재)
	struct page *kva = palloc_get_page(PAL_USER);

	frame->kva = kva;

	/* if 프레임이 꽉 차서 할당받을 수 없다면 페이지 교체 실시
	   else 성공했다면 frame 구조체 커널 주소 멤버에 위에서 할당받은 메모리 커널 주소 넣기 */
	if (frame->kva == NULL)
	{
		frame = vm_evict_frame(); // frame에서 공간 내리고 새로 할당받아온다.
		frame->page = NULL;

		return frame;
	}

	/* 새 프레임을 프레임 테이블에 넣어 관리한다. */
	list_push_back(&frame_table, &frame->frame_elem);

	frame->page = NULL;

	ASSERT(frame != NULL);
	ASSERT(frame->page == NULL);

	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth(void *addr UNUSED)
{
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp(struct page *page UNUSED)
{
}

/* Return true on success */
bool 
vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
												 bool user UNUSED, bool write UNUSED, bool not_present UNUSED)
{
	struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* Validate the fault */
	if (!addr || is_kernel_vaddr(addr) || !not_present)
		return false;

	/* TODO: Your code goes here */
	page = spt_find_page(spt, addr);

	if (!page)
		return false;

	// stack growth 구현과 그 이후를 위한 코드
	// if (!page)
	// {
	// 	if (addr >= USER_STACK - (1 << 20) && USER_STACK > addr && addr >= f->rsp - 8 && addr < thread_current()->stack_bottom)
	// 	{
	// 		void *fpage = thread_current()->stack_bottom - PGSIZE;
	// 		if (vm_stack_growth(fpage))
	// 		{
	// 			page = spt_find_page(spt, fpage);
	// 		}
	// 		else
	// 		{
	// 			return false;
	// 		}
	// 	}
	// 	else
	// 	{
	// 		return false;
	// 	}
	// }

	return vm_do_claim_page(page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void 
vm_dealloc_page(struct page *page)
{
	destroy(page);
	free(page);
}

/* 클레임은 물리 프레임을 페이지에 할당해주는 것을 의미,
 * 1. vm_get_frame() 함수를 호출해 frame을 받아온다. 
 * 2. 이후, 인자로 받은 va를 이용해, 
 * 		supplemental page table에서 frame과 연결해주고자 하는 페이지를 찾는다.
 * Claim the page that allocate on VA. */
bool 
vm_claim_page(void *va)
{
	struct page *page = NULL;
	/* TODO: Fill this function */
	// 우선 한 페이지 얻기(pt에서 빈 페이지 찾기?)
	page = spt_find_page(&thread_current()->spt, va);

	if (page == NULL)
		return false; // 추가

	return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page(struct page *page)
{
	if (!page || !is_user_vaddr(page->va)) // 페이지의 주소가 커널 va 일 수 있나?
		return false;

	struct frame *frame = vm_get_frame(); // 물리 frame 할당 -> page와 연결 X

	/* Set links, 할당된 frame과 page 연결 */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	/* 페이지의 VA를 프레임의 PA에 매핑하기 위해 PTE insert */
	// struct supplemental_page_table *spt = &thread_current()->spt;
	if (!pml4_set_page(&thread_current()->pml4, page->va, frame->kva, page->writable)) // 추가
		return false;

	return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table
 * 보조 페이지 테이블를 초기화합니다. 
 * 보조 페이지 테이블를 어떤 자료 구조로 구현할지 선택하세요. 
 * userprog/process.c의 initd 함수로 새로운 프로세스가 시작하거나,
 * process.c의 __do_fork로 자식 프로세스가 생성될 때 호출됩니다.
 * 
 * spt 설계 후 구현해야할 함수 1 */
void 
supplemental_page_table_init(struct supplemental_page_table *spt)
{
	hash_init(&spt->spt_hash, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool 
supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
																	struct supplemental_page_table *src UNUSED)
{

}

/* Free the resource hold by the supplemental page table */
void 
supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED)
{
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}
