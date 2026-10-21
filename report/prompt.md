# Lab2 提示词汇总

## 记录说明


- [整合报告](report.md)
- [洪若曦原始 Prompt](materials/originals/best-fit/prompt.md)
- [秦紫轩实现原始记录](materials/originals/buddy/buddy_prompt1.md)
- [秦紫轩测试原始记录](materials/originals/buddy/buddy_prompt2.md)

## 一、李易桐（2412933）：功能模块一

### Prompt 1：实现物理内存初始化

[PROMPT]

我负责 riscv64-ucore Lab2 功能模块一，请根据当前工程接口，协助实现 `kern/mm/pmm.c` 中的 `static void page_init(void)`。

[RELY]

使用设备树接口 `get_memory_base()`、`get_memory_size()` 获取内存信息；结合 `end`、`struct Page`、`DRAM_BASE`、`PHYSICAL_MEMORY_END` 及工程已有的地址转换、页对齐和页面状态接口完成初始化。

[GUARANTEE]

必须区分虚拟地址、物理地址、物理页号和页描述符指针。OpenSBI、内核镜像及描述符数组占用的页面不能交给分配器。设备树信息无效、地址计算溢出或没有完整可用页面时，应终止初始化，不得登记空区间。保留其他成员的分配器实现和已有测试接口。

[SPECIFICATION]

1. 读取并校验物理内存信息，按照平台限制确定可管理上界。
2. 设置 `va_pa_offset`、`npage` 和 `pages`，说明为什么描述符数量是 `npage - nbase`。
3. 将描述符数组放在内核结束后的页对齐位置，检查数组所需空间，再初始化描述符并标记为保留。
4. 排除已占用空间，将空闲起点向上对齐、终点向下对齐，只把完整空闲页交给当前 `pmm_manager`。
5. 输出内存范围和空闲页数，并解释关键计算过程。请先给出实现思路，再给出代码；提供修改命令时先备份原文件。

### Prompt 2：检查实现并指导验证

[PROMPT]

请检查功能模块一的 `page_init()` 实现，重点核对内存边界、描述符布局和地址转换，并结合我提供的编译与启动日志指导验证。

[RELY]

以当前工程的 `pmm.c`、`pmm.h`、`memlayout.h`、设备树接口和 Makefile 为依据，结合实际终端输出分析，不假定其他成员的模块已经通过测试。

[GUARANTEE]

检查过程不能破坏内核镜像或描述符数组，也不能通过删除断言、跳过自检来获得成功结果。区分代码检查、边界用例设计和实际运行验证，未执行的测试不能记为已通过。

[SPECIFICATION]

1. 核对 `npage`、描述符数量、数组物理末端及空闲页数的计算，确认没有重复登记或遗漏尾部对齐。
2. 分析零容量、地址溢出、超过管理上界、末端未对齐、描述符空间不足等情况，说明预期处理方式。
3. 根据现有 Makefile 给出编译和启动步骤；出现问题时，结合日志判断故障发生在编译、内核加载、分页切换还是分配器检查阶段。
4. 核对启动输出中的内存范围、管理器名称、空闲页数和 `check_alloc_page() succeeded!`。
5. 说明需要保存哪些运行输出和截图，并总结这些结果能够验证的内容，供实验报告使用。

## 二、洪若曦（2413524）：First-Fit 分析与 Best-Fit 实现

### Lab2 功能模块二任务二：Best-Fit 分配器提示词


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

#### best_fit_init
Pre-Condition：尚未建立任何空闲块。
Post-Condition：free_list 是空的双向循环链表，nr_free == 0。

#### best_fit_init_memmap
Pre-Condition：base 指向一段连续 n 个物理页的描述符数组，这 n 页当前都处于保留状态，n > 0。
Post-Condition：这 n 页被登记为一个空闲块，nr_free 增加 n；块头页置 PG_property 位、property = n。
Case 1（链表为空）：直接挂到 free_list 后面。
Case 2（链表非空）：按 Page 地址升序插入正确位置（插到第一个地址比 base 大的块之前；都小则插到末尾）。
Requirements：把这 n 页的 flags 清 0、ref 清 0；只有 base 置 PG_property。

#### best_fit_alloc_pages —— 核心差异点
Pre-Condition：n > 0；空闲链表按地址升序组织。
Post-Condition：成功则返回分配出去的连续 n 页的块头 Page；从链表摘除，nr_free 减 n；若块大小 > n，尾部剩余作为新块头页挂回链表；被分配头页 ClearPageProperty。失败返回 NULL。
Case 1（nr_free < n）：直接返回 NULL。
Case 2（存在可满足块）：遍历整个空闲链表，在所有 property >= n 的块里选择 property 最小的那个。禁止"遇到第一个就 break"。选中后从链表摘除；块大小 == n 整块占用；块大小 > n 则前 n 页切出、尾部作为新块插回。
Case 3（无任何块 >= n）：返回 NULL。
Requirements：分配出去的头页 ClearPageProperty。

#### best_fit_free_pages
Pre-Condition：base 指向要回收的连续 n 页，n > 0；这 n 页当前既不 reserved 也不是空闲块头。
Post-Condition：这 n 页被回收为新空闲块，nr_free 增加 n；插入后与相邻前/后空闲块合并，合并时清掉邻居的 PG_property、累加 property、摘除邻居节点。
Case 1（链表为空）：直接挂入。
Case 2（链表非空）：按地址升序插入；再检查前驱是否紧邻、后继是否紧邻，紧邻则合并。
Requirements：回收时把 flags 清 0、ref 清 0，base 置为新块头页。

#### best_fit_nr_free_pages
返回当前 nr_free。

#### pmm.c 中 init_pmm_manager 的切换
只改 pmm_manager 赋值那一行，不要动 page_init、check_alloc_page 等其他逻辑。

#### 验收标准
- basic_check 全部 assert 通过。
- best_fit_check：空闲链表同时有 2 页块（p0+1）和 1 页块（p0+4）时，alloc_pages(1) 必须返回 p0+4；随后 alloc_pages(2) 必须成功。
- 链表遍历：每个在链节点 PageProperty(p) 为真，property 之和 == nr_free_pages()。


## 三、秦紫轩（2413646）：Buddy System 实现与测试

### Buddy System 实现 Prompt 记录

#### 小模块一：管理基础、可用页初始化与物理页分配

##### [PROMPT]

**任务**

参考当前 uCore 工程，实现 Buddy System 的内存块管理基础、可用物理页初始化和物理页分配功能。

工程代码目录为 `D:\OS-Lab\projects\lab2\code`，在 WSL 中对应 `/mnt/d/OS-Lab/projects/lab2/code`。

本次仅新增以下文件：

- `kern/mm/buddy_pmm.c`：实现管理状态、可用内存登记、物理页分配和空闲页计数。
- `kern/mm/buddy_pmm.h`：按需要提供后续接入所需的声明，避免暴露内部实现。

如果目标文件已经存在，先读取并说明现有内容，保留与本任务无关的修改。

