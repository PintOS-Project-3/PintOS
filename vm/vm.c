/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include <hash.h> // 해시 테이블로 spt 구현

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
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

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool 
vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
																		vm_initializer *init, void *aux)
{

	ASSERT(VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page(spt, upage) == NULL)
	{
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		/* TODO: Insert the page into the spt. */
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
	struct page *page = page_lookup(&spt->spt_hash, va);

	return page;
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

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim(void)
{
	struct frame *victim = NULL;
	/* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame(void)
{
	struct frame *victim UNUSED = vm_get_victim();
	/* TODO: swap out the victim and return the evicted frame. */

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
	// 사용자 풀에서 page 가져오기,(함수 안에 mutex 락 존재)
	struct page *get_page = palloc_get_page(PAL_USER);

	if (get_page)
	{
		frame = malloc(sizeof(struct frame));
		if (frame == NULL)
			PANIC("todo: malloc failed.");

		// frame 구조체 초기화하기
		frame->kva = NULL;
		frame->page = get_page;

		// spt에 새로 할당한 get_page 추가
		struct supplemental_page_table *spt = &thread_current()->spt;
		bool insert_success = spt_insert_page(spt, get_page); // 맞을까? 예외처리 how?
		if (!insert_success)
		{
			free(frame);
			PANIC("todo: spt_insert_page failed.");
		}

		return frame;
	}
	else // get_page == NULL 경우
	{
		// 페이지 할당 실패 시 스왑 처리 로직 추가 해야함
		PANIC("todo: palloc failed.");

		ASSERT(frame != NULL);
		ASSERT(frame->page == NULL);
	
		return frame;
	}
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
	/* TODO: Your code goes here */

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

/* Claim the page that allocate on VA. */
bool 
vm_claim_page(void *va)
{
	// struct page *page = NULL;
	/* TODO: Fill this function */
	// 우선 한 페이지 얻기(pt에서 빈 페이지 찾기?)
	struct page *page = page_lookup(&thread_current()->spt.spt_hash, va);
	if (page == NULL)
		PANIC("vm_claim_page() failed.");

	return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page(struct page *page)
{
	struct frame *frame = vm_get_frame();

	/* Set links */
	// 매핑이 이게 끝인가??
	frame->page = page; 
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	struct supplemental_page_table *spt = &thread_current()->spt;
	if (spt_insert_page(spt, page) == false)
		PANIC("spt_insert_page() failed.");

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
