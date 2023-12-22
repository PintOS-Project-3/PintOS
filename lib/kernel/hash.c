/* Hash table.

	 This data structure is thoroughly documented in the Tour of
	 Pintos for Project 3.

	 See hash.h for basic information. */

#include "hash.h"
#include "../debug.h"
#include "threads/malloc.h"
#include "vm/vm.h"

#define list_elem_to_hash_elem(LIST_ELEM) \
	list_entry(LIST_ELEM, struct hash_elem, list_elem)

static struct list *find_bucket(struct hash *, struct hash_elem *);
static struct hash_elem *find_elem(struct hash *, struct list *,
																	 struct hash_elem *);
static void insert_elem(struct hash *, struct list *, struct hash_elem *);
static void remove_elem(struct hash *, struct hash_elem *);
static void rehash(struct hash *);

/* 해시 테이블 H를 초기화하여 HASH를 사용하여 해시 값을 계산하고,
	 LESS를 사용하여 해시 요소를 비교합니다. 보조 데이터 AUX가 주어집니다. */
/* Initializes hash table H to compute hash values using HASH and
	 compare hash elements using LESS, given auxiliary data AUX. */
bool hash_init(struct hash *h,
							 hash_hash_func *hash, hash_less_func *less, void *aux)
{
	h->elem_cnt = 0;
	h->bucket_cnt = 4;
	h->buckets = malloc(sizeof *h->buckets * h->bucket_cnt);
	h->hash = hash;
	h->less = less;
	h->aux = aux;

	if (h->buckets != NULL)
	{
		hash_clear(h, NULL);
		return true;
	}
	else
		return false;
}

/* 해시 테이블 H에서 모든 요소를 제거합니다.

	 DESTRUCTOR가 NULL이 아니라면, 해시의 각 요소에 대해 호출됩니다.
	 DESTRUCTOR는 해시 요소에 사용된 메모리를 적절하게 해제할 수 있습니다.
	 그러나, hash_clear()가 실행되는 동안 해시 테이블 H를 수정하는 것은
	 hash_clear(), hash_destroy(), hash_insert(), hash_replace(),
	 hash_delete() 함수를 사용하더라도 정의되지 않은 동작을 야기합니다.
	 이는 DESTRUCTOR 내에서든 다른 곳에서든 마찬가지입니다. */
/* Removes all the elements from H.

	 If DESTRUCTOR is non-null, then it is called for each element
	 in the hash.  DESTRUCTOR may, if appropriate, deallocate the
	 memory used by the hash element.  However, modifying hash
	 table H while hash_clear() is running, using any of the
	 functions hash_clear(), hash_destroy(), hash_insert(),
	 hash_replace(), or hash_delete(), yields undefined behavior,
	 whether done in DESTRUCTOR or elsewhere. */
void hash_clear(struct hash *h, hash_action_func *destructor)
{
	size_t i;

	// 해시 테이블의 각 버킷에 대해 반복합니다.
	for (i = 0; i < h->bucket_cnt; i++)
	{
		struct list *bucket = &h->buckets[i];

		// DESTRUCTOR가 NULL이 아닐 경우 각 요소에 대해 DESTRUCTOR를 호출합니다.
		if (destructor != NULL)
			while (!list_empty(bucket))
			{
				struct list_elem *list_elem = list_pop_front(bucket);
				struct hash_elem *hash_elem = list_elem_to_hash_elem(list_elem);
				destructor(hash_elem, h->aux); // 요소에 대해 DESTRUCTOR 함수 호출
			}

		// 해당 버킷을 초기화합니다.
		list_init(bucket);
	}

	// 해시 테이블의 요소 수를 0으로 설정합니다.
	h->elem_cnt = 0;
}

/* 해시 테이블 H를 파괴합니다.

	 DESTRUCTOR가 NULL이 아니라면, 해시의 각 요소에 대해 먼저 호출됩니다.
	 DESTRUCTOR는 적절할 경우 해시 요소에 사용된 메모리를 해제할 수 있습니다.
	 그러나, hash_clear()가 실행되는 동안 해시 테이블 H를 수정하는 것은
	 hash_clear(), hash_destroy(), hash_insert(), hash_replace(),
	 hash_delete() 함수를 사용하더라도 정의되지 않은 동작을 야기합니다.
	 이는 DESTRUCTOR 내에서든 다른 곳에서든 마찬가지입니다. */