**操作要求**

必须修改工程中的真实文件。修改前先阅读相关代码，简要说明数据结构、块对齐依据、分配约定和修改范围。

本次不实现释放与合并，不切换内核当前分配器，不修改 Makefile、公共 `struct Page` 或现有分配器。不得代做 Best-Fit，不得修改、删除原有测试，也不得用空函数或固定返回值伪造尚未完成的接口。

注意有“lab2”的注释，代码中所有需要完成的地方（challenge 除外）都有“lab2”和“YOUR CODE”的注释，请在提交时特别注意保持注释，并将“YOUR CODE”替换为自己的学号，并把所有标有对应注释的部分填上正确的代码。

上述注释要求仅应用于**本次 Buddy 任务对应的部分**，不得因此修改其他组员负责的任务。新增实现按同样约定添加标记；保留 `lab2` 和任务说明，用本人学号替换 `YOUR CODE`。

我的学号是 **2413646**。本任务对应的代码注释保留 `lab2` 和任务说明，将 `YOUR CODE` 替换为 `2413646`。

**输出要求**

完成后说明新增功能、关键状态约定和编译结果。

本阶段只进行编译检查，**不设计、编写或运行 Buddy 功能测试**；测试统一由第之后的 Prompt 驱动。不得把编译成功表述为算法正确或完整 Buddy 分配器已经完成。

##### [RELY]

以当前工程源码为准，以下是与本任务直接相关的上下文；修改前核对实际定义。

**物理页与页描述符**

`kern/mm/mmu.h` 中：

```c
#define PGSIZE  4096
#define PGSHIFT 12
```

`kern/mm/memlayout.h` 中：

```c
struct Page {
    int ref;
    uint64_t flags;
    unsigned int property;
    list_entry_t page_link;
};
```

每个 `struct Page` 描述一个物理页。连续的页描述符对应连续的物理页，但**页描述符地址不等于其描述的物理页地址**。

现有状态标志的语义：

- `PageReserved(page)`：该页被保留，不能用于普通分配。
- `PageProperty(page)`：该页是一个空闲块的首页。
- `ref`：物理页引用计数，不得用作 Buddy 阶数或分配状态。

可使用对应的 `SetPageReserved`、`ClearPageReserved`、`SetPageProperty`、`ClearPageProperty` 宏维护状态。

**地址关系与已有接口**

`kern/mm/pmm.h` 中：

```c
extern struct Page *pages;
extern size_t npage;
extern const size_t nbase;

static inline ppn_t page2ppn(struct Page *page) {
    return page - pages + nbase;
}

static inline uintptr_t page2pa(struct Page *page) {
    return page2ppn(page) << PGSHIFT;
}
```

`pages` 对应物理内存起始页号 `nbase`，描述符数量为 `npage - nbase`；`npage` 是物理页号上界，不能直接当作描述符数量。

可复用 `list.h` 的链表操作、`set_page_ref()` 和工程已有的断言机制。使用前核对声明，不引入宿主系统的 `malloc`、标准库容器或其他内核运行时不可用的依赖。

**现有初始化流程**

`pmm.c` 先初始化选中的分配器，再由 `page_init()` 排除 OpenSBI、内核、页描述符数组等占用区域，最后通过：

```c
init_memmap(struct Page *base, size_t n);
```

将可管理的连续物理页交给分配器。传入页数和起始位置不保证能够组成一个完整的大伙伴块。

`pmm_manager` 的分配接口允许分配至少 `n` 页。当前 Best-Fit 尚未完成，Makefile 会编译其源文件；本阶段不能为通过编译而修改该模块。

##### [GUARANTEE]

必须实现以下函数，签名与后续 `pmm_manager` 对应接口兼容：

```c
static void buddy_init(void);

static void buddy_init_memmap(struct Page *base, size_t n);

static struct Page *buddy_alloc_pages(size_t n);

static size_t buddy_nr_free_pages(void);
```

允许新增模块内部的 `static` 辅助函数、管理结构、宏和状态变量。具体数据结构自行设计，但必须说明字段语义和容量边界。

本阶段不定义缺少真实释放功能的 `buddy_pmm_manager` 实例，不使用占位的释放或检查函数完成注册。

必须确定并记录以下跨模块约定，供后续释放功能使用：

- 对于正整数请求 `n`，实际分配页数为不小于 `n` 的最小 2 的幂，记为 `B`。
- 后续释放接口接收原申请返回的首页和原申请页数 `n`，回收整个 `B` 页块。
- 保留足够的管理信息，使后续释放功能能够识别有效分配、实际块大小和原申请大小，调用者无须访问私有结构。

##### [SPECIFICATION]

**一、`buddy_init()`：建立空的管理状态**

**Pre-Condition：**

首次使用 Buddy 分配器，或处于允许重新初始化且不存在待保留分配的状态。

**Post-Condition：**

内部管理结构处于一致的空状态，空闲页数为 0。未登记任何内存时，合法的分配请求返回 `NULL`。

**Requirements：**

不得将尚未交给本分配器的物理页自动视为空闲页，不得改写其他分配器的管理状态。

**二、`buddy_init_memmap(base, n)`：接收可管理物理页**

**Pre-Condition：**

管理结构已经初始化；`n > 0`；传入范围位于有效的页描述符数组中，各页处于待交接的保留状态，且未被本分配器重复登记。

**Post-Condition：**

传入范围内的全部页面被纳入管理，引用计数清零，解除待交接的保留状态；空闲页数准确增加 `n`。

空闲块大小均为 2 的幂，满足所采用伙伴关系的对齐要求。各块互不重叠，完整覆盖传入范围，不包含范围之外的页面。

**Case 1：**

页数不是 2 的幂，或起始位置不满足较大块的对齐条件时，仍须完整管理这些页面，不得丢弃尾部或向外扩大范围。

**Case 2：**

多次登记互不重叠的合法范围时，保留此前的管理状态，不得把未登记的间隔纳入空闲块。

**Requirements：**

明确伙伴关系使用的地址或页号基准，并始终保持一致。不得对 `struct Page *` 的数值直接进行伙伴地址计算。

**三、`buddy_alloc_pages(n)`：分配连续物理页**

**Pre-Condition：**

管理结构已初始化，内部状态一致；调用者请求正整数页数 `n`。

**Post-Condition：**

成功时返回一个真实物理块的首页描述符。该块包含 `B` 个连续物理页，满足对应块大小的对齐要求，与其他尚未释放的分配不重叠。

整个 `B` 页块从空闲资源中移除，空闲页数减少 `B`。块内各页不得仍被标记为空闲块首页，引用计数保持为 0。

**Case 1：**

存在能够满足请求的空闲伙伴块时，完成分配；允许从较大的块取得所需空间，剩余页面仍须得到正确管理。

**Case 2：**

空闲页总量不足，或者总量足够但不存在能够满足请求的块时，返回 `NULL`；原有分配、空闲块和计数保持不变。

**Case 3：**

