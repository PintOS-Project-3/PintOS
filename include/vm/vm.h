#ifndef VM_VM_H
#define VM_VM_H
#include <stdbool.h>
#include "threads/palloc.h"
#include <hash.h>

enum vm_type
{
	/* page not initialized */
	VM_UNINIT = 0,
	/* page not related to the file, aka anonymous page */
	VM_ANON = 1,
	/* page that realated to the file */
	VM_FILE = 2,
	/* page that hold the page cache, for project 4 */
	VM_PAGE_CACHE = 3,

	/* Bit flags to store state */

	/* Auxillary bit flag marker for store information. You can add more
	 * markers, until the value is fit in the int. */
	VM_MARKER_0 = (1 << 3),
	VM_MARKER_1 = (1 << 4),

	/* DO NOT EXCEED THIS VALUE. */
	VM_MARKER_END = (1 << 31),
};

#include "vm/uninit.h"
#include "vm/anon.h"
#include "vm/file.h"
#ifdef EFILESYS
#include "filesys/page_cache.h"
#endif

struct page_operations;
struct thread;

#define VM_TYPE(type) ((type) & 7)

/* The representation of "page".
 * This is kind of "parent class", which has four "child class"es, which are
 * uninit_page, file_page, anon_page, and page cache (project4).
 * DO NOT REMOVE/MODIFY PREDEFINED MEMBER OF THIS STRUCTURE. */
struct page
{
	/* 해시 테이블 사용하기 위한 멤버 정의 */
	struct hash_elem hash_elem; /* Hash Table element */
	// void *addr;									/* Virtual address */
	

	const struct page_operations *operations;
	void *va;						 /* Address in terms of user space */
	struct frame *frame; /* Back reference for frame */

	/* Your implementation */

	/* Per-type data are binded into the union.
	 * Each function automatically detects the current union */
	union
	{
		struct uninit_page uninit;
		struct anon_page anon;
		struct file_page file;
#ifdef EFILESYS
		struct page_cache page_cache;
#endif
	};
};

/* The representation of "frame" */
struct frame
{
	/* 프레임이 어떤 커널 가상 주소(Kernel Virtual Address, KVA)와 매핑되는지를 나타냄
	 * 핀토스에서는 kva = pa (깃북에 있다), 연속적 & 일대일 대응*/
	void *kva;
	struct page *page; /* 이 프레임과 매핑된 페이지 */

	/* 프레임 관리를 위한 멤버 추가하기 */
	struct hash_elem hash_elem; // 해시 테이블 사용해보자
};

/* The function table for page operations.
 * This is one way of implementing "interface" in C.
 * Put the table of "method" into the struct's member, and
 * call it whenever you needed.
 * 이 구조체는 "인터페이스"를 구현하는 한 방법으로,
 * C 언어에서 객체 지향 프로그래밍의 일부 개념을 모방합니다. */
struct page_operations
{
	bool (*swap_in)(struct page *, void *);
	bool (*swap_out)(struct page *);
	void (*destroy)(struct page *);
	enum vm_type type;
};

/* 저장 매체에 저장된 페이지를 메모리로 다시 로드
 * 성공 여부 불리언 값으로 반환 */
#define swap_in(page, v) (page)->operations->swap_in((page), v)

#define swap_out(page) (page)->operations->swap_out(page)
#define destroy(page)              \
	if ((page)->operations->destroy) \
	(page)->operations->destroy(page)

/* Representation of current process's memory space.
 * We don't want to force you to obey any specific design for this struct.
 * All designs up to you for this. */
struct supplemental_page_table
{
	struct hash spt_hash;
};

#include "threads/thread.h"
void supplemental_page_table_init(struct supplemental_page_table *spt); // 구현
bool supplemental_page_table_copy(struct supplemental_page_table *dst,
																	struct supplemental_page_table *src);
void supplemental_page_table_kill(struct supplemental_page_table *spt);
struct page *spt_find_page(struct supplemental_page_table *spt, void *va);		// 구현
bool spt_insert_page(struct supplemental_page_table *spt, struct page *page); // 구현
void spt_remove_page(struct supplemental_page_table *spt, struct page *page);

void vm_init(void);
bool vm_try_handle_fault(struct intr_frame *f, void *addr, bool user,
												 bool write, bool not_present);

#define vm_alloc_page(type, upage, writable) \
	vm_alloc_page_with_initializer((type), (upage), (writable), NULL, NULL)
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage,
																		bool writable, vm_initializer *init, void *aux);
void vm_dealloc_page(struct page *page);
bool vm_claim_page(void *va);
enum vm_type page_get_type(struct page *page);

// hash table 위한 전방 선언
unsigned page_hash(const struct hash_elem *p_, void *aux UNUSED);
bool page_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);

#endif /* VM_VM_H */
