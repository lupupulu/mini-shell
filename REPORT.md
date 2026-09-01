# parser.c 修复报告：使 `parse_divide_command` 正常工作（完整命令设计）

本报告记录本轮对 `parser.c` 的修复与整理（未提交）。背景：上一次提交（`5d2ef03`）的
REPORT 描述了旧式"词切分"解析器（`type_execute.argraw` 为 `char **` 词数组）；此后工作区中
`parser.c` 被改到一半——意图是让 `type_execute` 不再保存"粗略划分后的词数组"，而是保存
**完整命令原文**，但改动不完整，导致 `parser.c` 无法编译（重复声明、指针类型不匹配、
引用未定义标识符）且解析逻辑大量失效。本轮通过上次 git 提交（可工作的旧版实现）与现存代码，
完成了这一迁移并修复了测试中发现的全部逻辑错误。

---

## 1. 设计变更：`type_execute` 保存完整命令

| 项 | 旧设计（上次提交） | 新设计（本轮完成） |
|---|---|---|
| `type_execute.argraw` | `char **`，NULL 结尾的**词数组**（解析器已做词切分） | `char *`，**完整命令原文**，直接指向调用者的输入缓冲 |
| 词切分 | 解析阶段完成（空格处断词、逐个词存入数组） | 解析阶段**不再做**；引号、空格、`$` 扩展原样保留在命令文本中，词切分/展开留给执行器 |
| 命令文本终止 | 每个词单独以 `\0` 终止 | `parse_emit_command_raw()` 在命令末尾写入 `\0`，并裁掉尾部空白 |
| 会话状态 | 静态全局（`parse_pipe_is_continue` 等 8 个） | `parse_divide_command` 的局部变量 |
| 内存所有权 | `argraw` 数组本身由解析器 `malloc` | `argraw` 指向调用者缓冲，`parse_commands_free` 不再释放它 |

配套调整：

- `parse_commands_free()`：只释放 `type_redir.to_file/.body` 与各结构体本身，不再 `free(e->argraw)`；
- 新增 `parse_is_fd_span()`：对尚无 `\0` 结尾的 `[word_start,i)` 区间做定长数字检查（fd 词判定，
  `2>f` 为 fd、`echo 2 > f` 的 `2` 是参数）；
- 新增 `parse_span_is_blank()`：判定 `[start,end)` 是否纯空白（用于"命令位置"判定）；
- 新增 `parse_emit_command_raw()` + 宏 `emit_command(s,e)`：发出完整命令文本，并在真正发出
  EXECUTE 时复位 `pipe_need_cmd`；
- `start`（命令起点）/`word_start`（当前词起点）双游标替代旧的词数组 `arg`；空白分支在
  pending 区间纯空白时同步前进 `start`，维持"命令位置 ⇔ `start==i`"不变式。

## 2. 修复的编译错误（修复前无法编译）

| # | 问题 | 修复 |
|---|------|------|
| 1 | `case_pattern` 重复声明（同函数内两次 `int case_pattern=0;`） | 删除重复声明，统一为一个局部变量 |
| 2 | `save_execute()` 宏与多处 `parse_add_type(&cmds,CMD_EXECUTE,&buf[start])` 把 `char*` 传给 `size_t*` 形参 | 统一走 `parse_emit_execute()`→`parse_create_execute()` 构造 `type_execute`，`loc` 字段正确回填 |
| 3 | `parse_deal_break()` 引用已被注释掉的静态全局（`parse_pipe_is_continue` 等）→ 未声明标识符 | 删除 `parse_deal_break()`（其逻辑已内联进主循环 switch） |
| 4 | `parse_emit_execute()` 参数类型 `char **` 与新 `char *` 不匹配 | 签名改为 `char *` |
| 5 | `parse_execute_exe()` 内 `i` 重声明、对 `char*` 取下标 | 整个"Execute Part"残桩删除（见第 4 节） |

## 3. 修复的逻辑错误（编译通过后由测试暴露）