请求过大，所需块大小可能溢出或超过支持范围时，安全返回 `NULL`，不得发生整数回绕或越界访问。

**Requirements：**

`n == 0` 属于前置条件违规，使用工程断言明确拒绝，并在修改管理状态前完成检查。

必须实现真实的伙伴块管理，不能用普通连续区间分配后仅将请求数向上取整来冒充 Buddy。具体数据结构和实现步骤自行设计。

**四、`buddy_nr_free_pages()`：查询空闲物理页数**

**Pre-Condition：**

管理结构已经初始化。

**Post-Condition：**

返回当前所有空闲块包含的物理页总数，而非块数量。查询不改变管理状态。

**五、编译检查与阶段边界**

使用工程实际的交叉编译器、头文件路径和编译选项，对新增源文件进行独立编译检查，并报告实际命令和结果。不得依靠补写 Best-Fit 或屏蔽其代码完成检查。

本阶段不运行 `make qemu`、`make grade`，不新增测试函数；释放与合并、内核接入、算法测试分别在后续阶段完成。



##### AI 输出

**第 1 块已完成，共 3 个实现块：Buddy 初始化、内存登记、页分配和空闲页计数已写入 D 盘，独立编译通过。** 结果如下：

1. **新增了两个文件。**

   - [buddy_pmm.c](../code/kern/mm/buddy_pmm.c)：管理状态和四个接口的实现。
   - [buddy_pmm.h](../code/kern/mm/buddy_pmm.h)：记录后续释放、接入时需要遵守的约定。

   两个文件均包含 `lab2 2413646` 注释。通过修改前后的文件哈希核对，**原有 39 个文件均未改变**。

2. **初始化会完整接收可用页。**

   `buddy_init()` 建立空的管理状态；`buddy_init_memmap()` 将传入物理页组织为符合对齐要求的伙伴块。

   实现以**物理页号**计算对齐和伙伴关系，支持非 2 的幂页数及分次登记，并用私有状态区分已登记页面和未登记区域。

3. **分配会拆分较大块，并记录实际占用。**

   `buddy_alloc_pages(n)` 寻找能够满足请求的空闲块，必要时拆分，将剩余块放回空闲链表。

   例如申请 3 页，实际分配 4 页，同时记录原申请数 `3` 和实际块阶数。空闲页计数减少 `4`，为下一块完整释放这 4 页保留依据。`buddy_nr_free_pages()` 返回空闲物理页总数。

4. **完成了编译检查，没有执行功能测试。**

   使用 `os` 环境中的 **RISC-V GCC 10.2.0**，按工程编译选项单独编译新增源文件，退出码为 `0`；产物为：

   ```text
   /tmp/lab2-buddy-stage1-2413646.o
   ```

   已核对该文件为 **ELF64、RISC-V 可重定位目标文件**。这说明新增源文件通过编译，算法行为仍须由第二大部分的测试验证。

   本轮未实现释放接口、未接入内核，未运行 `make qemu` 或 `make grade`。



#### 小模块二：物理页释放与伙伴合并

##### [PROMPT]

**任务**

在已经完成的 Buddy 初始化与分配模块基础上，实现物理页释放与伙伴合并，使已分配的物理块能够完整回收并再次参与分配。

工程代码目录为 `D:\OS-Lab\projects\lab2\code`，在 WSL 中对应 `/mnt/d/OS-Lab/projects/lab2/code`。

本次修改范围：

- `kern/mm/buddy_pmm.c`：新增释放接口，按需复用或调整内部空闲块管理辅助函数。
- `kern/mm/buddy_pmm.h`：仅在必要时更新阶段说明及释放约定，不暴露私有管理状态。

**操作要求**

必须修改工程中的真实文件。修改前读取当前 Buddy 代码，简要说明释放合法性、块大小恢复和伙伴合并的设计，再完成实现。

保留上一模块的初始化、分配接口及其行为；辅助函数如需调整，应说明原因，不得重新实现整套分配器。不得修改 Makefile、公共 `struct Page`、内核分配器选择、Best-Fit 或其他组员负责的代码。本阶段不注册 `buddy_pmm_manager`，不得添加占位接口完成注册。

注意有“lab2”的注释，代码中所有需要完成的地方（challenge 除外）都有“lab2”和“YOUR CODE”的注释，请在提交时特别注意保持注释，并将“YOUR CODE”替换为自己的学号，并把所有标有对应注释的部分填上正确的代码。

上述要求仅应用于本次 Buddy 释放与合并任务对应的部分。我的学号是 **2413646**；保留已有标记，新增实现按相同约定添加 `lab2 2413646` 注释，不修改其他任务的标记。

**输出要求**

说明修改位置、释放与合并的行为、与上一模块的兼容关系及编译结果。

本阶段仅进行独立编译检查，不设计、编写或运行功能测试，不运行 `make qemu` 或 `make grade`。专项测试由第二大部分的 Prompt 统一驱动，不得将编译成功表述为算法正确或内核接入完成。

##### [RELY]

以工程当前源码为准，先核对以下最小上下文；如与记录不一致，先说明差异，不得凭记录覆盖现有代码。

**现有接口与分配约定**

`kern/mm/buddy_pmm.c` 已实现：

```c
static void buddy_init(void);
static void buddy_init_memmap(struct Page *base, size_t n);
static struct Page *buddy_alloc_pages(size_t n);
static size_t buddy_nr_free_pages(void);
```

申请正整数 `n` 页时，实际占用不小于 `n` 的最小 2 的幂页数 `B`。申请成功后，整个 `B` 页块均属于该次分配，空闲页数减少 `B`。本模块必须接受原返回首页与原申请数 `n`，回收整个块；不支持拆开一次分配进行部分释放。

**现有私有状态**

```c
enum buddy_page_state {
    BUDDY_UNMANAGED = 0,
    BUDDY_FREE_INTERIOR,
    BUDDY_FREE_HEAD,
    BUDDY_ALLOC_HEAD,
    BUDDY_ALLOC_INTERIOR
};

struct buddy_page_info {
    size_t requested;
    unsigned int order;
    enum buddy_page_state state;
};
```

`buddy_info` 按 `page - pages` 索引，与页描述符一一对应。已分配块首页记录原申请数 `requested` 和实际阶数 `order`；其余页面为 `BUDDY_ALLOC_INTERIOR`。空闲块首页为 `BUDDY_FREE_HEAD`，其余页面为 `BUDDY_FREE_INTERIOR`；未登记页面为 `BUDDY_UNMANAGED`。

现有管理变量为 `buddy_free_lists`、`buddy_free_count`、`buddy_max_order` 和 `buddy_initialized`。元数据容量由 `BUDDY_PAGE_CAPACITY` 限制，该宏根据平台物理内存上界与 `DRAM_BASE`、`PGSIZE` 计算。

空闲块首页的 `PageProperty` 标志为真，`property` 表示块包含的页数；分配块各页的该标志为假，`property` 为 0。`PageReserved` 仍表示保留页，`ref` 仍表示物理页引用计数，不得挪作内部状态。

