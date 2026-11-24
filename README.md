# Lab5 B 模块

本分支 lab5-b 保存 B 部分实现、验证工具和调试资料。
code/ 为 Lab5 框架与 B 实现，A/C 功能待合入，完整评分待合并后执行。

- [B 实现与验证总结](lab5_B/docs/B_materials.md)
- [双重 GDB 现场与 QEMU/TCG 分析](lab5_B/docs/GDB_OBSERVATIONS.md)
- [8 张截图和原始日志](lab5_B/docs/evidence/README.md)
- [A/C 对接约定](lab5_B/docs/INTERFACES_A_C.md)

已完成：8 个目标编译单元检查、独立 fixture 的 11 个运行检查点、
一次 SYS_putc 的用户态到内核再返回用户态的双重 GDB 记录。

整组实验报告由 A 负责，最终合并联调和评分由 C 负责。