/* Destroys hash table H.

	 If DESTRUCTOR is non-null, then it is first called for each
	 element in the hash.  DESTRUCTOR may, if appropriate,
	 deallocate the memory used by the hash element.  However,
	 modifying hash table H while hash_clear() is running, using
	 any of the functions hash_clear(), hash_destroy(),
	 hash_insert(), hash_replace(), or hash_delete(), yields
	 undefined behavior, whether done in DESTRUCTOR or
	 elsewhere. */
void hash_destroy(struct hash *h, hash_action_func *destructor)
{
	// DESTRUCTOR가 NULL이 아닐 경우 해시 테이블의 모든 요소에 대해 DESTRUCTOR를 호출합니다.
	if (destructor != NULL)
		hash_clear(h, destructor);

	// 해시 테이블의 버킷 배열에 할당된 메모리를 해제합니다.
	free(h->buckets);
}

/* 해시 테이블 H에 새 요소 NEW를 삽입하고, 테이블에 동일한 요소가
	 이미 없으면 NULL 포인터를 반환합니다.
	 만약 테이블에 동일한 요소가 이미 있으면, 새 요소를 삽입하지 않고
	 기존 요소를 반환합니다. */
/* Inserts NEW into hash table H and returns a null pointer, if
	 no equal element is already in the table.
	 If an equal element is already in the table, returns it
	 without inserting NEW. */
struct hash_elem *
hash_insert(struct hash *h, struct hash_elem *new)
{
	struct list *bucket = find_bucket(h, new);				 // 새 요소가 들어갈 버킷을 찾습니다.
	struct hash_elem *old = find_elem(h, bucket, new); // 동일한 요소가 있는지 검사합니다.

	if (old == NULL)
		insert_elem(h, bucket, new); // 동일한 요소가 없으면 새 요소를 삽입합니다.

	rehash(h); // 필요한 경우 해시 테이블을 재해시합니다.

	return old; // 기존 요소 또는 NULL을 반환합니다.
}

/* 해시 테이블 H에 새 요소 NEW를 삽입하며, 테이블에 이미 존재하는 동일한 요소를
	 대체합니다. 대체된 요소는 반환됩니다. */
/* Inserts NEW into hash table H, replacing any equal element
	 already in the table, which is returned. */
struct hash_elem *
hash_replace(struct hash *h, struct hash_elem *new)
{
	struct list *bucket = find_bucket(h, new);				 // 새 요소가 들어갈 버킷을 찾습니다.
	struct hash_elem *old = find_elem(h, bucket, new); // 동일한 요소가 있는지 검사합니다.

	if (old != NULL)
		remove_elem(h, old);			 // 동일한 요소가 있으면 제거합니다.
	insert_elem(h, bucket, new); // 새 요소를 삽입합니다.

	rehash(h); // 필요한 경우 해시 테이블을 재해시합니다.

	return old; // 대체된 기존 요소 또는 NULL을 반환합니다.
}

/* 해시 테이블 H에서 요소 E와 동일한 요소를 찾아 반환합니다.
	 테이블에 동일한 요소가 없으면 NULL 포인터를 반환합니다. */
/* Finds and returns an element equal to E in hash table H, or a
	 null pointer if no equal element exists in the table. */
struct hash_elem *
hash_find(struct hash *h, struct hash_elem *e)
{
	return find_elem(h, find_bucket(h, e), e); // 동일한 요소를 찾아 반환합니다.
}

/* 해시 테이블 H에서 요소 E와 동일한 요소를 찾아 제거하고 반환합니다.
	 테이블에 동일한 요소가 존재하지 않으면 NULL 포인터를 반환합니다.

	 해시 테이블의 요소가 동적으로 할당되었거나, 관련 리소스를 가지고 있다면,
	 호출자의 책임으로 이를 해제해야 합니다. */
/* Finds, removes, and returns an element equal to E in hash
	 table H.  Returns a null pointer if no equal element existed
	 in the table.

	 If the elements of the hash table are dynamically allocated,
	 or own resources that are, then it is the caller's
	 responsibility to deallocate them. */