| # | 现象 | 根因与修复 |
|---|------|-----------|
| 1 | `a \| b`、`a \| b \| c` 报 "expected a command after '\|'" | `pipe_need_cmd` 置 1 后永不复位；`emit_command()` 宏现在在每次真正发出 EXECUTE 时复位它 |
| 2 | `ls > /tmp/out`（无结尾换行）重定向目标被重复输出为一条命令 | EOF 冲刷附加目标后未前进 `start/word_start`；现在附加后同步前进 |
| 3 | `sleep 1 & echo done &` 第二个 `BACKGROUND_START` 插到第一个 `BREAK` 之前 | `parse_set_pipe_start()` 改为把 `BACKGROUND_START` 插到**最后一个 CMD_BREAK 之后**（即被包裹命令之前） |
| 4 | `echo hi # comment` 把注释并入命令文本 | `#` 注释判定误用 `i!=start`（命令起点）；改为 `i!=word_start`（词起点） |
| 5 | `if ls; then ls > /tmp/out 2>&1; fi` 报 "unbalanced keywords"（`fi` 不识别） | 重定向目标后的 `;` 被目标附加逻辑吞掉后，`start` 停留在空白处，关键字检测 `i==start` 失败；空白分支现在在 pending 纯空白时前进 `start`，恢复命令位置不变式 |
| 6 | `echo "it's"` 误报 unterminated single quote | 双引号内的 `'` 是字面量；`case '\''` 增加 `quote` 保护 |
| 7 | `case x in ) …` 空模式未正确报错 | 空模式判定由 `start==i` 改为 `parse_span_is_blank(buf,start,i)` |
| 8 | 旧版 `parse_set_pipe_start` 从 `size-1` 起回退找 BREAK 并插在其前 | 同上 #3，改为从 `size` 起找最后一个 BREAK 并插在其后 |

> 说明：`a > b > c` 正确解析为 `REDIR_OUT(b) REDIR_OUT(c) EXE[a]`——目标 `b` 在紧随其后的
> 空格处即被第一个重定向消费，第二个 `>` 随后作为新重定向处理；只有 `a > > b`（重定向后
> 无目标词又遇操作符）才报错。

## 4. 清理不再需要的代码

- 被注释掉的 8 行静态全局状态块；
- `parse_deal_break()`（旧操作符分发，引用未定义全局，逻辑已内联）+ `parse_is_execute_break()`；
- `parse_emit_command()` / `parse_is_assignment()`（NAME=value 拆词逻辑，与"完整命令"设计冲突）；
- 结构体 `type_var` / `type_function` / `type_item` 与宏 `parse_create_item` / `parse_create_function`；
- 枚举中无人使用的 `CMD_CASE_ITEM`、`CMD_FUNCTION_START`、`CMD_FUNCTION_END`、`CMD_VAR`
  （case 模式与函数名复用 `CMD_EXECUTE` 承载完整命令原文）；
- 底部残缺的"Execute Part"：`parse_execute()`、`parse_execute_exe()`、`parse_escseq()`、
  `parse_execute_dollar()`、全局 `retval`、`IS_HEX/TO_HEX/IS_OCT` 宏；
- 旧版遗留的 `cnull`、未使用变量 `part` 等。

保留的对外接口不变：`parse_check()`（mnsh.h 已声明）、`parse_divide_command()`、
`parse_commands_free()`、`commands_t`/`type_execute`/`type_redir`。

## 5. 报错信息（每个 `parse_error=1` 处均有对应输出）

