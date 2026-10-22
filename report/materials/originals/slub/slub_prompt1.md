# SLUB 实现 Prompt 记录


## 小模块一：通用接口、内核接入与上层分流

[PROMPT]

建立独立于算法的字节分配接口，内核调用 `kmem_init/kmalloc/kfree`，通过 `kmem_manager` 选择具体字节分配算法。SLUB 是该接口的一种实现，其再区分小对象 Cache 路径和大对象页面路径。先完成抽象边界、上层控制逻辑和全部下层接口契约，后续模块逐层补全。

新增 `code/kern/mm/kmalloc.h`、`kmalloc.c`；修改 `slub_pmm.c/.h`完成SLUB 上层分流
        ├─ Cache → Slab
        └─ 大对象页面路径。同时修改 `code/Makefile`、`code/kern/init/init.c`，完成增量开关和内核初始化接入。仅执行本模块，不修改原 `pmm.h/.c`，不生成测试。保留用户修改和相关 `lab2` 注释，已完成部分的 `YOUR CODE` 改为学号 `2413646`，新增代码使用 `lab2 2413646` 注释。

输出要求：执行完成后，将本模块的完成说明、实际文件和接口、验证结果及待完成内容输出，未执行的验证明确注明。

[RELY]

读取 `code/Makefile`、`kern/init/init.c`、`kern/mm/pmm.h`、`pmm.c`、`memlayout.h`、`mmu.h`，以及 `libs/defs.h`、`list.h` 和 `kern/debug/assert.h`。原有抽象为 `pmm_manager`，公共页接口如下：

```c
struct Page *alloc_pages(size_t n);
void free_pages(struct Page *base, size_t n);
size_t nr_free_pages(void);
```

`pmm.c` 将这些调用转发到当前 `pmm_manager`。当前 `PMM=buddy` 选择 `buddy_pmm_manager`，其他配置选择 `best_fit_pmm_manager`。Buddy 可能向上取整实际页数，归还时使用原申请描述符和页数即可。新字节接口与页接口分别定义，SLUB 通过公共页接口取得内存，不替换或重置现有页管理器，不修改 `Page.flags/property/ref/page_link`。

`PGSIZE=4096`，页接口按页计数，返回描述符；页面内容地址为 `page2pa(page) + va_pa_offset`。最终实现范围为单 CPU、串行调用。小对象规格为 `16、32、64、128、256、512、1024、2048`，Cache 描述符集中静态保存，每个 slab 使用一页；大对象使用连续页面，所有返回地址按 16 字节对齐。

[GUARANTEE]

在算法无关的 `kmalloc.h` 定义结构和公开接口，不把公共声明放进 SLUB 专用头文件：

```c
struct kmem_manager {
    const char *name;
    void (*init)(void);
    void *(*alloc)(size_t size);
    void (*free)(void *ptr);
};

void kmem_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
```

`kmalloc.c` 保存选中的管理器和必要的初始化状态，负责检查与转发。`slub_pmm.h` 包含通用头文件，仅向选择层提供：

```c
extern const struct kmem_manager slub_kmem_manager;
```

`slub_pmm.c` 实现管理器对应的三个私有回调，写出大小分流及释放归属分流的上层代码。确定必要私有结构，并为后续层完整声明以下接口、记录其行为；这些接口均为 `static`：

```c
static void slub_cache_init(void);
static struct slub_cache *slub_select_cache(size_t size);
static void *slub_cache_alloc(struct slub_cache *cache);
static void slub_cache_free(struct slub_slab *slab, void *ptr);

static struct slub_slab *slub_find_slab(const void *ptr);
static struct slub_slab *slub_create_slab(struct slub_cache *cache);
static void slub_destroy_slab(struct slub_slab *slab);
static void *slub_take_object(struct slub_slab *slab);
static void slub_return_object(struct slub_slab *slab, void *ptr);
static void *slub_alloc_large(size_t size);
static bool slub_free_large(void *ptr);
```

构建与启动接入也在本模块交付：Makefile 新增 `KMEM ?= 0`，仅接受 `0/1`，`1` 时定义 `USE_KMEM_SLUB`；`kmalloc.c` 始终提供公共符号，SLUB 算法源文件仅在开启时参与构建，SLUB 测试文件不接入。`kern_init` 仅在开启时于 `pmm_init()` 后调用 `kmem_init()`，只包含通用 `kmalloc.h`。保留现有工具链、页管理器选择、QEMU 和测试参数，不改变底层 `.check`。

[SPECIFICATION]

**Pre-Condition：** 原有页分配框架可用，新增字节分配功能尚未接入启动。