**伙伴关系与可复用能力**

现有实现使用绝对物理页号：`page2ppn(page) = page - pages + nbase`。`order` 阶块含有 `2^order` 页，起始物理页号满足对应对齐，其伙伴页号为 `pfn XOR (2^order)`。不得使用页描述符的地址数值代替物理页号。

合法描述符数量为 `npage - nbase`，不是 `npage`。有效物理页号上界为 `npage`，且私有元数据访问不能超过 `BUDDY_PAGE_CAPACITY`。

现有内部辅助函数如下，复用前应读取函数体及其副作用：

```c
static void buddy_add_block(struct Page *base, unsigned int order);
static void buddy_remove_block(struct Page *base);
static void buddy_register_block(struct Page *base, unsigned int order);
```

其中 `buddy_register_block()` 已具备登记空闲块时的伙伴合并能力，但它不是对外的释放接口，不负责验证原分配记录或增加空闲页计数。可以在保持初始化行为的前提下复用、提取或调整该内部能力，避免维护两套不一致的伙伴规则。

当前 PMM 在启动阶段串行调用；本模块沿用此前的串行使用条件，不引入并发调用支持。

##### [GUARANTEE]

必须新增并实现与 `pmm_manager.free_pages` 类型兼容的接口：

```c
static void buddy_free_pages(struct Page *base, size_t n);
```

必须保持已有四个接口的签名和约定不变，不改变“申请 `n` 页、实际占用 `B` 页、使用原申请数 `n` 完整释放”的契约。

允许新增模块内的 `static` 辅助函数，或对现有辅助函数进行必要调整；不得改变公共 PMM 接口或 `struct Page`。不新增测试函数，不注册缺少后续检查接口的管理器实例。

##### [SPECIFICATION]

**一、`buddy_free_pages(base, n)`：完整回收一次分配**

**Pre-Condition：**

Buddy 已初始化，内部状态一致。`base` 为本分配器某次成功分配返回且尚未释放的首页，`n > 0` 且等于该次原申请页数。实际分配块内各页的引用计数均为 0，不再被调用者使用，也不存在尚未撤销的映射或引用。

**Post-Condition：**

该次实际占用的整个 `B` 页块被完整回收，原分配记录失效，空闲页数准确增加 `B`，而非仅增加 `n`。其他未释放的分配保持不变，其元数据和物理页内容不被改写。

释放后的空闲结构与页面状态一致：只有空闲块首页具有 `PageProperty`，首页 `property` 为实际空闲块页数，内部页不保留过期的首页标记、阶数或原申请记录。回收不得把页面重新标为保留页。

**Case 1：无可合并伙伴。**

伙伴尚在使用、未登记、位于管理范围之外或阶数不同等情况下，释放块仍须作为合法空闲块保留，能够再次参与分配；不得为形成大块而吸收这些页面。

**Case 2：存在可合并伙伴。**

只有满足现有物理页号伙伴关系、阶数相同且整个块处于空闲状态的两个块，才允许合并。合并结果仍满足块大小和对齐约束；只要存在继续合法合并的条件，应最终形成相应的大块，且不能超过支持的最大阶数。

合并只改变空闲块的组织方式，不额外增加空闲页数。地址相邻但不具有伙伴关系的块不得合并。

**Case 3：申请页数不是 2 的幂。**

例如此前申请 3 页、实际占用 4 页，调用 `buddy_free_pages(base, 3)` 必须完整回收 4 页，并使计数增加 4。不得泄漏额外页面，也不得要求调用者改传 4。

**Case 4：释放参数或分配状态非法。**

对空指针、零页数、无效或不对齐的描述符指针、越界范围、未登记或保留页面、非分配首页、重复释放等情况，使用工程断言明确拒绝。


**Requirements：**

按阶数计算块大小前确认阶数有效，范围计算不得整数溢出。

断言终止执行前不应已发生部分释放。对未撤销映射等无法仅凭本模块状态可靠判断的条件，明确保留为调用者责任，不得声称已检测所有悬空引用。


**二、与上一模块的兼容性**

**Pre-Condition：**

上一模块的初始化、登记、分配与查询接口继续使用原有契约。

**Post-Condition：**

释放后的物理块可以被现有分配接口重新取得；反复分配和释放在语义上不丢页、不重复归还、不重叠分配。

**三、编译检查与阶段边界**

使用当前工程的交叉编译器、头文件路径和编译选项，独立编译修改后的 `buddy_pmm.c`，报告实际命令及结果。不得代做 Best-Fit、修改 Makefile 或运行完整构建来绕过阶段边界。

本阶段不设计、编写或运行算法测试，不运行内核；完整功能验证由第二大部分的测试 Prompt 完成。不得用成功日志替代实现或测试证据。



##### AI 输出

**第 2 块已完成，共 3 块：释放与伙伴合并代码已写入，并通过独立编译检查。** 本次只修改了 [buddy_pmm.c](../code/kern/mm/buddy_pmm.c) 和 [buddy_pmm.h](../code/kern/mm/buddy_pmm.h)，其他源文件未改变。

1. **新增了完整释放接口。**

   ```c
   static void buddy_free_pages(struct Page *base, size_t n);
   ```

   先验证指针范围、分配首页、原申请数、实际阶数及整块页面状态，通过后才修改管理结构。重复释放、部分释放、申请数不匹配或引用计数非零等情况会被断言拒绝。

2. **释放按实际占用回收。**

   例如申请 3 页、实际占用 4 页，释放时仍传入原申请数 `3`。代码读取保存的阶数，回收整个 4 页块，并使空闲页计数增加 `4`。

3. **合并复用了已有的伙伴规则。**

   回收块交给 `buddy_register_block()`，与同阶、空闲的合法伙伴逐级合并。合并本身不重复增加空闲页计数。原有初始化、分配和查询函数保持不变，新增代码带有 `lab2 2413646` 注释。

4. **独立编译通过，功能测试尚未运行。**

   在 `os` 环境、实验二的 `code` 目录执行：

   ```bash
   riscv64-unknown-elf-gcc \
     -mcmodel=medany -std=gnu99 -Wno-unused -Werror \
     -fno-builtin -Wall -O2 -nostdinc -fno-stack-protector \
     -ffunction-sections -fdata-sections -g \
     -Ilibs/ -Ikern/debug/ -Ikern/driver/ -Ikern/mm/ -Ikern/arch/ \
     -c kern/mm/buddy_pmm.c -o /tmp/lab2-buddy-stage2-2413646.o
   ```

   编译退出码为 `0`，产物已确认为 RISC-V ELF64 目标文件。本轮未接入内核，未执行 `make qemu`、`make grade` 或功能测试。




#### 修改调整：改用块首页管理，删除冗余状态和逐页操作

##### [PROMPT]

修改 `D:\OS-Lab\projects\lab2\code\kern\mm\buddy_pmm.c` 和 `buddy_pmm.h`，将模块一、二改为块首页管理。本段覆盖旧的逐页状态和原申请数检查约定。

