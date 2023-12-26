/* uninit.c: Implementation of uninitialized page.
 *
 * All of the pages are born as uninit page. When the first page fault occurs,
 * the handler chain calls uninit_initialize (page->operations.swap_in).
 * The uninit_initialize function transmutes the page into the specific page
 * object (anon, file, page_cache), by initializing the page object,and calls
 * initialization callback that passed from vm_alloc_page_with_initializer
 * function.
 * */
/* uninit.c: 초기화되지 않은 페이지의 구현.
 *
 * 모든 페이지는 초기화되지 않은(uninit) 페이지로 생성됩니다. 첫 번째 페이지 폴트가 발생하면,
 * 핸들러 체인이 uninit_initialize (page->operations.swap_in)를 호출합니다.
 * uninit_initialize 함수는 특정 페이지 객체(익명, 파일, 페이지 캐시)로 페이지를 변환시키며,
 * 페이지 객체를 초기화하고 vm_alloc_page_with_initializer 함수에서 전달된
 * 초기화 콜백을 호출합니다.
 * */

#include "vm/vm.h"
#include "vm/uninit.h"

static bool uninit_initialize(struct page *page, void *kva);
static void uninit_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations uninit_ops = {
		.swap_in = uninit_initialize,
		.swap_out = NULL,
		.destroy = uninit_destroy,
		.type = VM_UNINIT,
};
/* 페이지를 초기화하기 전의 설정 단계를 나타내며, 
 * 페이지 객체를 만들고 이후에 필요할 때 사용할 수 있도록 준비하는 역할을 합니다
 * uninit_new 함수는 초기화되지 않은 새 페이지 객체를 구성합니다.
 * 이 함수는 가상 주소(va)와 페이지 타입(type)에 대한 정보를 받아,
 * struct page 타입의 객체에 이 정보들을 설정합니다. */
/* DO NOT MODIFY this function */
void uninit_new(struct page *page, void *va, vm_initializer *init,
								enum vm_type type, void *aux,
								bool (*initializer)(struct page *, enum vm_type, void *))
{
	ASSERT(page != NULL); // 전달된 'page' 포인터가 NULL이 아닌지 확인합니다.

	*page = (struct page){
			.operations = &uninit_ops, // uninit_ops(초기화되지 않은 페이지의 연산)를 가리키도록 설정합니다.
			.va = va,
			.frame = NULL, /* no frame for now */
			.uninit = (struct uninit_page){
					.init = init,										 // 초기화 함수.
					.type = type,										 // 페이지 타입.
					.aux = aux,											 // 추가 데이터.
					.page_initializer = initializer, // 실제 페이지를 초기화할 때 호출될 함수.
			}};
}

/* uninit_initialize 함수는 페이지가 처음 접근되었을 때 페이지를 초기화하는 역할을 합니다. 
 * 이 함수는 초기화되지 않은 페이지 구조체의 포인터와 커널 가상 주소를 인자로 받아서,
 * 해당 페이지를 사용할 준비가 되도록 초기화합니다. */
/* 프로세스가 처음 만들어진(UNINIT)페이지에 처음으로 접근할 때 page fault가 발생한다. 
 * 그러면 page fault handler는 해당 페이지를 디스크에서 프레임으로 swap-in하는데, 
 * UNINIT type일 때의 swap_in 함수가 바로 이 함수이다. 
 * 즉, UNINIT 페이지 멤버를 초기화해줌으로써 
 * 페이지 타입을 인자로 주어진 타입(ANON, FILE, PAGE_CACHE)로 변환시켜준다. 
 * 여기서 만약 segment도 load되지 않은 상태라면 lazy load segment도 진행한다. */
/* Initalize the page on first fault */
static bool
uninit_initialize(struct page *page, void *kva)
{
	struct uninit_page *uninit = &page->uninit; // 'uninit' 구조체를 page 객체로부터 가져옵니다.

	/* Fetch first, page_initialize may overwrite the values
	 * 초기화 함수와 추가 데이터를 먼저 추출합니다.
	 * (page_initializer 함수가 이 값을 덮어쓸 수 있기 때문에 추출해야 합니다.) */
	vm_initializer *init = uninit->init;
	void *aux = uninit->aux;

	/* TODO: You may need to fix this function.
	 * uninit->page_initializer를 호출하여 페이지 타입에 맞게 페이지를 초기화합니다
	 * 이 함수는 페이지가 특정 타입으로 초기화되어야 할 때 호출된다.
	 * 이후, init이 제공되었다면 (즉, NULL이 아니라면), init 함수를 호출하여
   *    추가적인 사용자 정의 초기화를 수행합니다.
	 * 모든 초기화 단계가 성공적으로 완료되면 true를, 아니면 false를 반환 */
	return uninit->page_initializer(page, uninit->type, kva) &&
				 (init ? init(page, aux) : true);
}

/* Free the resources hold by uninit_page. Although most of pages are transmuted
 * to other page objects, it is possible to have uninit pages when the process
 * exit, which are never referenced during the execution.
 * PAGE will be freed by the caller. */
static void
uninit_destroy(struct page *page)
{
	struct uninit_page *uninit UNUSED = &page->uninit;
	/* TODO: Fill this function.
	 * TODO: If you don't have anything to do, just return. */
}
