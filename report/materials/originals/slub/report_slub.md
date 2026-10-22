# SLUB 实现与测试记录

<!-- SLUB TEST RESULTS START -->
## 测试配置与结果

本轮共 15 项运行结果，通过 15 项，失败 0 项。

在 WSL os 的 code 目录运行 `python3 tools/run_slub_tests.py`；默认只用框架默认的 Best-Fit 提供页面，运行 SLUB 用例并跳过底层自检；追加 `--with-pmm-check` 可开启同一底层的自检。

本轮使用 PMM_CHECK=0，日志核对底层自检确实跳过；默认镜像只重建，不启动底层自检。

每个预期断言单独启动 QEMU，并核对用例标识、源文件及具体断言；超时不算通过。

### 运行记录 1–5

- **PASS** slub-normal：verified completion。[日志](slub_test_logs/run-20261022-122117/slub-normal.log)
- **PASS** slub-negative-01：expected assertion。[日志](slub_test_logs/run-20261022-122117/slub-negative-01.log)
- **PASS** slub-negative-02：expected assertion。[日志](slub_test_logs/run-20261022-122117/slub-negative-02.log)
- **PASS** slub-negative-03：expected assertion。[日志](slub_test_logs/run-20261022-122117/slub-negative-03.log)
- **PASS** slub-negative-04：expected assertion。[日志](slub_test_logs/run-20261022-122117/slub-negative-04.log)

### 运行记录 6–10

- **PASS** slub-negative-05：expected assertion。[日志](slub_test_logs/run-20261022-122117/slub-negative-05.log)
- **PASS** slub-negative-06：expected assertion。[日志](slub_test_logs/run-20261022-122117/slub-negative-06.log)
- **PASS** slub-negative-07：expected assertion。[日志](slub_test_logs/run-20261022-122117/slub-negative-07.log)
- **PASS** slub-negative-08：expected assertion。[日志](slub_test_logs/run-20261022-122117/slub-negative-08.log)
- **PASS** slub-negative-09：expected assertion。[日志](slub_test_logs/run-20261022-122117/slub-negative-09.log)

### 运行记录 11–15

- **PASS** slub-negative-10：expected assertion。[日志](slub_test_logs/run-20261022-122117/slub-negative-10.log)
- **PASS** slub-negative-11：expected assertion。[日志](slub_test_logs/run-20261022-122117/slub-negative-11.log)
- **PASS** slub-negative-12：expected assertion。[日志](slub_test_logs/run-20261022-122117/slub-negative-12.log)
- **PASS** slub-disabled：verified completion。[日志](slub_test_logs/run-20261022-122117/slub-disabled.log)
- **PASS** slub-restore-default：default image rebuilt (not booted)。[日志](slub_test_logs/run-20261022-122117/slub-restore-default.log)

## 覆盖范围与有效性

- 规格边界、2048/2049、3036、一页半及整两页请求检查大小分流；预期类别用独立常量表，填满页面后的最小槽位间距核对实际规格，不依赖私有容量函数。
- 超过单页理论槽位上限的分配覆盖扩充；共享页、保留一个在用对象及最后释放分别验证复用、不能提前回收、必须归还整页。
- 请求范围完整写入不同图案，交错释放和跨页大对象共同检查覆盖范围、重叠及数据破坏；16 轮混合操作后恢复基线，检查历史状态残留。
- 用直接页申请耗尽真实页面，分别验证新 slab、大对象失败和既有空槽仍可分配；检查失败不改计数和已有数据，全部归还后再次申请。
- 使用默认 Best-Fit 提供真实页面；关闭模式、初始化误用和独立断言覆盖框架接入。默认核对底层自检跳过标识，不重复运行其他页分配器；可用 --with-pmm-check 开启同一底层自检。

## 有效性分析与限制

这些场景围绕初始化、申请、释放、失败及重复使用的状态转换，检查范围合法、所有权互斥、数据保持和资源守恒。资源数量、实际地址和数据内容相互核对，可减少单一检查恰好通过的情况。测试覆盖主要行为类别，并非穷举所有大小、布局和操作序列；未验证并发、越界写入拦截或地址复用后的旧指针识别，也不代表 make grade 通过。

原始构建日志、QEMU 日志和结果 JSON 保存在 `slub_test_logs/run-20261022-122117/`，失败尝试不会覆盖。
<!-- SLUB TEST RESULTS END -->