struct hash_elem *
hash_delete(struct hash *h, struct hash_elem *e)
{
	struct hash_elem *found = find_elem(h, find_bucket(h, e), e); // 동일한 요소를 찾습니다.
	if (found != NULL)
	{
		remove_elem(h, found); // 찾은 요소를 제거합니다.
		rehash(h);						 // 필요한 경우 해시 테이블을 재해시합니다.
	}
	return found; // 제거된 요소를 반환합니다.
}

/* 해시 테이블 H의 각 요소에 대해 임의의 순서로 ACTION 함수를 호출합니다.
	 hash_apply()가 실행되는 동안 해시 테이블 H를 수정하는 것은
	 hash_clear(), hash_destroy(), hash_insert(), hash_replace(),
	 또는 hash_delete() 함수를 사용하더라도 정의되지 않은 동작을 야기합니다.
	 이는 ACTION 내에서든 다른 곳에서든 마찬가지입니다. */
/* Calls ACTION for each element in hash table H in arbitrary
	 order.
	 Modifying hash table H while hash_apply() is running, using
	 any of the functions hash_clear(), hash_destroy(),
	 hash_insert(), hash_replace(), or hash_delete(), yields
	 undefined behavior, whether done from ACTION or elsewhere. */
void hash_apply(struct hash *h, hash_action_func *action)
{
	size_t i;

	ASSERT(action != NULL); // action이 NULL이 아님을 확인합니다.

	for (i = 0; i < h->bucket_cnt; i++) // 모든 버킷을 순회합니다.
	{
		struct list *bucket = &h->buckets[i];
		struct list_elem *elem, *next;

		for (elem = list_begin(bucket); elem != list_end(bucket); elem = next) // 버킷 내의 각 요소에 대해 반복합니다.
		{
			next = list_next(elem);												// 다음 요소를 저장합니다.
			action(list_elem_to_hash_elem(elem), h->aux); // 각 요소에 대해 ACTION 함수를 호출합니다.
		}
	}
}

/* 해시 테이블 H를 순회하기 위해 이터레이터 I를 초기화합니다.

	 반복 사용 예시:

	 struct hash_iterator i;

	 hash_first(&i, h);
	 while (hash_next(&i))
	 {
		 struct foo *f = hash_entry(hash_cur(&i), struct foo, elem);
		 ...f와 관련된 작업 수행...
	 }

	 반복 중에 해시 테이블 H를 수정하는 것은
	 hash_clear(), hash_destroy(), hash_insert(),
	 hash_replace(), 또는 hash_delete() 함수를 사용하더라도
	 모든 이터레이터를 무효화합니다. */
/* Initializes I for iterating hash table H.

	 Iteration idiom:

	 struct hash_iterator i;

	 hash_first (&i, h);
	 while (hash_next (&i))
	 {
	 struct foo *f = hash_entry (hash_cur (&i), struct foo, elem);
	 ...do something with f...
	 }

	 Modifying hash table H during iteration, using any of the
	 functions hash_clear(), hash_destroy(), hash_insert(),
	 hash_replace(), or hash_delete(), invalidates all
	 iterators. */
void hash_first(struct hash_iterator *i, struct hash *h)
{
	ASSERT(i != NULL); // i가 NULL이 아님을 확인합니다.
	ASSERT(h != NULL); // h가 NULL이 아님을 확인합니다.

	i->hash = h;																						// 이터레이터에 해시 테이블을 할당합니다.
	i->bucket = i->hash->buckets;														// 이터레이터의 현재 버킷을 해시 테이블의 첫 번째 버킷으로 설정합니다.
	i->elem = list_elem_to_hash_elem(list_head(i->bucket)); // 이터레이터의 현재 요소를 첫 번째 버킷의 첫 번째 요소로 설정합니다.
}

/* 이터레이터 I를 해시 테이블의 다음 요소로 진행시키고 반환합니다.
	 더 이상 요소가 없으면 NULL 포인터를 반환합니다.
	 요소는 임의의 순서로 반환됩니다.

	 반복 중에 해시 테이블 H를 수정하는 것은
	 hash_clear(), hash_destroy(), hash_insert(),
	 hash_replace(), 또는 hash_delete() 함수를 사용하더라도
	 모든 이터레이터를 무효화합니다. */
/* Advances I to the next element in the hash table and returns
	 it.  Returns a null pointer if no elements are left.  Elements
	 are returned in arbitrary order.

	 Modifying a hash table H during iteration, using any of the
	 functions hash_clear(), hash_destroy(), hash_insert(),
	 hash_replace(), or hash_delete(), invalidates all
	 iterators. */