**通用接口：** `kmem_init` 初始化选中的字节管理器；`kmalloc/kfree` 只检查通用调用条件并转发，不判断具体 Cache 或 Slab。算法选择集中在 `kmalloc.c`，内核调用方只包含 `kmalloc.h`。新增另一种字节算法时无需修改调用方。管理器在初始化时确定，不支持存在未释放对象时切换。

**增量开关：** 构建变量 `KMEM=0/1`，默认 `0`，本模块完成 Makefile 和启动接入；值为 `1` 时定义 `USE_KMEM_SLUB`。本模块按宏编写通用选择逻辑：未定义时不引用 SLUB 符号，`kmem_init` 不做操作，`kmalloc` 返回 `NULL`，`kfree(NULL)` 不做操作，非空释放断言拒绝；开启后才选择 `slub_kmem_manager`。关闭不改变原页分配接口，不暗中回退到其他字节算法，不增加运行中切换机制。

**SLUB 上层：** 初始化调用 Cache 初始化接口；申请先处理零大小，再将小对象转入最小规格 Cache，大对象转入页面路径。释放先处理空指针，使用 `slub_find_slab` 查找已登记 slab；找到后交给 Cache 归还对象，否则交给大对象释放接口，均无合法归属时断言拒绝。初始化状态仅由通用层维护，SLUB 不重复维护同义状态。

**下层契约：** `slub_find_slab` 只查找已登记 slab，不随意解引用传入地址；找到所属对象区域后返回 slab，具体槽位边界和分配状态由归还接口验证。`slub_free_large` 对合法当前大对象入口完整释放并返回 `true`，无匹配返回 `false` 且不改状态。其余 Cache、Slab 接口分别承担初始化、规格选择、对象取还、创建与回收，后续模块沿用这些签名。

**边界与中间状态：** 开启后，零大小返回 `NULL`，空指针释放不做操作；正大小申请和非空释放要求已初始化，重复初始化断言拒绝。未实现的下层允许声明或明确的 `panic` 桩，不以固定成功或统一失败冒充实现；关闭开关的约定返回值与待实现桩必须区分。完成后列出各接口实际位置和待实现项，不调用桩验证功能，不自动执行下一模块。

**本模块验证：** 在工具链已加载的 WSL 中检查 `KMEM=0` 的构建和启动，确认新接口及接入代码不影响原内核。`KMEM=1` 可检查编译和链接，但下层未实现时不启动调用桩来冒充功能验证。结束时明确“接口与接入已完成、分配器尚未完成”；完整开启模式的启动验证安排在模块三补全下层之后。切换开关强制重建，不修改 Git 或自动提交。


### AI 输出

#### 完成内容

模块一完成通用接口、内核接入和 SLUB 上层骨架，分配器尚未完整实现。

- 新增 [kmalloc.h](../../code/kern/mm/kmalloc.h)、[kmalloc.c](../../code/kern/mm/kmalloc.c)：定义 `kmem_manager` 和 `kmem_init/kmalloc/kfree`，集中选择算法、检查初始化状态并转发调用。
- 完成 [slub_pmm.h](../../code/kern/mm/slub_pmm.h) 的管理器声明及 [slub_pmm.c](../../code/kern/mm/slub_pmm.c) 的上层分流。小对象调用 Cache 接口，大对象调用页面路径；Cache、Slab 和大对象下层暂用明确的 panic 桩。
- 修改 Makefile 和 `kern/init/init.c`：默认 `KMEM=0`，开启后按 `pmm_init → kmem_init` 初始化。原有页管理器和 `.check` 未修改。

#### 实际验证与待完成内容

在 WSL os 的 `code` 目录执行：

```bash
make -B -j2 PMM=buddy KMEM=0 BUDDY_TEST=0
make PMM=buddy KMEM=0 BUDDY_TEST=0 qemu
make -B -j2 PMM=buddy KMEM=1 BUDDY_TEST=0
```

关闭模式编译、链接和内核启动通过；开启模式仅检查编译、链接，通过后未运行待实现路径。没有进行对象分配释放测试。内部接口按上方 `[GUARANTEE]` 的签名生成，后续模块继续补全。

## 小模块二：Cache 管理与下层调用

[PROMPT]

基于模块一，在 `code/kern/mm/slub_pmm.c/.h` 中完成 Cache 层，使上层能够选择大小类别、寻找可分配 slab，并通过下层接口取还对象、扩充和回收 slab。

只执行本模块，保留模块一的上层控制逻辑及用户修改。不得展开 Slab 页内算法、大对象页管理或内核接入，也不生成测试；沿用 `lab2 2413646` 注释要求。

输出要求：执行完成后，将本模块的完成说明、实际文件和接口、验证结果及待完成内容输出，未执行的验证明确注明。

[RELY]

先读取模块一实际生成的 `kmalloc.h/.c`、`slub_pmm.h/.c` 中的结构、函数签名和行为注释，以及 `libs/list.h`、`kern/debug/assert.h`。若模块一尚未完成，停止并说明依赖，不杜撰字段。

