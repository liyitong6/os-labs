# Buddy System 实现 Prompt 记录

## 小模块一：管理基础、可用页初始化与物理页分配

### [PROMPT]

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

### [RELY]

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

### [GUARANTEE]

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

### [SPECIFICATION]

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



### AI 输出

**第 1 块已完成，共 3 个实现块：Buddy 初始化、内存登记、页分配和空闲页计数已写入 D 盘，独立编译通过。** 结果如下：

1. **新增了两个文件。**

   - [buddy_pmm.c](D:/OS-Lab/projects/lab2/code/kern/mm/buddy_pmm.c)：管理状态和四个接口的实现。
   - [buddy_pmm.h](D:/OS-Lab/projects/lab2/code/kern/mm/buddy_pmm.h)：记录后续释放、接入时需要遵守的约定。

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



## 小模块二：物理页释放与伙伴合并

### [PROMPT]

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

### [RELY]

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

### [GUARANTEE]

必须新增并实现与 `pmm_manager.free_pages` 类型兼容的接口：

```c
static void buddy_free_pages(struct Page *base, size_t n);
```

必须保持已有四个接口的签名和约定不变，不改变“申请 `n` 页、实际占用 `B` 页、使用原申请数 `n` 完整释放”的契约。

允许新增模块内的 `static` 辅助函数，或对现有辅助函数进行必要调整；不得改变公共 PMM 接口或 `struct Page`。不新增测试函数，不注册缺少后续检查接口的管理器实例。

### [SPECIFICATION]

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



### AI 输出

**第 2 块已完成，共 3 块：释放与伙伴合并代码已写入，并通过独立编译检查。** 本次只修改了 [buddy_pmm.c](D:/OS-Lab/projects/lab2/code/kern/mm/buddy_pmm.c:214) 和 [buddy_pmm.h](D:/OS-Lab/projects/lab2/code/kern/mm/buddy_pmm.h)，其他源文件未改变。

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




## 修改调整：改用块首页管理，删除冗余状态和逐页操作

### [PROMPT]

修改 `D:\OS-Lab\projects\lab2\code\kern\mm\buddy_pmm.c` 和 `buddy_pmm.h`，将模块一、二改为块首页管理。本段覆盖旧的逐页状态和原申请数检查约定。

先读取实际代码并说明表示方式，再修改真实文件。保留公共 `Page`、PMM 接口及其他模块，注释使用 `lab2 2413646`。本阶段只独立编译，不运行内核或功能测试。

### [RELY]

框架 `Page` 已有 `ref`、`flags`、`property`、`page_link`；`PageReserved` 表示保留页，`PageProperty` 表示空闲块首页，引用计数不能挪作他用。

描述符数量为 `npage - nbase`，通过 `page2ppn()` 得到物理页号，用统一的物理页号基准确定对齐和伙伴。

修订前的 `buddy_info[]` 为每页保存 `requested`、`order` 和五状态枚举，正常分配与释放会逐页检查或重写。各阶空闲链表和空闲页总数可继续使用。

### [GUARANTEE]

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

### [SPECIFICATION]

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



### AI 输出

**修订已执行：Buddy 现在按块首页管理，原来的逐页状态结构和正常分配、释放中的整块遍历已删除，独立编译通过。**



1. **Prompt 已补充“不保存 `order`”。**

   新要求位于 [buddy_prompt1.md 第 444 行](D:/OS-Lab/projects/lab2/report/buddy_prompt1.md:444)：

   > 实际块大小只在块首页保存页数，不单独保存 `order`；需要阶数或空闲链表下标时，使用移位临时计算。

   你在执行期间对文档的手动修改也已保留。

2. **冗余结构已整套删除。**

   [buddy_pmm.c](D:/OS-Lab/projects/lab2/code/kern/mm/buddy_pmm.c) 中已删除 `struct buddy_page_info`、`buddy_info[]`、五状态枚举及原申请数记录。

   当前直接使用 `Page` 中的字段：

   ```text
   空闲首页：PageProperty 为真，property 保存块页数
   分配首页：内部标志位为真，property 保存块页数
   非首页：  没有上述首页标志，property 为0
   ```

   `buddy_list_index()` 通过右移临时计算链表下标，没有单独存储阶数。

3. **正常分配、释放只处理相关首页。**

   [分配函数](D:/OS-Lab/projects/lab2/code/kern/mm/buddy_pmm.c:163) 查找、拆分块后记录分配首页；[释放函数](D:/OS-Lab/projects/lab2/code/kern/mm/buddy_pmm.c:202) 核对首页、实际块大小和参数，再合并伙伴。

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


## 小模块三：分配器注册、构建选择与内核启动接入

### [PROMPT]

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

### [RELY]

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

### [GUARANTEE]

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

### [SPECIFICATION]

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

### AI 输出

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


