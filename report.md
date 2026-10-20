# Lab2 功能模块二报告：连续物理内存分配算法

## 任务一：理解 First-Fit 连续物理内存分配算法

### 1. 整体框架

ucore 把物理内存分配抽象成一个"管理器接口" `struct pmm_manager`，里面是一组函数指针（init / init_memmap / alloc_pages / free_pages / nr_free_pages / check）。`default_pmm.c` 填了一张表，就是 First-Fit 分配器；`pmm.c` 作为框架层，只通过 `pmm_manager->xxx()` 调用，不关心底下是哪种算法。

内部状态是一个全局 `free_area_t`：

```c
static free_area_t free_area;
#define free_list (free_area.free_list)   // 空闲块双向循环链表头
#define nr_free   (free_area.nr_free)     // 当前空闲总页数
```

链表上每个节点对应一个**连续空闲块**，挂在块头页的 `page_link` 字段上；块头页的 `property` 记录这个块有几页，`PG_property` 位标记"我是块头"。链表按物理地址升序排列。

### 2. 各函数作用

#### default_init
初始化空闲区：`list_init(&free_list)` 把链表头变成自指的空链表，`nr_free = 0`。这只是清零内部状态，此时还不知道机器有多少内存。

#### default_init_memmap(base, n)
内核探测到一段连续空闲物理页（`base` 起 `n` 页）后调用，把这段页登记进空闲链表：

1. 遍历这 `n` 页，把每页的 `flags`、`property` 清 0，`ref` 置 0（这些页原来是保留状态，现在交给分配器）。
2. 把第一页 `base` 设为块头：`base->property = n`，`SetPageProperty(base)`。
3. `nr_free += n`。
4. 按物理地址升序把 `base` 插入链表——链表为空直接挂在头后；否则从头往后找，插到第一个地址比 `base` 大的块之前，都比它小就插到末尾。

#### default_alloc_pages(n)
分配连续 `n` 页：

1. `n > nr_free` 直接返回 NULL。
2. **从链表头开始往后扫，遇到第一个 `property >= n` 的块就选中，立刻 break**——这就是 First-Fit 的"First"。
3. 把选中块从链表摘掉。
4. 如果块大小 `> n`：前 `n` 页切出去分配，尾部剩余 `(property - n)` 页作为新块头页插回链表（置 `PG_property`、写 `property`）。
5. 如果块大小 `== n`：整块拿走。
6. `nr_free -= n`，被分配出去的头页清掉 `PG_property`。

#### default_free_pages(base, n)
回收连续 `n` 页：

1. 遍历这 `n` 页，断言它们既不是 reserved 也不是空闲块头，然后把 `flags` 清 0、`ref` 置 0。
2. 把 `base` 设为新块头：`base->property = n`，`SetPageProperty(base)`，`nr_free += n`。
3. 按地址升序把新块插入链表。
4. **向前合并**：看链表前驱块的尾页是否正好等于 `base`（即物理上相邻），是则合并：把前驱的 `property` 加到 `base` 上，清掉前驱的 `PG_property`，把前驱从链表删掉，`base` 前移到前驱。
5. **向后合并**：再看后继块的首页是否正好等于合并后的块尾，是则同样合并。

### 3. First-Fit 的改进空间

First-Fit 实现简单、O(n) 扫描一次就停，但有几个明显缺点：

1. **容易在链表头部留下大量"小碎块"**：每次都从前面找，前面的块被反复切来切去，越切越碎，大块请求反而要扫到后面才找到。
2. **大块请求会"跨越"前面的小碎块**：明明前面有很多小块凑起来够数，但不连续，只能找后面的大块，浪费前面碎片。
3. **选块策略粗糙**：只要够大就用，不看富余多少，一个只要 1 页的请求可能占掉一个 100 页大块的开头，把 100 页块切成 1+99，造成浪费。

改进方向：

- **Best-Fit**：扫描全表，选 `property >= n` 中最小的那块（本实验任务二）。
- **Worst-Fit**：反过来选最大的块，避免产生小碎块。
- **按大小分桶**：把空闲块按大小分级挂不同链表，分配时直接定位。
- **伙伴系统（Buddy System）**：块大小限制为 2 的幂，分配 O(log)，回收时父子块合并。

---

## 任务二：实现 Best-Fit 连续物理内存分配算法

### 1. 实现内容

在 `kern/mm/best_fit_pmm.c` 中补全了完整的 Best-Fit pmm 模块，包括：

- `static free_area_t free_area;` 及 `free_list` / `nr_free` 宏映射；
- `best_fit_init`：初始化空链表、`nr_free = 0`；
- `best_fit_init_memmap`：与 First-Fit 相同，按地址升序登记空闲块；
- `best_fit_alloc_pages`：**核心差异点**——遍历整个空闲链表，在所有 `property >= n` 的块中选择 `property` 最小的那个（Best-Fit），选中后切块逻辑与 First-Fit 相同；
- `best_fit_free_pages`：按地址插入 + 前后邻居合并，与 First-Fit 相同；
- `best_fit_nr_free_pages`：返回 `nr_free`；
- `best_fit_pmm_manager`：填充 `struct pmm_manager` 函数指针表，`.check` 指向文件中已有的 `best_fit_check`。

并在 `kern/mm/pmm.c` 的 `init_pmm_manager()` 中将默认管理器从 `default_pmm_manager` 切换为 `best_fit_pmm_manager`。

### 2. 与 First-Fit 的唯一本质区别

```c
// First-Fit：遇到第一个够大的就停
if (p->property >= n) {
    page = p;
    break;
}

// Best-Fit：扫全表，选最小满足块
if (p->property >= n) {
    if (page == NULL || p->property < page->property) {
        page = p;
    }
}
```

测试 `best_fit_check` 中有一个关键判定：当空闲链表同时存在一个 2 页块（p0+1）和一个 1 页块（p0+4）时，请求 `alloc_pages(1)` 必须返回 p0+4（1 页块）而不是 p0+1（2 页块）。这正是 Best-Fit 与 First-Fit 的分水岭。

### 3. Best-Fit 的改进空间

Best-Fit 思想上减少了大块浪费，但工程上仍有可改进之处：

1. **每次分配都要全链表扫描，O(n)**：链表长时慢。改进方式是按块大小排序空闲链表（红黑树/按大小分桶），把查找降到 O(log n)。
2. **容易产生极小残块**：Best-Fit 总是把大块切到刚好，剩下的尾部往往只有一两页，再也用不掉，变成外部碎片。改进方式是设最小残块阈值——如果切完尾巴小于阈值就整块给出。
3. **按地址排序和按大小查找矛盾**：链表为了合并邻居按地址排，Best-Fit 查找又要按大小扫，两个需求冲突。更彻底的方案是伙伴系统：块大小天然为 2 的幂，按大小分桶，分配和回收都 O(log)，相邻伙伴自动合并。
4. **页级粒度太粗**：内核里大量 16/64/256 字节的小对象按页分浪费严重。改进方式是在页级分配器之上加 SLAB/SLUB 层，把一个页切成多个同大小小对象。

最终演化方向就是本实验后续的 Buddy System（任务三）和 SLUB（Challenge）。

### 4. 本任务使用的提示词

见同目录下的 `prompt.md`。