Cache 规格为 `16、32、64、128、256、512、1024、2048`，描述符集中静态保存。Cache 管理其 slab，slab 管理具体空闲对象。页内管理及大对象路径仍可能为待实现接口，不能据此把正常请求直接判定为资源不足。

[GUARANTEE]

实现模块一已声明的 `slub_cache_init`、`slub_select_cache`、`slub_cache_alloc`、`slub_cache_free`，准确沿用源码签名。通用 `kmalloc.h/.c` 和管理器回调契约保持不变；若私有结构必须调整，同步更新相关使用处并说明原因。

调用模块一已声明的 slab 创建、对象取还及回收接口，不重新设计另一组接口。Slab 和大对象路径继续保留明确的待实现桩，不在本模块补全其函数体。Cache 初始化不另设与通用层重复的初始化状态。

[SPECIFICATION]

**Pre-Condition：** 模块一已完成，调用环境为单 CPU、串行执行。

**Post-Condition：** 初始化后每个 Cache 的管理入口为空，不取得物理页；`1–2048` 字节请求选择能容纳它的最小规格，零请求和大对象不误入小规格。缓存选择不改变分配状态。

**申请行为：** 优先使用所属 Cache 中有空位的 slab，没有可用空间时调用下层创建接口。成功后取得一个对象，正确维护 slab 的可用性；同类小对象能够共享页面，不能每个对象独占一页。底层真实失败时不残留空记录或损坏已有状态。

**释放行为：** 调用下层归还对象，正确处理 slab 从满到可分配的变化；最后一个对象释放后，移除相关管理入口并调用回收接口。其他 slab 和对象保持有效。释放归属及槽位验证与下层职责明确，不根据对象内容猜测 Cache。

**状态约束：** 只保存规格、管理入口及必要状态，不重复缓存可推导的信息，不增加全物理页状态表。下层待实现路径不得假装可运行。结束时说明已完成的 Cache 逻辑和仍待补全的 Slab 接口，随后停止。


### AI 输出

#### 完成内容

模块二完成 Cache 层，保留通用接口和上层调用关系，未展开 Slab 页内算法。

- `slub_cache_init` 初始化 8 个静态 Cache 及空链表，不申请页面；`slub_select_cache` 返回能容纳请求的最小规格。
- `slub_cache_alloc` 查找所属 Cache 中未满的 slab，调用 `slub_take_object`；没有可用 slab 时调用 `slub_create_slab`。
- `slub_cache_free` 调用 `slub_return_object`，最后一个对象释放后调用 `slub_destroy_slab`；链表移除由销毁函数负责，避免重复移除及回收后访问。

#### 实际验证与待完成内容

```bash
make -B -j2 PMM=buddy KMEM=1 BUDDY_TEST=0
```

编译、链接通过；Slab 查找、创建、对象取还、销毁和大对象路径仍为待实现桩，因此未运行开启模式或专项测试。

## 小模块三：Slab、对象槽位与大对象页面管理

[PROMPT]

基于前两模块，在 `code/kern/mm/slub_pmm.c/.h` 中补全 Slab 和页面层：创建页内对象布局，取还空闲对象，回收空 slab，并实现大对象页面分配及释放，使整个分配器核心逻辑完整。

只执行本模块，保留现有调用层次，不重写底层 Buddy、Best-Fit 或页描述符，不修改 Makefile 和启动代码，不生成专项测试。沿用 `lab2 2413646` 注释要求；完成后停止。

输出要求：执行完成后，将本模块的完成说明、实际文件和接口、验证结果及待完成内容输出，未执行的验证明确注明。

[RELY]

读取前两模块的通用接口、SLUB 管理器回调和内部契约；缺少依赖时停止。不修改通用 `kmalloc.h/.c`，不将公共函数重新定义到 SLUB 文件中。公共页接口为：

```c
struct Page *alloc_pages(size_t n);
void free_pages(struct Page *base, size_t n);
size_t nr_free_pages(void);
```

读取 `pmm.h`、`memlayout.h`、`mmu.h`、`libs/defs.h`、`list.h`、`string.h` 和 `kern/debug/assert.h`。`PGSIZE=4096`，数据地址使用 `page2pa(page) + va_pa_offset`；不能把描述符当作数据，也不能使用已被注释的 `KADDR`。

底层可为 Buddy 或 Best-Fit。Buddy 可向上取整实际占用，使用原申请的页描述符和页数归还即可；不得依赖其私有字段或覆盖 `Page` 管理状态。

[GUARANTEE]

补全前两模块留下的 Slab、对象取还、归属验证和大对象路径接口，沿用实际签名。最终 `slub_kmem_manager` 的初始化、分配、释放回调所需的全部依赖必须真实实现，不能残留执行路径上的占位桩。

