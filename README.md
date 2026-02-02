# DCtype

- 关于 DCtype：类型信息查询工具
- 接入方式：链接 `DCtype.h`

## 使用方式（推荐：显式冻结注册）

为保证查询行为稳定且可控，建议按以下流程使用：

1. 注册所有类型
2. （可选）为该枚举注册表设置未注册类型的 fallback 值
3. 冻结注册表
4. 开始进行查询

示例：

- `DC::registerType<T, Enum>(value)`：注册类型
- `DC::setFallback<Enum>(unknownValue)`：设置未注册类型返回值
- `DC::freeze<Enum>()`：冻结注册表（冻结后禁止继续注册，查询要求已冻结）

备注：
- 在 Debug 下，如果在未冻结状态下进行查询，会触发断言，提示先调用 `DC::freeze<Enum>()`。
- `DC::getTypeOr` 仍可在查询时显式指定 fallback，以覆盖注册表的 fallback。