struct hash_elem *
hash_next(struct hash_iterator *i)
{
	ASSERT(i != NULL); // i가 NULL이 아님을 확인합니다.

	i->elem = list_elem_to_hash_elem(list_next(&i->elem->list_elem)); // 다음 요소로 이동합니다.
	while (i->elem == list_elem_to_hash_elem(list_end(i->bucket)))		// 현재 버킷의 끝에 도달했는지 확인합니다.
	{
		if (++i->bucket >= i->hash->buckets + i->hash->bucket_cnt) // 모든 버킷을 순회했는지 확인합니다.
		{
			i->elem = NULL; // 더 이상 요소가 없으면 NULL로 설정합니다.
			break;
		}
		i->elem = list_elem_to_hash_elem(list_begin(i->bucket)); // 다음 버킷의 첫 번째 요소로 이동합니다.
	}

	return i->elem; // 현재 요소를 반환합니다.
}

/* Returns the current element in the hash table iteration, or a
	 null pointer at the end of the table.  Undefined behavior
	 after calling hash_first() but before hash_next(). */
struct hash_elem *
hash_cur(struct hash_iterator *i)
{
	return i->elem;
}

/* Returns the number of elements in H. */
size_t
hash_size(struct hash *h)
{
	return h->elem_cnt;
}

/* Returns true if H contains no elements, false otherwise. */
bool hash_empty(struct hash *h)
{
	return h->elem_cnt == 0;
}

/* Fowler-Noll-Vo hash constants, for 32-bit word sizes. */
#define FNV_64_PRIME 0x00000100000001B3UL
#define FNV_64_BASIS 0xcbf29ce484222325UL

/* Returns a hash of the SIZE bytes in BUF. */
uint64_t
hash_bytes(const void *buf_, size_t size)
{
	/* Fowler-Noll-Vo 32-bit hash, for bytes. */
	const unsigned char *buf = buf_;
	uint64_t hash;

	ASSERT(buf != NULL);

	hash = FNV_64_BASIS;
	while (size-- > 0)
		hash = (hash * FNV_64_PRIME) ^ *buf++;

	return hash;
}

/* Returns a hash of string S. */
uint64_t
hash_string(const char *s_)
{
	const unsigned char *s = (const unsigned char *)s_;
	uint64_t hash;

	ASSERT(s != NULL);

	hash = FNV_64_BASIS;
	while (*s != '\0')
		hash = (hash * FNV_64_PRIME) ^ *s++;

	return hash;
}

/* Returns a hash of integer I. */
uint64_t
hash_int(int i)
{
	return hash_bytes(&i, sizeof i);
}

/* Returns the bucket in H that E belongs in. */
static struct list *
find_bucket(struct hash *h, struct hash_elem *e)
{
	size_t bucket_idx = h->hash(e, h->aux) & (h->bucket_cnt - 1);
	return &h->buckets[bucket_idx];
}

/* Searches BUCKET in H for a hash element equal to E.  Returns
	 it if found or a null pointer otherwise. */
static struct hash_elem *
find_elem(struct hash *h, struct list *bucket, struct hash_elem *e)
{
	struct list_elem *i;

	for (i = list_begin(bucket); i != list_end(bucket); i = list_next(i))
	{
		struct hash_elem *hi = list_elem_to_hash_elem(i);
		if (!h->less(hi, e, h->aux) && !h->less(e, hi, h->aux))
			return hi;
	}
	return NULL;
}

/* Returns X with its lowest-order bit set to 1 turned off. */
static inline size_t
turn_off_least_1bit(size_t x)
{
	return x & (x - 1);
}

/* Returns true if X is a power of 2, otherwise false. */
static inline size_t
is_power_of_2(size_t x)
{
	return x != 0 && turn_off_least_1bit(x) == 0;
}

/* Element per bucket ratios. */
#define MIN_ELEMS_PER_BUCKET 1	/* Elems/bucket < 1: reduce # of buckets. */
#define BEST_ELEMS_PER_BUCKET 2 /* Ideal elems/bucket. */
#define MAX_ELEMS_PER_BUCKET 4	/* Elems/bucket > 4: increase # of buckets. */

