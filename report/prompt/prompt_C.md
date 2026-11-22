# Lab4 C：虚拟内存页操作实现 Prompt

## [PROMPT]

在当前 Lab4 的 `kern/mm/pmm.c` 中完成 `LAB4 function1`，填写学号 2413646，直接修改真实文件。

## [RELY]

读取 `pmm.c`、`pmm.h`、`mmu.h`、`memlayout.h`、`sync.h`，使用已有页表索引、权限宏，以及 `get_pte`、`pte_create`、`pte2page`、`pde2page`、`page_ref_inc/dec`、`free_page`、`tlb_invalidate`。

当前 Sv39 `get_pte()` 最多分配两张中间页表；第二次分配失败时，第一张新表可能仍保留在页目录中。

## [GUARANTEE]

交付以下接口，保持 A/B 代码、函数签名与 Page 结构不变：

```c
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm);
void page_remove(pde_t *pgdir, uintptr_t la);
```

保持“页表项内容、物理页引用计数、TLB 状态”三者一致。可添加文件内静态辅助函数，并为 `tlb_invalidate()` 补充 `memory` clobber；不扩展其他内容。

## [SPECIFICATION]

### Pre-Condition

`pgdir` 有效，`la` 位于框架支持的多级 4 KiB 映射范围；插入时 `page` 为已分配的有效物理页。调用者负责同步。

### page_insert

先调用 `get_pte(pgdir, la, 1)`，取得 PTE 后再增加新页引用。原地址未映射时直接建立映射；已映射到同页时抵消本次引用增加，不释放该页；已映射到其他页时撤销旧引用，归零才释放旧页。

成功时写入目标物理页、调用者要求的 `perm` 和有效位，不残留旧权限或擅自增加读写权限；完成对应 TLB 失效后返回 0，使后续访问使用新映射与新权限。

分配失败时返回 `-E_NO_MEM`，回收本次新建空表并恢复原表项，保持旧映射、相关页引用和空闲页数不变。不得先删除旧映射再尝试分配，不得留下半完成映射。

### page_remove

使用 `get_pte(pgdir, la, 0)` 查找，不为删除分配页表。无表或无有效映射时正常返回，不修改引用计数。

有映射时清除 PTE、失效对应 TLB、减少物理页引用，归零才释放；普通删除不自动回收中间页表。接口必须幂等，重复调用不重复减引用或释放，也不把未映射地址作为错误。

### Verification

普通编译后运行框架的 `check_pgdir()`、`check_boot_pgdir()` 和 `make grade`。环境或测试接口问题仅做必要兼容修正，不修改断言、匹配串或分值。