先读取实际代码并说明表示方式，再修改真实文件。保留公共 `Page`、PMM 接口及其他模块，注释使用 `lab2 2413646`。本阶段只独立编译，不运行内核或功能测试。

##### [RELY]

框架 `Page` 已有 `ref`、`flags`、`property`、`page_link`；`PageReserved` 表示保留页，`PageProperty` 表示空闲块首页，引用计数不能挪作他用。

描述符数量为 `npage - nbase`，通过 `page2ppn()` 得到物理页号，用统一的物理页号基准确定对齐和伙伴。

修订前的 `buddy_info[]` 为每页保存 `requested`、`order` 和五状态枚举，正常分配与释放会逐页检查或重写。各阶空闲链表和空闲页总数可继续使用。

##### [GUARANTEE]

保持以下签名：

```c
static void buddy_init(void);
static void buddy_init_memmap(struct Page *base, size_t n);
static struct Page *buddy_alloc_pages(size_t n);
static void buddy_free_pages(struct Page *base, size_t n);
static size_t buddy_nr_free_pages(void);
```

删除 `buddy_page_info`、`buddy_info[]`、旧五状态枚举及原申请数记录。复用 `Page` 字段，只维护必要的首页身份和实际块大小，不再记录内部页归属。

实际块大小只保存页数，不单独保存 `order`；需要阶数或链表下标时用移位临时计算。正常分配、释放不得扫描或重写整块页面，也不得将扫描隐藏在辅助函数中；初始化允许逐页建立基础状态。

##### [SPECIFICATION]

**初始化与登记**

**Pre-Condition：** 输入范围合法且未重复登记。

**Post-Condition：** 完整管理所有交接页面，支持非规则起点、非 2 的幂页数及多次登记，保留页和未登记间隔不被纳入块。

**分配**

**Pre-Condition：** 管理器已初始化，`n > 0`，内部状态一致。

**Post-Condition：** 成功返回满足对齐的连续 `B` 页块，`B` 为不小于 `n` 的最小 2 的幂；空闲页数减少 `B`，不同分配不重叠。失败返回 `NULL`，状态不变。

**释放与合并**

**Pre-Condition：** `base` 是有效分配首页，调用者已解除整块引用和映射；`n` 向上取整后的块大小与实际记录相同。

**Post-Condition：** 整块回收，空闲页数只增加一次 `B`；仅合并同大小的空闲伙伴，移除旧首页标记和链表节点，登记最终块。非首页不能残留有效旧首页记录。

**Case 1：** 申请 3 页得到 4 页块后，传 `3` 或 `4` 均可完整释放，不支持部分释放。

**Case 2：** 拒绝空指针、零页数、越界、保留页、无效首页、重复释放及块大小不匹配；修改前完成可由范围和首页判断的检查。

**Requirements：** 先验证指针再解引用，大小运算不得溢出。保留首页引用检查，不声称能检测任意内部页引用或映射，不通过清零引用掩盖误用。报告管理信息变化、实际编译命令和结果，不以编译代替功能测试。



##### AI 输出

**修订已执行：Buddy 现在按块首页管理，原来的逐页状态结构和正常分配、释放中的整块遍历已删除，独立编译通过。**



1. **Prompt 已补充“不保存 `order`”。**

   新要求位于 [buddy_prompt1.md 第 444 行](materials/originals/buddy/buddy_prompt1.md)：

   > 实际块大小只在块首页保存页数，不单独保存 `order`；需要阶数或空闲链表下标时，使用移位临时计算。

   你在执行期间对文档的手动修改也已保留。

2. **冗余结构已整套删除。**

   [buddy_pmm.c](../code/kern/mm/buddy_pmm.c) 中已删除 `struct buddy_page_info`、`buddy_info[]`、五状态枚举及原申请数记录。

   当前直接使用 `Page` 中的字段：

   ```text
   空闲首页：PageProperty 为真，property 保存块页数
   分配首页：内部标志位为真，property 保存块页数
   非首页：  没有上述首页标志，property 为0
   ```

   `buddy_list_index()` 通过右移临时计算链表下标，没有单独存储阶数。

3. **正常分配、释放只处理相关首页。**

   [分配函数](../code/kern/mm/buddy_pmm.c) 查找、拆分块后记录分配首页；[释放函数](../code/kern/mm/buddy_pmm.c) 核对首页、实际块大小和参数，再合并伙伴。

   申请 3 页得到 4 页块后，传 `3` 或 `4` 都可以完整释放。初始化登记阶段仍保留逐页检查和初始状态设置。

4. **编译与修改范围已核对。**

   使用 GCC 10.2.0 和工程选项执行：

   ```bash
   riscv64-unknown-elf-gcc \
     -mcmodel=medany -std=gnu99 -Wno-unused -Werror \
     -fno-builtin -Wall -O2 -nostdinc -fno-stack-protector \
     -ffunction-sections -fdata-sections -g \
     -Ilibs/ -Ikern/debug/ -Ikern/driver/ -Ikern/mm/ -Ikern/arch/ \
     -c kern/mm/buddy_pmm.c -o /tmp/lab2-buddy-head-2413646.o
   ```

   编译退出码为 `0`。另加 `-fkeep-static-functions` 做了代码生成检查，确认尚未注册的静态函数也能生成目标代码；该目标文件中的私有管理变量合计 **1036 字节，约 1 KiB**，不含框架已有的 `Page` 数组。

   源码只修改了两个 Buddy 文件，功能测试尚未运行。

**下一步：审阅新的分配、释放函数，确认块级表示后，再执行模块三的内核接入。**


#### 小模块三：分配器注册、构建选择与内核启动接入

##### [PROMPT]

**任务**

将已经实现初始化、分配、释放和伙伴合并的 Buddy 模块接入 uCore 公共物理内存管理框架，使内核能够通过独立构建选项选择 Buddy，并在 QEMU 中完成物理内存初始化和后续启动流程。

工程代码目录为 `D:\OS-Lab\projects\lab2\code`，在 WSL 中对应 `/mnt/d/OS-Lab/projects/lab2/code`；使用 WSL 发行版 `os`。

本次修改范围：

- `kern/mm/buddy_pmm.c`：定义 Buddy 管理器实例，绑定已经实现的接口。
- `kern/mm/buddy_pmm.h`：声明管理器实例，更新接入阶段说明。
- `kern/mm/pmm.c`：按构建选项选择分配器，兼容尚未安装的检查回调。
- `Makefile`：提供 Buddy 构建选择，处理该模式的源文件集合和必要构建参数。

**操作要求**

必须修改工程中的真实文件。修改前读取当前代码，简要说明注册方式、构建选择、检查回调的阶段边界和启动检查命令，再完成接入。

保持 Buddy 算法及其既有接口契约，不重写前两模块；保留现有 `page_init()` 的物理内存探测、保留区排除和描述符布局逻辑，不改变公共 `struct Page` 或 `struct pmm_manager`。不得代做 Best-Fit、改动其他组员的实现或删除、修改原有测试。