/* 해시 테이블 H의 버킷 수를 이상적인 수로 변경합니다.
	 이 함수는 메모리 부족으로 인해 실패할 수 있으나, 그럴 경우에도
	 해시 접근은 단지 덜 효율적일 뿐 계속 사용 가능합니다.
	 해시 테이블의 크기를 조정하고, 요소의 분포를 최적화 */
/* Changes the number of buckets in hash table H to match the
	 ideal.  This function can fail because of an out-of-memory
	 condition, but that'll just make hash accesses less efficient;
	 we can still continue. */
static void
rehash(struct hash *h)
{
	size_t old_bucket_cnt, new_bucket_cnt;
	struct list *new_buckets, *old_buckets;
	size_t i;

	ASSERT(h != NULL); // h가 NULL이 아님을 확인합니다.

	/* 이전 버킷 정보를 저장합니다. */
	old_buckets = h->buckets;
	old_bucket_cnt = h->bucket_cnt;

	/* 사용할 새로운 버킷 수를 계산합니다.
		 BEST_ELEMS_PER_BUCKET 당 하나의 버킷을 원합니다.
		 최소한 4개의 버킷이 필요하며, 버킷 수는 2의 거듭제곱이어야 합니다. */
	new_bucket_cnt = h->elem_cnt / BEST_ELEMS_PER_BUCKET;
	if (new_bucket_cnt < 4)
		new_bucket_cnt = 4;
	while (!is_power_of_2(new_bucket_cnt))
		new_bucket_cnt = turn_off_least_1bit(new_bucket_cnt); // 2의 거듭제곱으로 만듭니다.

	/* 버킷 수가 변경되지 않으면 아무것도 하지 않습니다. */
	if (new_bucket_cnt == old_bucket_cnt)
		return;

	/* 새로운 버킷을 할당하고 초기화합니다. */
	new_buckets = malloc(sizeof *new_buckets * new_bucket_cnt);
	if (new_buckets == NULL)
	{
		/* 메모리 할당 실패. 이는 해시 테이블 사용이 덜 효율적이 됩니다.
			 그러나 여전히 사용 가능하므로 오류는 아닙니다. */
		return;
	}
	for (i = 0; i < new_bucket_cnt; i++)
		list_init(&new_buckets[i]);

	/* 새로운 버킷 정보를 설치합니다. */
	h->buckets = new_buckets;
	h->bucket_cnt = new_bucket_cnt;

	/* 각각의 이전 요소를 적절한 새 버킷으로 이동합니다. */
	for (i = 0; i < old_bucket_cnt; i++)
	{
		struct list *old_bucket;
		struct list_elem *elem, *next;

		old_bucket = &old_buckets[i];
		for (elem = list_begin(old_bucket); elem != list_end(old_bucket); elem = next)
		{
			struct list *new_bucket = find_bucket(h, list_elem_to_hash_elem(elem));
			next = list_next(elem);
			list_remove(elem);
			list_push_front(new_bucket, elem);
		}
	}

	free(old_buckets); // 이전 버킷의 메모리를 해제합니다.
}

/* Inserts E into BUCKET (in hash table H). */
static void
insert_elem(struct hash *h, struct list *bucket, struct hash_elem *e)
{
	h->elem_cnt++;
	list_push_front(bucket, &e->list_elem);
}

/* Removes E from hash table H. */
static void
remove_elem(struct hash *h, struct hash_elem *e)
{
	h->elem_cnt--;
	list_remove(&e->list_elem);
}

/* 함수 추가
 * page_hash()
 * page_less() */
/* Returns a hash value for page p. */
unsigned page_hash(const struct hash_elem *p_, void *aux UNUSED)
{
	const struct page *p = hash_entry(p_, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof p->va);
}

/* Returns true if page a precedes page b. */
bool page_less(const struct hash_elem *a_,
							 const struct hash_elem *b_, void *aux UNUSED)
{
	const struct page *a = hash_entry(a_, struct page, hash_elem);
	const struct page *b = hash_entry(b_, struct page, hash_elem);

	return a->va < b->va;
}

/* Returns the page containing the given virtual address,
 * or a null pointer if no such page exists. */
struct page *
page_lookup(struct hash *h, const void *address)
{
	struct page p;
	struct hash_elem *e;

	p.va = address;
	e = hash_find(h, &p.hash_elem);
	return e != NULL ? hash_entry(e, struct page, hash_elem) : NULL;
}