# AxisForge CAD 0.5.0

- 整理界面工具栏：文件与编辑、基础建模、装配与定位、视图；更多模型集中在下拉菜单。
- 新增“对齐地板”（Ctrl+G）：所选模型最低点对齐世界 Z=0，保留旋转与配合，支持撤销；冲突时拒绝并保留原状态。
- 新增圆管、圆台、椭球、楔形体，均为可计算 OCCT 实体，可编辑参数、执行布尔与 STEP 交换。
- 保留七种装配配合、关系可视化、GLB/OBJ 交换和自适应地板。
- 新图元项目使用 version 5，兼容读取 version 1～5。

Windows 用户下载并解压 Windows-x64.zip，运行 start.cmd，保留整个 run 目录。驱动需支持 OpenGL 3.3。OBJ 的 MTL 文件须一起保留；网格不直接参与实体布尔。

本机采用 Qt 5.15.2 / OCCT 7.9.3 / MSVC x64 验证；Release 工作流另在 Windows Qt6 构建并测试核心，在 Linux Qt6 执行核心及桌面检查。依赖许可见 run/licenses 与 docs/DEPENDENCIES.md。