保持通用公开接口和 `slub_kmem_manager` 注册关系不变，公开函数仍只在 `kmalloc.c` 定义：

```c
void kmem_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
```

[SPECIFICATION]

**Pre-Condition：** 前两模块已完成，页分配器已初始化；运行范围为单 CPU、串行调用。

**Post-Condition：** 分配成功时至少提供请求大小的可写空间，地址按 16 字节对齐；在用分配互不重叠，不覆盖元数据，不改变其他对象内容。不承诺清零。释放后管理入口、空闲结构和使用数量一致。

**Slab 生命周期：** 每个 slab 使用一页、只承载一种规格，页内保存必要管理信息及对象。容量扣除元数据、对齐和必要状态开销；创建成功时空闲链表完整覆盖槽位，每个槽位只出现一次。创建失败完整归还已取得资源。对象释放但 slab 非空时保留页面；最后一个对象释放后归还该页，且不再访问页内元数据。

**对象取还：** 从空闲链表取出和放回槽位，指针可保存在空闲槽位自身。调用者使用对象时，其有效数据区不能继续存放分配器依赖的链表信息。新建 slab 可以初始化各槽位；正常操作不逐个重写全部对象状态，也不遍历底层块各页维护冗余状态。

**大对象路径：** 超过 2048 字节时，按请求数据、管理信息和对齐开销计算页数，不固定分配两页。记录归还所需的描述符和原申请页数，不把管理头算入可写空间；本次不复用剩余尾部。释放时完整归还一次，不能只归还返回指针所在页。管理信息不得通过递归调用依赖自身的 `kmalloc` 取得。

**失败与边界：** 零大小返回 `NULL`，空指针释放不做操作；超出支持范围、计算可能溢出或资源不足时返回 `NULL`，不发生整数回绕、零页申请或泄漏。失败保持其他对象有效，部分取得资源须完整回滚。

**非法释放：** 未登记地址、对象内部地址、未分配槽位及可识别的重复释放，须在修改状态前断言拒绝。先验证地址归属和槽位边界，再读取相关管理信息，不能随意解引用未知地址。无法仅凭裸指针区分已复用地址上的旧指针，不声称解决此问题。

**最少状态：** 不记录每个小对象的原请求字节数或可推导的 order，不新增全物理页状态表；如需分配标记或位图，仅覆盖实际 slab 并说明用途。禁止宿主机 `malloc` 或静态大数组模拟内存。全部对象归还后不保留空 slab 或大对象页面，底层空闲页数恢复至 SLUB 初始化后的基线。

核心补全后，在已加载工具链的 WSL 中验证 Buddy、Best-Fit 分别配合 `KMEM=0/1` 的构建与启动，切换时强制重建；必要时只修复本模块引入的接口衔接问题，不重写通用接口或既有接入配置。报告实际命令和结果，不将启动或编译成功当作专项测试通过，不修改 Git。本模块不负责拦截调用者越界读写。

### AI 输出

#### 完成内容与代码审阅

模块三补全 Slab 和大对象页面路径，分配器核心不再含待实现桩。

- Slab 使用真实单页内存，页内管理头与对象区域分开；空闲对象链表覆盖全部合法槽位，32 字节位图用于检查槽位是否已分配。
- 普通操作只维护目标槽位、空闲链表和在用计数；Slab 全空后先移除管理入口，再归还页面，之后不再读取页内元数据。
- 大对象根据管理头、对齐及请求大小计算页数，检查溢出和资源不足；保存原页描述符、申请页数，完整释放一次。3036 字节不被固定视为两页请求。
- 审阅时删除了可以直接计算的 slab 容量副本；大对象释放从断言表达式中移出，避免释放动作依赖断言副作用。未增加原请求字节数、order 或全物理页状态表。

#### 实际验证与限制

以下组合均强制重新编译并在 QEMU 启动成功，Buddy 构建使用 `BUDDY_TEST=0`：

- `PMM=buddy KMEM=0`
- `PMM=buddy KMEM=1`
- `PMM=default KMEM=0`，当前默认底层为 Best-Fit。
- `PMM=default KMEM=1`

开启模式实际输出：

```text
byte memory management: slub_kmem_manager
```

结束后已重新构建并启动验证默认 Best-Fit、`KMEM=0` 模式。改动范围检查通过：实现新增/修改限于通用接口、SLUB 算法、Makefile 和内核初始化文件，Buddy、Best-Fit、`pmm.h/.c` 保持原样。

本轮结果只证明编译、链接和初始化接入通过，尚未运行对象分配释放专项测试；`slub_test.c/.h`、`run_slub_tests.py` 仍为空。未进行 Git 提交。
