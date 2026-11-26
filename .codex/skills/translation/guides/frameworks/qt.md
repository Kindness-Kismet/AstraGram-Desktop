# Telegram / Qt 翻译规则

AstraGram 使用 Telegram 的语言生成器和运行时语言包。按现有 `tr::` 接口维护键，界面翻译不采用 Qt Linguist 的 `tr("文本")`、`.ts/.qm` 或 `%n` 规则。

## 键与资源的对应

| 内容 | 现有约定 |
|---|---|
| 上游键 | `lng_snake_case`，保持官方名称与参数契约 |
| 定制键 | `extras_PascalCase`，例如 `extras_ChatFilterLocalRules` |
| extras JSON 键 | 省略 `extras_`，例如 `ChatFilterLocalRules` |
| JSON 中的官方文案覆盖 | 保留完整 `lng_*` 键 |
| 编译期接口 | `tr::lng_*` / `tr::extras_*`，声明由 `.strings` 生成 |
| 翻译键与英文默认值 | `Telegram/Resources/langs/lang.strings` |
| 当前内置 extras 覆盖 | `Telegram/Resources/langs/extras/en.json`、`zh-hans.json` |
| 内置语言注册 | `Telegram/Resources/qrc/extras/extras.qrc` |

`lang.strings` 同时登记官方和定制键，是 `td_lang.cmake` 的实际生成输入。仅在 JSON 新增条目不会生成 C++ 接口；`cloud_lang.strings` 也不是这一生成入口的替代品。

## 新增和修改

1. 查找当前功能的 `lng_*`、`extras_*` 及实际调用；同义且参数相同的文案复用已有键。沿用现有大小写，不用其他框架的点分层 ID 改写现有键。
2. 新定制键在 `lang.strings` 登记完整名称和英文默认值，在 `en.json`、`zh-hans.json` 补齐对应文案。修改既有项时核对实际覆盖关系；英文 JSON 若已有该项，同时维护默认值和英文覆盖。
3. 覆盖官方文案时先确认完整 `lng_*` 已登记；中文专用覆盖不机械补入英文 JSON。现有部分英文定制项只用 `.strings` 默认值，两个 JSON 的键集不要求完全相等。
4. 新增语言文件时同步登记 QRC。修改既有 JSON 条目不新增资源路径，保留其他已支持语言与原有云语言包流程。
5. C++ 调用包含 `lang/lang_keys.h`；按控件接口选择 `tr::key(tr::now, …)` 当前字符串或 `tr::key(…)` 响应式生产者。
6. 构建通过 `Telegram/cmake/td_lang.cmake`、`generate_lang.cmake` 调用 `codegen_lang`，生成 `lang_auto*.h/.cpp`。使用根 AGENTS 的构建入口，不修改生成文件。

`lang.strings` 的历史区块注释包含 `AyuGram keys generated`，当前构建没有从 extras JSON 自动同步该区块的步骤，仍须维护键定义。

## 格式与动态参数

- `.strings` 使用 UTF-8 的 `"完整键" = "文案";`，换行使用 `\n`，引号和反斜杠按该字符串语法转义；JSON 为标准对象，文案值使用字符串。
- 键定义解析器接受字母、数字、下划线、点和连字符，禁止连续 `__`；新键仍按本项目已用的前缀与命名写法。
- 动态内容使用 `{name}`，C++ 通过 `lt_name` 传值。保留参数名称、含义和类型，整句进入资源，允许译文改变参数顺序。
- 标签名使用字母、数字和下划线，禁止连续 `__`。同一字符串不重复同名标签；运行时会拒绝未知、非当前键允许或重复的标签。
- 运行时不会验证译文是否遗漏原有参数。修改时主动核对各译文的必要参数，尤其是用户名、文件名、金额和链接，不只依赖 JSON 解析成功。

## 数量与复数

- 英文源定义使用 `key#one`、`key#other`。生成器据此建立 `zero/one/two/few/many/other` 六个槽，语言规则决定实际分支。
- extras JSON 沿用 `_zero/_one/_two/_few/_many/_other` 后缀，加载时转成对应的 `#…`；不要把后缀改成其他框架的数量数组。
- 复数调用传数量，例如 `tr::extras_RegexFiltersAmount(tr::now, lt_count, count)`；保留已有复数标签契约。`count`、`count_short`、`count_decimal` 是生成器的复数保留标签，普通非复数项用已有的 `{amount}`、`{value}` 等语义参数。
- 英文至少核对 0、1、2；中文按自身数量规则使用 `other`。保留其他语言已存在的分支，不要求所有语言显式使用英文的 `one`。
- 数量可以由某一分支的完整句子直接表达；按既有语义判断是否显示计数，不机械给每个分支增加 `{count}`。其他动态参数保持完整。

## 富文本

- 文案格式必须匹配实际消费接口。`tr::rich` 支持的粗体、斜体标记和 `tr::marked` 的实体参数按现有调用使用，不假设支持任意 Markdown 或 HTML。
- 链接、强调和动态实体的边界随译文顺序一起核对，不能把占位符或控制标记作为可自由翻译的正文。

## 内置语言选择与旧格式

`extras_lang.cpp` 先尝试当前语言 ID，再尝试基础语言 ID，找到首个可解析的内置包后停止。显式映射只有 `zh-hans-beta`、`zh-hans-raw` → `zh-hans`；没有匹配包时保留英文默认，不能假设任意 `zh-*` 都有内置覆盖。

上游完整语言列表由云语言包管理；extras 当前内置 `en` 和 `zh-hans`，这不是应用全部支持语言的列表。语言包更新后会重新叠加 extras 文案。

加载器还兼容既有资源格式：

| 旧 JSON 内容 | 当前转换 |
|---|---|
| 键以 `_Android` 结尾 | 跳过 |
| 键以 `_PC` 结尾 | 去掉该后缀 |
| `%1$d`，没有 `%2$d` | `{count}` |
| `%1$d` 与 `%2$d` | `{count1}` 与 `{count2}` |
| `%1$s`，没有 `%2$s` | `{item}` |
| `%1$s` 与 `%2$s` | `{item1}` 与 `{item2}` |
| `&amp;` | `&` |

新文案直接用命名参数。旧格式转换采用互斥分支，不扩展为任意位置参数语法，也不混合数字、字符串或第三个位置参数。复数后缀处理先于 `_PC` 清理，维护旧项时按实际转换核对最终键。

## 检查本次改动

1. 核对 JSON 可解析性、重复键、字符串类型和规范化后的键是否已登记；检查英文覆盖与默认值、必要参数和数量分支。
2. 新资源核对 QRC 注册，新 C++ 调用核对生成声明和实际控件类型。`.strings` 的生成校验覆盖语法、重复键和标签规则，不覆盖 extras JSON 的全部语义。
3. 按真实调用核对当前译文、英文默认值和参数替换后的结果；仅查询未替换的资源字符串不能证明数量分支的最终呈现。
4. 运行检查覆盖当前修改的语言切换和相关布局，结果区分内置覆盖与云语言包来源。

仓库目前没有独立的翻译静态检查入口。`refresh.py` 会覆盖资源，`refresh.sh` 依赖官方私有环境，不能当作日常只读校验命令。

实现依据：`Telegram/codegen/codegen/lang/parsed_file.cpp`、`Telegram/SourceFiles/lang/lang_instance.cpp`、`lang_tag.cpp`、`lang_text_entity.h`、`Telegram/SourceFiles/extras/extras_lang.cpp`。规范变化时先核对这些实际消费者。
