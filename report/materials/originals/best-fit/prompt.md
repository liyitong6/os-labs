# Lab2 功能模块二任务二：Best-Fit 分配器提示词


[PROMPT]
任务：在 riscv64-ucore 内核中实现并接入 Best-Fit 连续物理内存分配器。需要完成两件事：
1) 在 kern/mm/best_fit_pmm.c 中标注 //lab2 function2-2 YOUR CODE 的位置，补全一个完整的 Best-Fit pmm 模块（空闲块管理数据结构、init/init_memmap/alloc_pages/free_pages/nr_free_pages 五个方法，以及 best_fit_pmm_manager 注册结构）。该文件目前只提供 basic_check() 和 best_fit_check() 两个测试函数。
2) 修改 kern/mm/pmm.c 中的 init_pmm_manager()，把当前使用的物理内存管理器从 default_pmm_manager 切换为 best_fit_pmm_manager，使内核真正运行 Best-Fit 分配器。

操作要求：你必须在项目中修改实际文件，而不是只展示代码片段。保留 best_fit_pmm.c 中已有的 basic_check() 和 best_fit_check() 两个函数及其全部断言逻辑，一个字符都不要改、不要删；你新增的代码只放在 `//lab2 function2-2 YOUR CODE` 注释下方（以及文件末尾的 manager 注册结构处）。参考实现 kern/mm/default_pmm.c 是 First-Fit 的完整可工作版本，可以阅读它理解框架约定，但你新写的是 Best-Fit 语义，不要照抄 First-Fit 的"选第一个满足块"策略。

输出要求：直接进行文件操作。使用下面 [RELY]、[GUARANTEE]、[SPECIFICATION] 提供的信息，并以工程里真实的头文件（pmm.h、memlayout.h、list.h）为准；不要引入额外的功能模块、不要改 Makefile、不要改其他 pmm 源文件。完成后请用 make 确认能编译通过。

[RELY]
// 来自 kern/mm/pmm.h 的管理器框架
struct pmm_manager {
    const char *name;
    void (*init)(void);
    void (*init_memmap)(struct Page *base, size_t n);
    struct Page *(*alloc_pages)(size_t n);
    void (*free_pages)(struct Page *base, size_t n);
    size_t (*nr_free_pages)(void);
    void (*check)(void);
};
extern const struct pmm_manager *pmm_manager;
struct Page *alloc_pages(size_t n);   // 全局包装函数，内部转发给 pmm_manager->alloc_pages
void free_pages(struct Page *base, size_t n);
size_t nr_free_pages(void);
#define alloc_page() alloc_pages(1)
#define free_page(page) free_pages(page, 1)

// 来自 kern/mm/memlayout.h 的页描述符与状态位
struct Page {
    int ref;
    uint64_t flags;
    unsigned int property;   // 只有"空闲块头页"才使用：记录本空闲块连续页数
    list_entry_t page_link;  // 挂入空闲块链表的节点
};
#define PG_reserved  0
#define PG_property  1
#define SetPageReserved(page) / ClearPageReserved(page) / PageReserved(page)
#define SetPageProperty(page) / ClearPageProperty(page) / PageProperty(page)
#define le2page(le, member)  to_struct((le), struct Page, member)
typedef struct { list_entry_t free_list; unsigned int nr_free; } free_area_t;

// 来自 libs/list.h 的双向循环链表原语（均 inline）
void list_init(list_entry_t *elm);
void list_add(list_entry_t *listelm, list_entry_t *elm);
void list_add_before(list_entry_t *listelm, list_entry_t *elm);
void list_add_after(list_entry_t *listelm, list_entry_t *elm);
void list_del(list_entry_t *elm);
bool list_empty(list_entry_t *list);
list_entry_t *list_next(list_entry_t *listelm);
list_entry_t *list_prev(list_entry_t *listelm);

// 测试函数硬编码依赖的名字约定
best_fit_pmm.c 里现有的 basic_check() / best_fit_check() 直接使用了 free_list 和 nr_free：
你必须定义 static free_area_t free_area; 并宏映射：
  #define free_list (free_area.free_list)
  #define nr_free   (free_area.nr_free)


