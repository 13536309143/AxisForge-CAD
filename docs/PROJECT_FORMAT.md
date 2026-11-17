# 项目格式契约 · 1.0.2

应用版本1.0.2统一读写 `.afcad` 格式8。格式标识为 `axisforge.project`，单位为 `mm`。只接受当前版本，不读格式1—7，也不自动迁移；拒绝输入时保留调用方原场景。

## 根对象

| 必需字段 | 类型与含义 |
| --- | --- |
| format | 字符串，固定 axisforge.project |
| version | 整数，固定8；代码常量ProjectFormatVersion |
| units | 字符串，固定mm |
| title | 字符串，最多200字符 |
| bodies | 节点数组，最多300项，含空节点 |
| mates | 配合数组，最多1000项 |

## 节点

每个节点显式包含 `id`、`name`、`parentId`、`shape`、`parameters`、`position`、`rotation`、`color`、`visible`、`fixed`。根节点parentId为空字符串，不能省略字段。ID唯一且不能为虚拟地面保留ID `@ground`；父节点须存在，禁止父子循环。

shape可为box、sphere、cylinder、cone、torus、solid、mesh、tube、frustum、ellipsoid、wedge、group。group不包含几何。solid另有brep载荷，mesh另有mesh载荷。

parameters显式包含width、depth、height、radius、tubeRadius、segments；不使用的字段仍保存有效默认值。position为三个有限数值，rotation按 `[w,x,y,z]` 保存归一化四元数。位姿是世界坐标缓存，父节点刚体增量在编辑和求解时传播到后代。color为有效颜色字符串，visible与fixed必须是JSON布尔值，不接受字符串强制转换。

## 配合

每条配合显式包含 `id`、`source`、`target`、`type`、`sourceAxis`、`targetAxis`、`distance`、`angle`、`offset`、`enabled`。角度单位为度，距离与偏移为mm，轴编号0/1/2表示局部X/Y/Z。即便当前配合类型不使用某字段，该字段也必须存在。

type可为coaxial、parallel、perpendicular、distance、coincident、angle、axialOffset、ground。地面source固定为 `@ground`，target是实际节点。空子树没有最低几何点，不能使用地面关系。

## 完整性与范围

读取先检查JSON类型、字段、数量、几何载荷和引用，再验证层级／配合依赖与求解残差。项目文件最多64MiB。B-Rep、网格解码还有独立大小与顶点限制，详见ProjectIO、Kernel与MeshIO实现。

保存使用QSaveFile提交。撤销命令、相机、选择和视口开关不写入项目。额外未知字段不作为应用状态使用；本格式不承诺对未知数据无损保留。项目格式校验不等于完整恶意输入安全审计。

## 与交换格式的区别

STEP、GLB、OBJ、STL为几何交换，不承载本软件完整层级、配合或历史。需要继续编辑，请保存格式8项目。随包所有.afcad已更新为格式8，不需要借助旧版程序打开。