注意有“lab2”的注释，代码中所有需要完成的地方（challenge 除外）都有“lab2”和“YOUR CODE”的注释，请在提交时特别注意保持注释，并将“YOUR CODE”替换为自己的学号，并把所有标有对应注释的部分填上正确的代码。

上述要求仅应用于本次 Buddy 接入任务对应的部分。我的学号是 **2413646**；新增或修改的 Buddy 接入代码按文件语法添加 `lab2 2413646` 注释，保留其他任务的注释和学号，不批量替换其他模块的 `YOUR CODE`。

**输出要求**

说明修改位置、分配器选择方法、实际构建与启动命令、观察到的启动输出和未完成的验证内容。

本阶段可以完整编译、链接内核并运行 QEMU 检查接入与启动，但不设计、编写或执行 Buddy 分配与释放的专项测试，不运行 `make grade`。算法测试统一由第二大部分的 Prompt 驱动。启动成功不得表述为分配算法已通过测试。

##### [RELY]

以当前工程为准，修改前核对以下上下文；如与记录不一致，先说明差异，不覆盖现有工作。

**管理器接口与已有实现**

`kern/mm/pmm.h` 中的公共结构为：

```c
struct pmm_manager {
    const char *name;
    void (*init)(void);
    void (*init_memmap)(struct Page *base, size_t n);
    struct Page *(*alloc_pages)(size_t n);
    void (*free_pages)(struct Page *base, size_t n);
    size_t (*nr_free_pages)(void);
    void (*check)(void);
};
```

`buddy_pmm.c` 已实现以下模块内部接口：

```c
static void buddy_init(void);
static void buddy_init_memmap(struct Page *base, size_t n);
static struct Page *buddy_alloc_pages(size_t n);
static void buddy_free_pages(struct Page *base, size_t n);
static size_t buddy_nr_free_pages(void);
```

申请 `n` 页实际占用不小于 `n` 的最小 2 的幂页数 `B`，释放使用原返回首页和原申请数 `n`，完整回收 `B` 页；空闲页计数按实际占用变化。不得在接入时改变这套约定。

Buddy 使用静态私有元数据，其空间属于内核镜像的 BSS。现有 `page_init()` 根据链接符号 `end` 放置页描述符并排除已用区域，接入后应继续使用这套真实布局，不得硬编码原先的内核结束地址或空闲页数量。

**公共初始化与调用链**

`pmm.c` 当前相关接口为：

```c
static void init_pmm_manager(void);
static void check_alloc_page(void);
void pmm_init(void);

struct Page *alloc_pages(size_t n);
void free_pages(struct Page *base, size_t n);
size_t nr_free_pages(void);
```

`init_pmm_manager()` 当前选择 `default_pmm_manager`，打印名称并调用其 `init()`。`pmm_init()` 随后调用 `page_init()`，由其把可用页面交给所选管理器，再调用 `check_alloc_page()`，最后输出启动页表相关地址。

`alloc_pages()`、`free_pages()`、`nr_free_pages()` 是公共分派接口，调用 `pmm_manager` 对应回调。必须通过这套框架接入，不能在启动代码中绕过它另建一套分配路径。

当前 `check_alloc_page()` 无条件调用 `pmm_manager->check()` 并打印成功信息。Buddy 尚未生成测试函数，因此不能直接沿用无条件调用，也不能用空检查函数假装完成验证。

**构建与现有未完成部分**

`best_fit_pmm.c` 尚未实现，Buddy 构建时将其排除，保留文件及原有测试。

保留现有工具链、编译和链接参数、QEMU 启动配置及默认分配器，只增加必要的 Buddy 选择与构建配置。

##### [GUARANTEE]

必须新增以下管理器实例及其头文件声明：

```c
/* buddy_pmm.h */
extern const struct pmm_manager buddy_pmm_manager;

/* buddy_pmm.c */
const struct pmm_manager buddy_pmm_manager = {
    /* 绑定现有真实接口；字段行为见下方规格。 */
};
```

头文件应具有有效的类型声明或包含关系。五个已实现的算法接口继续保持模块内部可见，不暴露私有管理数组。

按需修改以下既有函数的行为，保持签名：

```c
static void init_pmm_manager(void);
static void check_alloc_page(void);
```

提供显式构建参数 `PMM=buddy`，用于编译及运行 Buddy 模式；不传该参数时维持当前默认分配器选择。构建选项通过何种内部宏传递、如何组织源文件集合，由实现者设计并说明。

本阶段 Buddy 管理器的 `.check` 为 `NULL`，由公共分派处明确处理未安装检查回调的情况。不得新增空的 `buddy_check()`，不得借用 First-Fit 或 Best-Fit 的检查函数验证 Buddy。

##### [SPECIFICATION]

**一、管理器注册与选择**

**Pre-Condition：** Buddy 接口已实现，构建选项已确定。

**Post-Condition：** Buddy 模式绑定真实的五个功能接口，并在登记可用页前完成初始化；其他模式保持原有选择。

**二、`check_alloc_page()`**

**Pre-Condition：** 管理器初始化和可用页登记已完成。

**Post-Condition：** 检查回调非空时调用，正常返回后报告成功；为空时跳过并提示未安装，不误报通过。

**三、构建选择**

**Pre-Condition：** 使用原工具链，清理或强制重建与当前模式不一致的旧目标文件。

**Post-Condition：** `make PMM=buddy` 生成使用 Buddy 的内核镜像，该模式排除未完成的 Best-Fit 源文件；保留其他模式、原配置和测试规则。

**四、内核启动**

**Pre-Condition：** Buddy 模式的最新镜像已生成。

**Post-Condition：** 启动日志显示 Buddy、正数空闲页数和原有页表地址输出，无 panic；内存保留区及页面布局沿用现有初始化逻辑。

**Requirements：** 用 `make PMM=buddy qemu` 检查并记录启动输出，说明主动结束或超时结束方式。本阶段不执行算法专项测试。

##### AI 输出

**模块三已完成，3/3 个实现模块均已完成。Buddy 已接入内核，并在 QEMU 中完成物理内存初始化。**

1. **构建选择已生效。**

   使用 `PMM=buddy` 选择 Buddy，仅在此模式排除未完成的 Best-Fit 源文件。原工具链、编译和链接参数、输出目录、QEMU 启动参数均已核对保留。

2. **编译、链接和启动检查通过。**

   实际执行了：

   ```bash
   make -B PMM=buddy -j2
   timeout --signal=TERM 12s make PMM=buddy qemu
   ```

   启动日志包含：

   ```text
   memory management: buddy_pmm_manager
   Free physical pages: 31929
   PMM check skipped: no check callback installed.
   satp virtual address: 0xffffffffc0205000
   satp physical address: 0x0000000080205000
   ```

   内核继续到了初始化末尾，没有观察到 panic。QEMU 在 12 秒后由超时命令结束，退出码 `124` 对应这次主动限时结束。