//  - kern/mm/default_pmm.c 是 First-Fit 的完整参考。
//  - best_fit_pmm.c 中已 static 定义好 basic_check(void) 和 best_fit_check(void)，可直接注册为 .check。
//  - pmm.c 中 init_pmm_manager() 当前写的是 pmm_manager = &default_pmm_manager; 需要改为 &best_fit_pmm_manager。

[GUARANTEE]
在 best_fit_pmm.c 中必须交付以下内部静态函数与全局注册结构：

static void best_fit_init(void);
static void best_fit_init_memmap(struct Page *base, size_t n);
static struct Page *best_fit_alloc_pages(size_t n);
static void best_fit_free_pages(struct Page *base, size_t n);
static size_t best_fit_nr_free_pages(void);

const struct pmm_manager best_fit_pmm_manager = {
    .name = "best_fit_pmm_manager",
    .init = best_fit_init,
    .init_memmap = best_fit_init_memmap,
    .alloc_pages = best_fit_alloc_pages,
    .free_pages = best_fit_free_pages,
    .nr_free_pages = best_fit_nr_free_pages,
    .check = best_fit_check,
};


不允许新增对外符号、不允许改动 pmm_manager 接口签名、不允许改动 basic_check / best_fit_check。

同时在 kern/mm/pmm.c 的 init_pmm_manager() 内，把
    pmm_manager = &default_pmm_manager;
改为
    pmm_manager = &best_fit_pmm_manager;

[SPECIFICATION]

## best_fit_init
Pre-Condition：尚未建立任何空闲块。
Post-Condition：free_list 是空的双向循环链表，nr_free == 0。

## best_fit_init_memmap
Pre-Condition：base 指向一段连续 n 个物理页的描述符数组，这 n 页当前都处于保留状态，n > 0。
Post-Condition：这 n 页被登记为一个空闲块，nr_free 增加 n；块头页置 PG_property 位、property = n。
Case 1（链表为空）：直接挂到 free_list 后面。
Case 2（链表非空）：按 Page 地址升序插入正确位置（插到第一个地址比 base 大的块之前；都小则插到末尾）。
Requirements：把这 n 页的 flags 清 0、ref 清 0；只有 base 置 PG_property。

## best_fit_alloc_pages —— 核心差异点
Pre-Condition：n > 0；空闲链表按地址升序组织。
Post-Condition：成功则返回分配出去的连续 n 页的块头 Page；从链表摘除，nr_free 减 n；若块大小 > n，尾部剩余作为新块头页挂回链表；被分配头页 ClearPageProperty。失败返回 NULL。
Case 1（nr_free < n）：直接返回 NULL。
Case 2（存在可满足块）：遍历整个空闲链表，在所有 property >= n 的块里选择 property 最小的那个。禁止"遇到第一个就 break"。选中后从链表摘除；块大小 == n 整块占用；块大小 > n 则前 n 页切出、尾部作为新块插回。
Case 3（无任何块 >= n）：返回 NULL。
Requirements：分配出去的头页 ClearPageProperty。

## best_fit_free_pages
Pre-Condition：base 指向要回收的连续 n 页，n > 0；这 n 页当前既不 reserved 也不是空闲块头。
Post-Condition：这 n 页被回收为新空闲块，nr_free 增加 n；插入后与相邻前/后空闲块合并，合并时清掉邻居的 PG_property、累加 property、摘除邻居节点。
Case 1（链表为空）：直接挂入。
Case 2（链表非空）：按地址升序插入；再检查前驱是否紧邻、后继是否紧邻，紧邻则合并。
Requirements：回收时把 flags 清 0、ref 清 0，base 置为新块头页。

## best_fit_nr_free_pages
返回当前 nr_free。

## pmm.c 中 init_pmm_manager 的切换
只改 pmm_manager 赋值那一行，不要动 page_init、check_alloc_page 等其他逻辑。

## 验收标准
- basic_check 全部 assert 通过。
- best_fit_check：空闲链表同时有 2 页块（p0+1）和 1 页块（p0+4）时，alloc_pages(1) 必须返回 p0+4；随后 alloc_pages(2) 必须成功。
- 链表遍历：每个在链节点 PageProperty(p) 为真，property 之和 == nr_free_pages()。

