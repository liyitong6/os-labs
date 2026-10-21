# Buddy System 测试结果

执行命令：`python3 tools/run_buddy_tests.py`（WSL os，code 目录；先加载工具链环境）。

正常用例检查真实物理页；负面用例分别启动，只接受对应分配器断言。

## 正常测试

- **PASS** normal behavior suite：all 7 normal groups passed。[normal](buddy_test_logs/normal.log)

## 断言用例 1–5

- **PASS** zero allocation：expected allocator assertion。[negative-01](buddy_test_logs/negative-01.log)
- **PASS** null release：expected allocator assertion。[negative-02](buddy_test_logs/negative-02.log)
- **PASS** double release：expected allocator assertion。[negative-03](buddy_test_logs/negative-03.log)
- **PASS** interior release：expected allocator assertion。[negative-04](buddy_test_logs/negative-04.log)
- **PASS** wrong block size：expected allocator assertion。[negative-05](buddy_test_logs/negative-05.log)

## 断言用例 6–10

- **PASS** misaligned descriptor：expected allocator assertion。[negative-06](buddy_test_logs/negative-06.log)
- **PASS** upper descriptor bound：expected allocator assertion。[negative-07](buddy_test_logs/negative-07.log)
- **PASS** reserved page release：expected allocator assertion。[negative-08](buddy_test_logs/negative-08.log)
- **PASS** zero release：expected allocator assertion。[negative-09](buddy_test_logs/negative-09.log)
- **PASS** oversized release：expected allocator assertion。[negative-10](buddy_test_logs/negative-10.log)

## 断言用例 11–15

- **PASS** referenced head release：expected allocator assertion。[negative-11](buddy_test_logs/negative-11.log)
- **PASS** lower descriptor bound：expected allocator assertion。[negative-12](buddy_test_logs/negative-12.log)
- **PASS** duplicate registration：expected allocator assertion。[negative-13](buddy_test_logs/negative-13.log)
- **PASS** zero registration：expected allocator assertion。[negative-14](buddy_test_logs/negative-14.log)
- **PASS** registration upper bound：expected allocator assertion。[negative-15](buddy_test_logs/negative-15.log)

## 普通镜像恢复

- **PASS** ordinary Buddy startup：ordinary startup restored。[ordinary](buddy_test_logs/ordinary.log)

## 覆盖范围与有效性

**测试配置：** 在 QEMU 中使用真实空闲物理页，已排除内核和管理数据占用的页面。先分次登记共 12 页，检查不规则起点、非 2 的幂页数和未登记的间隔；通过后再登记其余可用页，全程不重置分配器。

**测试内容与判定依据：**

- **页面登记与分配：** 逐页核对登记范围，确保不遗漏页面、不分配未登记页面；用预先列出的期望块大小检查申请页数向上取整、地址对齐、块不重叠和空闲页计数。
- **释放与伙伴合并：** 将其他页面全部占住，只释放指定的 8 页区域，再申请 8 页，验证这些页面确实完成合并；检查不同释放顺序和旧块首标记清除，避免其他空闲大块使错误实现也通过测试。
- **碎片与资源不足：** 隔页释放、释放相邻但非伙伴的页面，确认不能错误合并；检查耗尽和超大请求返回失败，且空闲页数及已有数据不变。
- **数据隔离与反复使用：** 向不同已分配块写入不同数据，释放并重新分配其中一块，确认其他块内容不变；执行 32 轮混合申请与释放，每轮结束后空闲页数都应恢复到初始值。
- **非法操作：** 零页请求、重复释放、内部页释放、错误大小和越界参数等 15 个用例分别启动 QEMU。只有用例标识、报错文件和对应断言均匹配才算通过，不能仅凭超时判定成功。

**有效性分析：** Buddy 正确性的核心，是登记后的页面始终被空闲块和已分配块完整、互斥地划分；块大小及对齐符合伙伴规则，空闲页计数与实际状态一致，拆分和合并不会丢页、重复分配或影响仍在使用的数据。因此，测试需要同时检验初始登记是否正确，以及分配、释放和失败操作是否保持这些性质。上述用例按影响行为的条件划分：请求大小位于 2 的幂边界前、边界上和边界后，资源充足、耗尽或有碎片，释放时伙伴可合并、不可合并或可连续多级合并，并加入非法参数。这些条件决定了主要处理分支，比仅测试若干随机页数更有针对性。耗尽测试使全部可用页实际参与分配，完整归还和重复使用则能暴露单次操作后暂未显现的页面丢失、残留标记及链表错误；受控区域限制了可用资源，减少错误实现借助其他空闲块通过测试的可能。不过，这仍是对关键行为类别的覆盖，不是对所有块大小、布局和操作顺序的穷举。全部通过能支持“实现满足已测试场景中的核心要求”，不能证明绝对正确；严格证明还需论证任意合法初始状态下，每种操作都保持上述不变量。

**验证边界：** 未测试并发访问、所有内部页的引用状态或页面映射的建立与解除，也不验证调用者越界写入能否被拦截。本结果仅说明上述 Buddy 专项测试通过，不代表原 `make grade` 通过。

**运行记录：** 测试镜像可用 31863 页，普通镜像可用 31929 页；测试代码和静态数组增加了内核占用，因此测试镜像可用页数较少。首次运行因未加载工具链 PATH 而编译失败，未执行测试；随后在 Bash 登录环境中运行通过。[首次错误日志](buddy_test_logs/first-env-failure-build.log)。