**目前确认的是构建与内核接入可用；分配、释放和合并的正确性尚未通过专项测试验证。**

**下一步：完成第二大部分的测试 Prompt。**



### Buddy System 测试 Prompt 记录

#### 一：测试如何设计

##### [PROMPT]

请阅读当前 uCore Buddy 实现，先回答以下问题，暂不编写测试代码或修改工程：

1. 除初始化、分配、释放、伙伴合并等核心功能，分配合法且不重叠，非法参数与越界、溢出处理之外，还应测试哪些核心行为和边界？请说明各项的触发条件、预期结果，以及能发现什么错误，并区分必测项与扩展项。
2. 后续编写测试文件和测试函数时应注意什么？请结合本工程说明文件位置、接口选择、构建与启动接入、测试资源回收、断言异常隔离，以及如何避免测试结果受执行顺序或旧构建产物影响。

##### [RELY]

工程位于 `D:\OS-Lab\projects\lab2\code`，WSL 路径为 `/mnt/d/OS-Lab/projects/lab2/code`，发行版为 `os`。

主要参考 `kern/mm/buddy_pmm.c`、`buddy_pmm.h`、`pmm.c`、`pmm.h`、`memlayout.h` 和 Makefile；涉及原有评分流程时再读取 `tools/inject.py`、`tools/grade.sh`，以当前代码为准。

当前约定：

- 采用块首页管理，实际块页数保存在 `Page.property`；不保存原申请数、阶数或内部页归属。
- 申请 `n` 页实际取得不小于 `n` 的最小 2 的幂页数 `B`；释放参数取整后应与实际块大小一致，如申请 3 页得到 4 页块后，传 `3` 或 `4` 均完整释放，不支持部分释放。
- 零页请求和非法释放触发断言；分配资源不足或请求过大返回 `NULL`。`size_t` 为无符号类型，参数测试应区分接口可表达的值及其转换结果。
- 公共接口为 `alloc_pages(n)`、`free_pages(base, n)`、`nr_free_pages()`；`buddy_pmm_manager` 已接入，内部算法函数是 `static`，`.check` 尚为 `NULL`。
- 使用 `PMM=buddy` 构建，已通过编译和启动检查，尚未验证算法。Buddy 模式排除未完成的 Best-Fit，原有评分不是 Buddy 专项测试。

##### [GUARANTEE]

本轮只交付两份简短的文字清单：测试覆盖建议、测试实现注意事项。使用文字列表，不使用表格，不生成函数实现或修改分配器。

##### [SPECIFICATION]

**测试覆盖建议：** 根据真实接口契约提出可执行、可判断结果的场景，不能只列测试名称。说明如何分别确认块分配正确、伙伴关系正确和资源完整恢复，而非仅确认程序未崩溃。

**测试实现注意事项：** 说明如何通过真实接口取得和归还页面、如何形成独立预期结果，以及如何处理会终止内核的断言测试。不得通过伪造成功日志、放宽断言或重置分配器来掩盖错误。

区分可以从接口或块首页验证的行为，与调用者负责的内部页引用、映射生命周期；不要重新要求正常分配与释放逐页记录状态。测试确有必要时可以遍历结果进行核对。

保留原有配置、其他分配器和测试；分析新增测试位置、构建接入是否与现有评分注入机制冲突。测试应在结束后恢复可回收资源，并有明确的失败定位信息。



##### AI 输出

**测试应重点证明三件事：分配得到正确且独占的块，释放只合并合法伙伴，全部归还后资源完整恢复。** 仅检查“不崩溃”或空闲页数相等，都不足以证明算法正确。


**一、测试覆盖建议**

以下为必测项，包含你提出的基本要求及需要补充的行为：

1. **页数取整与计数准确。**
   分别申请 1、2、3、5 页，预期实际占用 1、2、4、8 页；检查地址对齐、不重叠及空闲页数变化。申请 3 页后，分别在独立用例中用 `3`、`4` 释放，都应完整恢复。这能发现取整错误、少扣页数和额外页泄漏。

2. **拆分、级联合并与旧首页失效。**
   构造可确认伙伴关系的小块，分别按不同顺序释放；预期能重新申请合并后的大块。还要构造“相邻但不是伙伴”的块，确认不会错误合并，并检查失效的小块首页标记已清除。这能发现漏合并、错合并及旧链表节点残留。

3. **耗尽、碎片与失败不改变状态。**
   分配到资源耗尽，确认再次申请返回 `NULL`；释放部分小块，形成“总空闲页数足够，但没有所需大块”的情况，大块申请仍应失败。失败前后计数和已有分配应保持不变；全部归还后应恢复基线，并能再次分配。

4. **管理范围与初始化边界。**
   在独立的初始化用例中，使用起点不满足大块对齐、页数不是 2 的幂，以及带间隔的多个合法范围。预期所有交接页都得到管理，保留页和未登记间隔不被分配。这能发现尾部丢页、越界组块和跨间隔合并。

5. **数据隔离及重复使用。**
   给同时持有的不同块写入不同内容，确认彼此不覆盖；释放其中一块并重新申请，不应破坏仍持有的其他块。多轮执行后计数应恢复。这比只比较返回指针更能发现实际物理范围重叠。

扩展项可加入固定随机种子的混合申请、乱序释放，以及更长时间的重复运行；先完成上述确定性用例。

**二、编写测试文件和测试函数的注意事项**

1. **独立测试文件，通过真实接口接入。**
   可考虑新增 `kern/mm/buddy_test.c`、`buddy_test.h`，使用公共 `alloc_pages()`、`free_pages()`、`nr_free_pages()`；测试模式下再将真实测试入口接到 `.check`。内部算法函数保持 `static`，不复制一份分配算法用于测试。

   当前 `tools/inject.py` 会检查 Makefile 是否包含 `kern/test`，直接使用该目录会与原评分注入冲突；需要避开，不能删除这个检查来迁就新测试。

2. **预期结果独立计算，场景条件先确认。**
   预期占用页数由输入独立计算，不能直接把 `property` 当作正确答案。连续两次申请也不保证取得伙伴，必须核对物理页号或建立受控场景。初始化测试应独立启动安排，不能向正在使用的管理器重复登记页面。

3. **每个正常用例归还自己取得的全部资源。**
   记录开始时的空闲页数，结束后核对恢复，再验证可重新分配；不能调用 `buddy_init()` 重置计数来伪装恢复。大量分配记录不要放在只有 **8 KiB** 的内核栈中，应使用有容量检查的存储。

   测试需要核对覆盖范围时可以遍历页面，这不要求分配器恢复逐页状态维护。

4. **会触发断言的用例单独运行。**
   当前 `__panic()` 打印信息后进入死循环，所以零页请求、重复释放、内部页误释放、错误块大小等负面用例，应分别在独立 QEMU 启动中执行。核对具体断言和用例标识，不能仅凭超时判为通过。

   `size_t` 是无符号类型：传入 `(size_t)-1` 实际是一个很大的正数；分配应返回 `NULL`。内部页的引用、映射未解除属于调用者责任，不能要求当前仅检查首页的释放函数检测所有内部页引用。