| 触发点 | 报错信息 |
|---|---|
| case 模式为空 | `parse: syntax error: empty case pattern` |
| 重定向无目标（后接操作符 / 空目标 / EOF，共 3 处） | `parse: syntax error: redirector without a target` |
| `\|` 前无命令 | `parse: syntax error near '\|'` |
| `&` 前无命令 | `parse: syntax error near '&'` |
| 悬空管道（EOF 时 `pipe_need_cmd`） | `parse: syntax error: expected a command after '\|'` |
| 关键字/分组不配平（`parse_validate`） | `parse: syntax error: unbalanced or mismatched keywords` |
| 未处理的操作符（防御性 default） | `parse: internal error: unhandled operator` |
| 无等待目标的重定向（防御性） | `parse: internal error: no redirector awaiting a target` |
| `>&`/`<&` 后不是合法 fd | `parse: invalid fd after '>&' or '<&': '%s'` |
| here-doc 到 EOF 未遇分隔符 | `parse: here-document delimited by end-of-file (wanted '%s')` |
| 未闭合单引号 | `parse: unterminated single quote` |
| 未闭合反引号（旧代码此处无信息，本轮补上） | `parse: unterminated backquote` |

## 6. 注释修复

- 文件头新增总体说明：解析器只识别结构，`type_execute.argraw` 保存完整命令原文、指向调用者
  缓冲、缓冲在 `commands_t` 存活期内不得释放；
- 更新 `parse_commands_free`、`type_execute.argraw`、`parse_emit_command_raw`、
  `parse_set_pipe_start`、`parse_is_fd_span`、`parse_span_is_blank` 等注释与实现一致；
- 枚举 `CMD_CASE_BREAK` 标注"仅作内部断点类型，不输出"。

## 7. 验证

测试驱动同步适配 `char *argraw`（`test.c`、`ptest.c` 各改一处打印逻辑）。

```sh
cc -std=gnu99 -O0 -g -Wall -Wextra -fsanitize=address,undefined -o ptest ptest.c && ./ptest
cc -std=gnu99 -O0 -g -fsanitize=address      -o leaktest leaktest.c && ./leaktest free
cc -std=gnu99 -O0 -g -fsanitize=address,undefined -o test test.c && ./test
```

| 项 | 结果 |
|---|---|
| `-Wall -Wextra` 编译 | 0 警告 |
| ptest 68 个用例 | 全部通过；10 个负向用例按预期打印 `parse error`；无超时、无 ASan/UBSan 报错 |
| leaktest free 模式（2000 轮） | 0 泄漏（LSan 无报告） |
| leaktest nofree 对照（2000 轮） | 泄漏 96000 字节，证明解析结果的所有权确在解析器堆对象中 |
| `test.c` 解析 `test.sh` | 成功，输出约 370 条操作码流（IF/FOR/CASE/FUNCTION/重定向/管道/后台齐全） |

代表性输出：

```
[pipe3] input: a | b | c
    0 CREATE_PIPE    1 EXECUTE [a]    2 CONTINUE_PIPE
    3 EXECUTE [b]    4 EXECUTE [c]    5 BREAK   6 DELETE_PIPE  7 BREAK
[if] input: if ls; then ls > test 2>&1; fi
    0 IF   1 EXECUTE [ls]   2 BREAK   3 THEN
    4 REDIR_OUT (fd=-1 file=test)   5 REDIR_DUP (fd=2 dup=1)   6 EXECUTE [ls]   7 FI
[func] input: foo() { echo hi; }
    0 FUNCTION   1 EXECUTE [foo]   2 PART_START   3 EXECUTE [echo hi]   4 BREAK   5 PART_END
```

## 8. 文件变更（未提交）

| 文件 | 变更 |
|---|---|
| `parser.c` | 整体重写（1489 → 1196 行）：完成 `type_execute` 完整命令设计、修复第 2/3 节全部问题、为每个 `parse_error` 添加报错信息、修复注释、清理第 4 节死代码 |
| `test.c` | `type_execute.argraw` 打印适配（词数组循环 → 直接 `%s`） |
| `ptest.c` | 同上（一处） |
| `leaktest.c` | 无改动 |
| `REPORT.md`（本文件） | 本轮修复报告（覆盖上一版审查报告） |

