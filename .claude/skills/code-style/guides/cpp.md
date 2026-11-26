# C++ 与 Qt 代码风格

本项目采用 C++20、Qt 和 rpl，沿用 Telegram Desktop 的实际接口与格式。没有独立生效的 clang-format / clang-tidy 配置时，以相邻源码和 CMake 编译配置为准；不为格式整理引入新的工具链。

## 命名

| 元素 | 项目写法 | 示例 |
|---|---|---|
| 类型、结构体、类型别名 | 大驼峰 | `HistoryItem` |
| 局部变量、参数 | 小驼峰，优先 `const auto` | `dataName` |
| 成员变量 | `_` 前缀加小驼峰 | `_lastseen` |
| 常量 | `k` 前缀加大驼峰 | `kWideIdsTag` |
| extras 函数 | 小驼峰 | `processUser` |
| 命名空间 | 大驼峰或匿名 | `ExtrasInfra` |
| 文件名 | 领域前缀加小写下划线 | `data_user.cpp` |

上游函数命名混杂，修改时沿用已有接口；不整批改名。重载、重写、序列化字段和对外符号保持契约。标准协议沿用 `begin()`、`end()`、`value_type` 等要求的拼写，C 链接保留 ABI 名称。

## 包含与职责

- 源文件先包含对应头文件，再按相邻源码排列本项目、第三方与标准库声明；直接包含实际使用的声明。
- 头文件可独立包含，沿用 include guard 或 `#pragma once`；不在头文件全局使用 `using namespace`。
- 内部实现留在私有成员、源文件或匿名命名空间；前置声明只用于不需要完整类型的边界。
- 文件布局、依赖方向和构建登记按根 AGENTS；增加实现文件时核对真实消费者。

## 控制流与失败

- 先排除失败、空值和无操作情况，主流程靠左；`return`、`continue`、`break` 后不接 `else`。
- 循环先跳过不相关项；找到目标后，在循环外处理。查询与修改共用函数时先完成查询分支并返回。
- 含额外校验且嵌套超过两层的分支，抽成返回 `Result` 或 `std::optional` 的职责函数；对称二选一保留 `if/else`。
- 失败沿用所在接口的返回值、状态码或结果类型，缺项可用 `std::optional<T>`。`Result { bool ok; QString payload; }` 是现有调试协议结果，不作为所有业务结果的通用模型；内部不变量用 `Expects()` / `Ensures()` 表达。
- 不为普通控制流新增异常。JSON 读写、导入、数据库事务等已有边界沿用依赖的实际错误契约，在能够处理失败的位置捕获并报告；不要为统一形式移除有效边界，也不逐层捕获后返回默认值。
- 日志使用 `LOG(("Category: message %1").arg(value))`，不使用 `qDebug()`；诊断语言按技能主文件。

## 类型与生命周期

- 用 RAII 管理资源；独占所有权优先 `unique_ptr`，确有共享所有者时才用 `shared_ptr`。Qt 对象按父子归属和项目 `object_ptr` 约定管理。
- 裸指针、引用与 `not_null<T*>` 表示借用；借用不越过所有者寿命。需要 `T*` 时使用 `not_null::get()`，不能返回指向局部栈对象的借用。
- 根据调用选择 `const`、引用、值和 `auto`；有意义的窄化与布局转换在边界显式处理。常量和类型关系优先由 `constexpr`、作用域枚举表达。
- 遍历 `json.items()` 前先把 JSON 放入具名变量；range-for 不会延长临时 JSON 经 `items()` 返回的代理所依赖的寿命。
- 字符串字面量不能与 `constexpr` 标识符直接拼接；`QStringLiteral` 的参数同样受宏展开限制，按需要使用 `QLatin1String` 或显式拼接。
- 长期 QObject 挂到随应用退出销毁的父对象，或在 `QCoreApplication::aboutToQuit` 清理；静态 QObject 不得存活到 QApplication 析构之后。

## rpl 与线程

- 对 `rpl::variable<T>` 设置，一次读取用 `current()`，订阅用 `value()`；lambda 不捕获 `current()` 返回的引用。直接赋值触发通知，不重复手动发送。
- 界面订阅绑定 `lifetime()`；捕获控件或 `this` 的异步回调使用项目现有的生命周期保护。
- 在线程归属允许的边界访问状态，具体持有方与调度入口见根 AGENTS；跨线程 Qt 信号显式使用 `Qt::QueuedConnection`。
- 不在主线程调用会嵌套事件循环等待网络响应的 `ExtrasSync::*Sync` 接口。
- 共享状态使用项目同步机制，锁的作用域保持短小，持锁期间不调用无关阻塞操作或外部回调。

## 设置与构建核对

- extras JSON 设置同步维护成员、`to_json` 和 `from_json`；官方设置沿用 `serialize()` / `addFromSerialized()` 等现有持久化接口。模型及界面的实际位置按根 AGENTS。
- 跨 ABI 的所有权、布局、编码、错误和取消遵循真实包装边界。
- 公共头文件、模板和共享状态变更按实际消费者验证；文件行数与检查入口由技能主文件统一定义。