5. **明确构建模式和失败证据。**
   测试开关变化后强制重建，保留原工具链、QEMU 参数和评分脚本。每个用例输出名称、预期值、实际值，验证完成后才能报告通过。当前 `make grade` 检查初始化和 Best-Fit，不能作为 Buddy 专项测试结果。




#### 二：生成并运行 Buddy 专项测试

##### [PROMPT]

为当前 uCore 块首页版 Buddy 实现独立测试，验证正确性、边界处理及资源恢复。工程为 `D:\OS-Lab\projects\lab2\code`，在 WSL `os` 中对应 `/mnt/d/OS-Lab/projects/lab2/code`。

新增 `kern/mm/buddy_test.c`、`buddy_test.h` 和宿主运行脚本 `tools/run_buddy_tests.py`；仅按需修改 Makefile、`buddy_pmm.c` 的测试回调绑定，以及 `pmm.c` 中受测试开关控制的可用页登记入口。不得修改分配、释放、合并算法或原有评分脚本来使测试通过。

先读取真实代码，再直接修改文件、编译并运行测试。保留普通启动、工具链、编译和链接参数及 QEMU 配置；注释使用 `lab2 2413646`。测试只在显式开关下启用，输出真实结果与日志到 `report`，不进行 Git 操作。

##### [RELY]

公共接口为 `alloc_pages(n)`、`free_pages(base, n)`、`nr_free_pages()`；管理器 `buddy_pmm_manager` 的登记回调可用于初始化受控范围，内部算法函数保持 `static`。

申请实际占用最小的 2 的幂页数 `B`。释放参数只要向上取整为记录的 `B` 即可，例如申请 3 页后传 3 或 4 均完整归还。零页和非法释放断言失败；资源不足、超大请求返回 `NULL`。

实际块页数在首页 `property` 中，空闲首页有 `PageProperty`。伙伴按绝对物理页号计算。非首页不记录归属，不应要求分配器逐页更新状态。

`pages` 对应 `npage - nbase` 个描述符，`page2pa()` 将描述符转换为物理地址，内核映射通过 `va_pa_offset` 转换。测试数据只能写入真实取得的物理页，不能写到页描述符上。内核栈只有 8 KiB。

已有 `PMM=buddy` 构建选择，`.check` 尚为空。`__panic()` 输出断言后死循环；原评分注入器会拒绝 Makefile 中出现 `kern/test`。读取相关源码确认这些约束。

##### [GUARANTEE]

新增测试入口及测试专用登记入口：

```c
void buddy_check(void);
void buddy_test_init_memmap(struct Page *base, size_t n);
```

提供独立测试开关与用例选择，普通模式仍使用原登记路径且不运行测试。运行脚本负责强制重建、逐例启动、核对结果、结束 QEMU 和保存日志；结束后恢复普通 Buddy 镜像。

##### [SPECIFICATION]

**正常功能测试**

**Pre-Condition：** 真实物理页由现有内存初始化提供，已排除内核、描述符和测试静态数据。

**Post-Condition：** 验证取整、对齐、非重叠、精确计数、拆分、不同顺序释放、级联合并、旧首页失效、非伙伴不合并、耗尽与碎片失败、失败不改变状态、数据隔离及多轮恢复。预期页数独立于被测实现计算，不能只比较计数或检查不崩溃。

**初始化边界：** 在测试专用启动中，先用真实可用页构造非规则起点、非 2 的幂长度、相邻分次登记和未登记间隔；检查登记范围与实际可分配页面一致，再登记其余真实可用页。不重置管理器，不把已登记区域重复当成新内存。

**负面测试**

**Pre-Condition：** 每个预期断言用例独立启动，输入来自合法测试分配或明确的边界地址。

**Post-Condition：** 零页、空指针、重复释放、内部页释放、错误块大小、描述符不对齐或越界、保留页释放、首页引用非零及非法登记被明确拒绝。大无符号请求与零页断言区分；不要求检测任意内部页引用或映射。

**Requirements：** 核对用例标识、具体断言及其来源，不能把任意 panic 或超时视为通过。正常用例必须归还资源、恢复基线；大数组放在受保护的静态存储中。测试可以遍历分配结果，不得恢复算法内的逐页状态维护。

报告实际编译和启动结果、覆盖范围及限制。若发现算法缺陷，保留失败证据并报告原因，不放宽测试或伪造通过；测试本身有误时据实修正并说明。

##### AI 输出

**测试生成 Prompt 已追加并执行：7 组正常测试、15 个独立断言用例全部通过，普通 Buddy 镜像已恢复。**

当前进度：**实现 3/3 完成，专项测试完成。**

1. **Prompt 和结果已保存。**

   - [测试生成 Prompt](materials/originals/buddy/buddy_prompt2.md)
   - [测试结果与覆盖说明](materials/buddy-tests/buddy_test_results.md)
   - [正常测试原始日志](materials/buddy-tests/buddy_test_logs/normal.log)

   结果文档包含各用例日志链接，以及这些测试为什么能检验实现的说明。

2. **正常测试验证了实际行为。**

   覆盖取整、对齐、不重叠、准确计数、真实物理内存数据隔离、耗尽与碎片、伙伴合并、旧首页失效和多轮恢复。

   特别验证了：只释放一个受控的 8 页区域后，可以重新取得合并后的 8 页块；相邻但不是伙伴的小块不会被错误合并。最终输出：

   ```text
   [BUDDY] ALL PASS (7 groups)
   ```

3. **15 个非法操作均触发了对应断言。**

   包括零页、空指针、重复释放、内部页释放、错误大小、描述符不对齐与越界、保留页释放、首页引用非零和非法登记。每个用例独立启动，并核对了用例标识、断言来源及具体条件，没有把超时或任意 panic 当作通过。

   **分配、释放和合并算法没有为通过测试而修改。** 新增的是 [测试代码](../code/kern/mm/buddy_test.c)、[测试头文件](../code/kern/mm/buddy_test.h) 和 [运行脚本](../code/tools/run_buddy_tests.py)，以及受开关控制的接入代码。

4. **普通启动已恢复。**

   普通镜像仍有 **31929 个空闲页**，测试默认关闭。测试镜像中的记录数组和代码会占用额外空间，因此测试时可用页为 **31863**，测试结束已完整恢复该基线。

   首次运行曾因未加载工具链 `PATH` 而在编译前失败；改用 Bash 登录环境后通过，原错误日志已保留。本次结果属于 Buddy 专项测试，未运行原 `make grade`，也未验证并发和映射生命周期。

以后在已加载工具链的 WSL 终端中，可以重新运行，预计 1–2 分钟：

```bash
cd /mnt/d/OS-Lab/projects/lab2/code
python3 tools/run_buddy_tests.py
```