> 工作区中 `CDS/confc.h`、`CDS/darray.h`、`CDS/input.h` 的改动与新增的 `CDS/bitmap.*`
> 在本轮之前已存在，本轮未触碰。

## 9. 遗留事项

1. **执行器**：新操作码流仍未接入 `main.c`/`mnsh.c`（旧执行器使用已被删除的 `command_t`，
   `make` 仍有预先存在的编译错误，与本轮无关）。执行时需要：对 `type_execute.argraw` 做词切分
   与 `$` 展开（重定向已由解析器提取为 `type_redir`，fd/目标/here-doc body 已就绪）。
2. 交互续行（`parse_is_continue`）需感知关键字/管道/重定向，否则交互模式会把 `if ls` 之后
   的行误判为不完整。
3. `foo ()`（带空格）函数名、`x=(a b c)` 数组赋值的词内重组、case 模式内嵌 `(` 等边缘特性
   仍按上一版 REPORT 第 3 节列出的顺序延后（现在 `x=(a b c)` 会作为完整命令原文原样保留）。
4. here-doc 无引号分隔符时的体内 `$` 展开、CRLF/`\r` 处理。

## 10. 结构调整：goto 压平 `if(break_type!=CMD_RESERVED)` 内的嵌套

- 原结构为 `if(redir){…}else{switch(…)}`：分隔符 switch 及其 case 处于第 4 层嵌套。
- 改为直通式（goto）：
  - 重定向分支处理完成后 `goto L_after_operator`，直接跳到操作符后的前进/续扫代码，
    不再需要 `else`；
  - fd 词命中时 `goto L_insert_redir`，与未命中路径汇合，`if/else` 拆为两段直通代码；
  - 分隔符 switch 及其全部 case 因此整体上移一层（第 3 层）。
- 行为保持不变：重编译后 `-Wall -Wextra` 0 警告；ptest 68 用例输出与重构前**逐字节一致**
  （10 个负向用例照常报错）；leaktest free 模式仍 0 泄漏。

## 11. 用户修改部分：测试发现并修复的回归

用户在第 10 节结构之上又做了一处改动（把 `if(quote){ goto L; }` 改为合并式条件、并删除
`if(buf[i]==' '||buf[i]=='\t'||buf[i]=='\n'||break_type!=CMD_RESERVED)` 边界包裹），
该改动破坏了解析器，全部 68 个 ptest 用例（含空输入）报 `parse: internal error: unhandled operator`：

- 新条件 `quote||(buf[i]==' '&&buf[i]=='\t'&&buf[i]=='\n'&&break_type==CMD_RESERVED)`
  中 `' '&&'\t'` 恒为假，空白字符不再进入边界处理；
- 失去边界包裹后，分隔符 `switch` 对每个普通字符（`break_type==CMD_RESERVED`）都会命中
  `default` 分支 → 报错；
- 纯空白分支（word boundary 处理）变成不可达代码。

修复方式（保留用户"扁平 + 合并条件"的方向）：

- 条件改为正确的"普通字符"判定：`quote||(buf[i]!=' '&&buf[i]!='\t'&&buf[i]!='\n'&&break_type==CMD_RESERVED)`
  → 走 `L`（普通字符/引号）；其余（空白或操作符）进入边界分发；
- 在 is_redir 处理之后增加 `if(break_type==CMD_RESERVED) goto L_word_boundary;`，
  让纯空白分支经 `L_word_boundary:` 标签可达；
- 修正 switch case 的错乱缩进。

验证：ptest 68 用例输出与用户改动前**逐字节一致**（10 个负向用例照常报错，ASan/UBSan 0 报告）；
leaktest free 模式 0 泄漏；`test.c` 解析 `test.sh` 成功。

> `make` 仍失败，但那是 `mnsh.c` 使用已从 `mnsh.h` 删除的旧类型（`command_t`、`var_arr_t` 等）
> 的**预先存在**问题（见第 9 节遗留事项 1），与 parser 无关；`nmsh` 可正常构建。
