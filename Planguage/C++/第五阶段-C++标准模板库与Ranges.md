# 第五阶段：C++ 标准模板库与 Ranges

> 前置要求：掌握现代资源管理、值类别、Lambda 和多文件项目。  
> 建议标准：C++23  
> 建议周期：8～10 周，每天 1～2 小时  
> 阶段目标：能够根据数据特征选择容器，正确分析复杂度与失效规则，并使用标准算法和 Ranges 编写声明式数据处理代码。

---

## 一、学习成果

完成本阶段后，你应当能够：

1. 根据访问方式、插入位置、顺序要求和稳定性选择容器。
2. 解释容器常见操作的时间复杂度及内存布局。
3. 识别迭代器、引用和指针的失效场景。
4. 使用标准算法代替大部分手写遍历。
5. 正确编写谓词、投影、比较器和哈希函数。
6. 熟练使用 `optional`、`variant`、`any` 和 `expected`。
7. 使用 Ranges 算法和 Views 构建惰性数据管道。
8. 识别 dangling range、view 生命周期和多次遍历等问题。
9. 为容器与算法代码设计边界、复杂度和不变量测试。
10. 完成日志分析器、缓存和数据流水线等综合项目。

---

## 二、STL 的整体结构

标准模板库的核心组成包括：

```text
容器 ──提供数据──> 迭代器 ──连接──> 算法
                         ↑
                 函数对象、Lambda、投影
```

- 容器负责存储和管理元素。
- 迭代器表示位置和遍历能力。
- 算法处理迭代器或 Range 描述的序列。
- 函数对象和 Lambda 定义操作规则。

现代 C++ 应优先组合标准组件，而不是重复实现排序、查找、复制等基础设施。

### 2.1 选择容器时先问什么

1. 元素数量是固定还是动态？
2. 是否需要连续内存？
3. 最常见操作是随机访问、尾部插入还是中间插入？
4. 是否需要按键查找？
5. 是否需要保持排序？
6. 元素地址或迭代器是否必须稳定？
7. 是否允许重复键？
8. 能否接受哈希表额外内存和无序遍历？

### 2.2 默认选择

没有特殊要求时：

- 动态序列优先 `std::vector`。
- 固定长度优先 `std::array`。
- 键值查找优先根据是否需要排序选择 `unordered_map` 或 `map`。
- 不要因为中间插入是常数复杂度就默认选择链表；定位位置和缓存不友好可能更昂贵。

---

## 三、序列容器

### 3.1 `std::vector`

`vector` 将元素连续存储：

```cpp
std::vector<int> values{1, 2, 3};
values.push_back(4);
values.emplace_back(5);
```

常用接口：

- `size()`、`empty()`、`capacity()`。
- `reserve()`、`shrink_to_fit()`。
- `push_back()`、`emplace_back()`、`pop_back()`。
- `insert()`、`erase()`、`clear()`。
- `front()`、`back()`、`data()`。
- `operator[]` 和 `at()`。

典型复杂度：

| 操作 | 复杂度 |
|---|---|
| 随机访问 | O(1) |
| 尾部插入 | 摊还 O(1) |
| 中间插入/删除 | O(n) |
| 按值查找 | O(n) |

### 3.2 `size` 与 `capacity`

- `size`：已经构造的元素数量。
- `capacity`：不重新分配时最多可容纳的元素数量。

```cpp
std::vector<Item> items;
items.reserve(1000); // 预留空间，不创建 1000 个元素
```

已知大致数量时 `reserve` 可减少重新分配，但不要无依据为每个 vector 预留巨大空间。

### 3.3 `push_back` 与 `emplace_back`

```cpp
items.push_back(Item{1, "book"});
items.emplace_back(1, "book");
```

`emplace_back` 在容器中直接构造元素，但不应机械替换所有 `push_back`。已有对象、类型转换语义或可读性更重要时，`push_back` 很合适。

### 3.4 `std::deque`

`deque` 支持首尾高效插入删除和随机访问，但通常不是单块连续内存：

```cpp
std::deque<Task> tasks;
tasks.push_front(task);
tasks.push_back(other);
```

适合双端队列、滑动窗口等场景。不应把其元素区域当作连续数组传给 C API。

### 3.5 `std::list` 与 `forward_list`

- `list`：双向链表。
- `forward_list`：单向链表。

特点：

- 已知位置的插入删除可为 O(1)。
- 不支持随机访问。
- 每个节点有额外分配和指针开销。
- 缓存局部性通常较差。
- 某些操作可保持其他元素迭代器有效。

链表适合需要稳定节点、频繁拼接且已有位置的特殊场景，不是通用默认容器。

### 3.6 `std::array`

```cpp
std::array<int, 4> values{1, 2, 3, 4};
```

大小是类型的一部分，存储连续，适合固定长度数据，也可传给接受 Range 的标准算法。

### 3.7 `std::basic_string`

`std::string` 是针对字符序列优化的容器，具有连续存储和空字符结尾保证。它的迭代器失效也需根据修改操作判断，不要长期保存 `.data()` 或字符引用后继续修改字符串。

### 本节练习

1. 比较 `vector` 未 `reserve` 和预留后的容量变化。
2. 使用 `deque` 实现滑动窗口最大值的朴素版本。
3. 分析一个 `list` 方案能否用 `vector` 获得更好局部性。
4. 为固定棋盘选择合适容器并说明理由。

---

## 四、关联容器

### 4.1 有序关联容器

- `std::set`、`std::multiset`。
- `std::map`、`std::multimap`。

它们通常基于平衡树实现，按比较器定义的顺序存储：

```cpp
std::map<std::string, int> scores;
scores.emplace("Alice", 95);
scores.insert_or_assign("Bob", 88);
```

典型查找、插入和删除复杂度为 O(log n)。

### 4.2 `map` 的访问接口

```cpp
scores["Alice"] += 1;
```

`operator[]` 在键不存在时会插入默认值。只想查询时应使用：

```cpp
if (auto position = scores.find("Alice"); position != scores.end()) {
    std::cout << position->second;
}
```

或：

```cpp
if (scores.contains("Alice")) { }
```

`at()` 不插入，找不到时抛异常。

### 4.3 比较器要求

有序关联容器的比较器必须形成严格弱序。常见错误包括：

- 使用 `<=` 而不是 `<` 风格关系。
- 比较结果依赖不断变化的外部状态。
- 修改键中参与排序的内容。
- 对浮点 NaN 没有考虑比较语义。

```cpp
struct CaseInsensitiveLess
{
    using is_transparent = void;

    bool operator()(std::string_view left, std::string_view right) const;
};
```

透明比较器可允许使用 `string_view` 查询 `string` 键，避免构造临时字符串。

### 4.4 无序关联容器

- `unordered_set`、`unordered_multiset`。
- `unordered_map`、`unordered_multimap`。

平均查找、插入和删除通常为 O(1)，最坏可达 O(n)。

```cpp
std::unordered_map<std::string, std::size_t> frequencies;
++frequencies[word];
```

重要概念：

- bucket。
- load factor。
- rehash。
- 哈希函数。
- 相等判定。

### 4.5 哈希一致性

如果两个键根据相等比较相等，它们的哈希值必须相同：

```text
equal(a, b) == true  ⇒  hash(a) == hash(b)
```

反向不成立，不同键允许哈希碰撞。

### 4.6 自定义键

```cpp
struct Point
{
    int x{};
    int y{};

    bool operator==(const Point&) const = default;
};

struct PointHash
{
    std::size_t operator()(const Point& point) const noexcept
    {
        const auto first = std::hash<int>{}(point.x);
        const auto second = std::hash<int>{}(point.y);
        return first ^ (second << 1);
    }
};
```

教学示例中的简单组合并不保证工业级哈希质量；真实项目应评估输入分布与碰撞风险。

### 4.7 `flat_map` 与 `flat_set`（C++23）

它们提供有序关联接口，但通常基于连续存储：

- 查询具有良好缓存局部性。
- 插入删除可能移动大量元素。
- 适合查询多、修改少的集合。
- 迭代器失效特性与节点式 `map` 不同。

使用前检查标准库实现情况。

### 本节练习

1. 使用 `map` 和 `unordered_map` 分别实现单词频率统计。
2. 解释查询时使用 `operator[]` 的副作用。
3. 为不区分大小写的字符串键设计一致的比较或哈希与相等函数。
4. 为查询多、批量构建后很少修改的数据选择 `map` 或 `flat_map`。

---

## 五、容器适配器

### 5.1 `stack`

```cpp
std::stack<int> values;
values.push(1);
values.push(2);
values.top();
values.pop();
```

后进先出，适合表达式求值、撤销记录和深度优先遍历。

### 5.2 `queue`

先进先出，适合任务排队和广度优先搜索：

```cpp
std::queue<Task> tasks;
tasks.push(task);
tasks.front();
tasks.pop();
```

### 5.3 `priority_queue`

默认顶部是最大元素：

```cpp
std::priority_queue<int> values;
```

最小堆：

```cpp
std::priority_queue<int, std::vector<int>, std::greater<>> values;
```

适合调度、Top-K、Dijkstra 等算法。它不支持任意位置高效更新；需要 decrease-key 时可能采用插入新条目并忽略过期项等策略。

### 5.4 适配器的限制

适配器有意只公开特定访问模式。若需要遍历全部元素，应重新审视是否应直接使用底层容器，而不是破坏适配器抽象。

### 本节练习

1. 用 `stack` 检查括号匹配。
2. 用 `queue` 实现网格 BFS。
3. 用 `priority_queue` 求数据流中最大的 K 个元素。

---

## 六、迭代器

### 6.1 基本使用

```cpp
auto first = values.begin();
auto last = values.end();

for (auto iterator = first; iterator != last; ++iterator) {
    std::cout << *iterator << '\n';
}
```

`end()` 表示尾后位置，不能解引用。

### 6.2 迭代器类别

经典类别由弱到强：

1. 输入迭代器：单向读取。
2. 输出迭代器：单向写入。
3. 前向迭代器：可多次遍历。
4. 双向迭代器：可前进和后退。
5. 随机访问迭代器：支持常数时间跳转。
6. 连续迭代器（C++20）：元素物理连续。

算法只要求完成任务所需的最低能力。

### 6.3 `const_iterator`

```cpp
auto iterator = values.cbegin();
```

通过 const 迭代器不能修改元素。只读遍历优先表达 const 意图。

### 6.4 反向迭代器

```cpp
for (auto iterator = values.rbegin(); iterator != values.rend(); ++iterator) {
    std::cout << *iterator;
}
```

反向迭代器的 `base()` 指向关系容易产生偏移错误，进行正反向位置转换时应仔细验证。

### 6.5 失效规则

典型情况：

- `vector` 重新分配：所有迭代器、指针和引用失效。
- `vector` 未重新分配但在中间插入/删除：操作位置及之后通常失效。
- `deque` 的失效规则因操作而异，应查询具体接口。
- `list` 插入通常不影响其他迭代器；删除只使被删元素失效。
- `map/set` 插入通常不使现有迭代器失效；删除只使被删元素失效。
- `unordered_*` rehash 会使迭代器失效，但对元素的引用和指针规则应查具体标准接口。

不要凭印象记一个“所有容器统一规则”。修改前查询对应容器和操作。

### 6.6 擦除循环

```cpp
for (auto iterator = values.begin(); iterator != values.end();) {
    if (should_remove(*iterator)) {
        iterator = values.erase(iterator);
    } else {
        ++iterator;
    }
}
```

C++20 可使用：

```cpp
std::erase_if(values, predicate);
```

### 本节练习

1. 记录 `vector` 扩容前后的元素地址，观察失效。
2. 修复在遍历中删除元素造成的错误。
3. 判断不同算法需要哪种最低迭代器能力。
4. 比较容器 const 和非 const 迭代器行为。

---

## 七、标准算法

### 7.1 非修改算法

常用算法：

- `find`、`find_if`。
- `count`、`count_if`。
- `all_of`、`any_of`、`none_of`。
- `equal`、`mismatch`。
- `search`。

```cpp
const auto position = std::find_if(
    users.begin(), users.end(),
    [](const User& user) { return user.active(); });
```

### 7.2 修改算法

- `copy`、`move`。
- `transform`。
- `replace`。
- `remove`、`remove_if`。
- `unique`。
- `fill`、`generate`。

`remove` 不会改变容器大小，它把保留元素移动到前方并返回新逻辑末尾：

```cpp
values.erase(
    std::remove(values.begin(), values.end(), target),
    values.end());
```

C++20 对标准容器可优先使用 `std::erase` 或 `std::erase_if`。

### 7.3 排序与分区

- `sort`、`stable_sort`。
- `partial_sort`。
- `nth_element`。
- `partition`、`stable_partition`。
- `is_sorted`。

不需要完整排序时：

- 只要第 k 个位置：`nth_element`。
- 只要最小的 k 项有序：`partial_sort`。
- 只要分为两组：`partition`。

### 7.4 二分查找

- `lower_bound`。
- `upper_bound`。
- `equal_range`。
- `binary_search`。

前置条件是范围已按相容规则排序。违反前置条件不会自动替你排序。

### 7.5 集合算法

对已排序范围：

- `set_union`。
- `set_intersection`。
- `set_difference`。
- `includes`。

### 7.6 数值算法

- `accumulate`。
- `reduce`。
- `inner_product`。
- `partial_sum`。
- `inclusive_scan`、`exclusive_scan`。
- `iota`。

`reduce` 允许以不同顺序组合，运算需满足相应结合等要求；浮点加法受顺序影响，结果可能不同。

### 7.7 输出迭代器

```cpp
std::vector<int> result;
std::transform(
    input.begin(), input.end(),
    std::back_inserter(result),
    [](int value) { return value * value; });
```

若能预知数量，可先 `reserve`。

### 7.8 算法还是循环

优先标准算法，当它清晰表达意图。以下情况循环可能更易读：

- 一个遍历需要维护多个相互关联状态。
- 存在复杂早退或多分支控制。
- 算法组合反而晦涩。

目标是表达清楚，而不是消灭所有 `for`。

### 本节练习

1. 将五段手写查找、计数、删除循环改为标准算法。
2. 用 `nth_element` 求中位数。
3. 用 `lower_bound` 在有序序列中查找插入位置。
4. 比较 `sort` 与 `stable_sort` 对等价元素次序的影响。
5. 使用 scan 算法计算前缀和。

---

## 八、谓词、比较器与投影

### 8.1 谓词要求

谓词应把参数当作只读输入，不应依赖调用次数或随意修改元素：

```cpp
auto adult = [](const Person& person) {
    return person.age() >= 18;
};
```

算法可能以你未预期的次数和顺序调用谓词。

### 8.2 比较器

```cpp
std::sort(people.begin(), people.end(),
    [](const Person& left, const Person& right) {
        return left.age() < right.age();
    });
```

比较器必须形成严格弱序。不要用 `<=`。

### 8.3 投影

Ranges 算法支持投影：

```cpp
std::ranges::sort(people, {}, &Person::age);
```

含义是使用默认比较器比较每个元素投影得到的年龄。

```cpp
auto position = std::ranges::find(people, "Alice", &Person::name);
```

投影能减少重复 Lambda，并清晰表达“按哪个字段操作”。

### 8.4 有状态函数对象

```cpp
struct Near
{
    int target{};
    int tolerance{};

    bool operator()(int value) const noexcept
    {
        return std::abs(value - target) <= tolerance;
    }
};
```

当规则有名称、状态或会复用时，函数对象可能比长 Lambda 更清晰。

---

## 九、词汇类型

### 9.1 `std::pair` 与 `std::tuple`

```cpp
std::pair<std::string, int> result{"Alice", 95};
auto [name, score] = result;
```

`pair` 适合两个含义清楚的临时组合。字段业务意义重要时，具名结构体通常更好。

`tuple` 适合泛型设施或短期组合；大量 `.get<2>()` 往往说明应设计结构体。

### 9.2 `std::optional`

表示“可能有一个 `T`”：

```cpp
std::optional<User> find_user(std::string_view name);

if (auto user = find_user("Alice")) {
    std::cout << user->name();
}
```

常用接口：

- `has_value()`、`operator bool`。
- `value()`。
- `value_or()`。
- `and_then`、`transform`、`or_else`（C++23）。

不要无检查使用 `*optional`。`optional<T&>` 不属于当前标准库；可用指针、`reference_wrapper` 或重新设计接口。

### 9.3 `std::variant`

表示一组类型中恰好一个：

```cpp
using Value = std::variant<int, double, std::string>;
Value value = 42;
```

访问方式：

```cpp
std::visit([](const auto& item) {
    std::cout << item;
}, value);
```

访问者可使用 overload 技巧：

```cpp
template<class... Ts>
struct Overload : Ts...
{
    using Ts::operator()...;
};
template<class... Ts>
Overload(Ts...) -> Overload<Ts...>;
```

`variant` 适合封闭类型集合，编译器可帮助检查分支；虚函数适合开放继承层次。二者不是简单的谁取代谁。

### 9.4 `std::any`

```cpp
std::any value = std::string{"hello"};
if (const auto* text = std::any_cast<std::string>(&value)) {
    std::cout << *text;
}
```

`any` 可保存几乎任意可复制类型，但失去编译期类型集合信息。适合插件属性、少量动态元数据等场景，不应替代清晰的数据模型。

### 9.5 `std::expected`（C++23）

表示一个成功值或错误值：

```cpp
enum class ParseError
{
    empty,
    invalid,
    out_of_range
};

std::expected<int, ParseError> parse_integer(std::string_view text);
```

```cpp
auto result = parse_integer(input);
if (result) {
    std::cout << *result;
} else {
    handle_error(result.error());
}
```

与 `optional` 相比，`expected` 保存失败原因；与异常相比，错误是函数类型签名的显式组成部分。选择取决于错误频率、传播方式和项目约定。

### 9.6 `std::span` 与 `std::mdspan`

- `span`：非拥有连续一维视图。
- `mdspan`（C++23）：非拥有多维数据视图，分离数据句柄、维度和布局映射。

二者都不负责底层数据生命周期。

### 本节练习

1. 将“未找到返回 -1”的接口改为 `optional`。
2. 用 `variant` 表示配置值并完整访问所有类型。
3. 用 `expected` 设计整数解析错误。
4. 判断某场景应使用结构体、tuple、variant 还是虚函数。

---

## 十、Ranges 基础

### 10.1 Range 概念

Range 是能够取得开始和结束位置的对象。C++20 Ranges 算法可以直接接受整个范围：

```cpp
std::ranges::sort(values);
auto position = std::ranges::find(values, target);
```

相比经典迭代器对：

- 参数更少。
- 更容易组合投影。
- Concepts 提供更清晰的约束。
- 返回类型可携带更丰富信息。

### 10.2 View

View 是轻量、通常非拥有、惰性的范围适配器：

```cpp
auto result = values
    | std::views::filter([](int value) { return value % 2 == 0; })
    | std::views::transform([](int value) { return value * value; });
```

创建管道时通常没有处理全部元素；遍历时才逐步计算。

### 10.3 常用 Views

- `filter`：筛选。
- `transform`：转换。
- `take`、`drop`。
- `take_while`、`drop_while`。
- `reverse`。
- `split`、`join`。
- `keys`、`values`。
- `elements<N>`。
- `iota`。
- `common`。

C++23 常用扩展：

- `zip`、`zip_transform`。
- `adjacent`、`adjacent_transform`。
- `chunk`、`slide`。
- `chunk_by`。
- `stride`。
- `repeat`。
- `enumerate`。
- `cartesian_product`。

具体可用性取决于标准库版本。

### 10.4 物化结果

C++23：

```cpp
auto result = source
    | std::views::filter(predicate)
    | std::ranges::to<std::vector>();
```

View 管道本身通常不是拥有结果；需要长期保存独立数据时，应物化到容器中。

### 10.5 惰性的影响

```cpp
int calls{};
auto view = values | std::views::transform([&](int value) {
    ++calls;
    return value * 2;
});
```

只有遍历时 `calls` 才增加；重复遍历可能重复计算。具有副作用的 View 转换会让行为难以推断，应尽量保持纯函数。

### 10.6 生命周期与 dangling

View 常引用外部数据：

```cpp
auto make_bad_view()
{
    std::vector<int> local{1, 2, 3};
    return local | std::views::filter([](int value) { return value > 1; });
}
```

返回后局部容器销毁，View 可能悬空。需要：

- 让数据所有者活得足够久。
- 返回拥有数据的结果容器。
- 使用适当的 owning view 设计。

### 10.7 借用范围

borrowed range 允许迭代器在临时 Range 对象销毁后仍有效，因为迭代器指向的数据由其他对象拥有。Ranges 算法会在可能悬空时返回 `std::ranges::dangling`，帮助阻止错误使用。

### 10.8 View 不一定是普通容器

某些 View：

- 不能随机访问。
- 不知道常数时间大小。
- 只能遍历一次。
- 迭代器和 sentinel 类型不同。
- 元素可能是计算出的临时值或代理。

泛型代码不应假设所有 Range 都像 `vector`。

### 本节练习

1. 用管道筛选偶数、平方并取前五项。
2. 用 `split` 处理简单分隔文本。
3. 用 `enumerate` 输出索引和值。
4. 复现返回局部数据 View 的生命周期错误并修复。
5. 比较惰性 View 重复遍历与物化结果的成本。

---

## 十一、自定义 Range 与算法接口

### 11.1 先接受 Range

```cpp
template<std::ranges::input_range Range>
auto sum(Range&& range)
{
    using Value = std::ranges::range_value_t<Range>;
    return std::accumulate(
        std::ranges::begin(range),
        std::ranges::end(range),
        Value{});
}
```

若算法只需输入范围，不要要求随机访问或具体 `vector`。

### 11.2 元素相关类型

- `range_value_t<R>`：值类型。
- `range_reference_t<R>`：解引用得到的类型。
- `range_difference_t<R>`：距离类型。
- `iterator_t<R>`、`sentinel_t<R>`。

不要假设解引用一定得到 `T&`；代理引用和计算 View 可能返回其他类型。

### 11.3 自定义 View 的边界

自定义 View 涉及 `view_interface`、迭代器、sentinel、Concepts 和生命周期设计。优先组合标准 View；只有复用价值明显且性能需求明确时再实现自定义 View。

### 11.4 管道可读性

```cpp
auto active_names = users
    | std::views::filter(&User::active)
    | std::views::transform(&User::name);
```

管道适合线性数据变换。若存在复杂分支、多状态聚合或难以调试的副作用，命名中间步骤或普通循环可能更清楚。

---

## 十二、复杂度与性能思维

### 12.1 大 O 不是全部

还要考虑：

- 常数因子。
- 内存分配次数。
- 缓存局部性。
- 分支预测。
- 元素移动与复制成本。
- 数据规模和访问模式。

小规模数据中，连续容器的线性查找可能比树或哈希表更快。

### 12.2 摊还复杂度

`vector::push_back` 的摊还 O(1) 意味着大多数插入便宜，偶尔扩容较贵，长序列平均下来为常数级；不表示每一次都是 O(1)。

### 12.3 避免重复查找

```cpp
if (map.contains(key)) {
    auto value = map.at(key); // 查找两次
}
```

可使用一次 `find` 并复用迭代器。

### 12.4 避免隐式插入

只读查询不要使用 `map[key]`，否则会改变容器。计数场景中 `++map[key]` 则正好利用默认零值，应按意图选择。

### 12.5 节点句柄

C++17 节点式关联容器支持 `extract`，可以不复制/移动元素地修改键或在兼容容器间转移节点：

```cpp
auto node = source.extract(key);
if (node) {
    node.key() = new_key;
    destination.insert(std::move(node));
}
```

需要维持唯一键和比较/哈希约束。

### 12.6 基准注意事项

- 使用 Release 优化构建。
- 防止编译器删除无用计算。
- 预热并多次运行。
- 使用现实数据分布。
- 分离构建数据与测量操作。
- 报告规模、硬件和编译参数。

---

## 十三、阶段项目

### 项目一：日志分析器

最低要求：

- 读取日志记录并解析时间、级别、模块和消息。
- 使用 `expected` 返回解析结果或具体错误。
- 按级别和模块统计数量。
- 支持关键词筛选和时间排序。
- 使用标准算法完成查找、排序和聚合。
- 对无效记录给出统计报告。

进阶要求：

- 使用 Ranges 构建查询管道。
- 输出 Top-K 高频错误。
- 支持多种日志记录 `variant`。
- 比较 `map` 与 `unordered_map` 的性能。

### 项目二：LRU 缓存

最低要求：

- 固定最大容量。
- 查询和更新平均 O(1)。
- 最久未使用项优先淘汰。
- 使用 `list` 保存访问顺序，`unordered_map` 保存键到节点位置。
- 正确处理迭代器稳定性和同步更新不变量。

核心不变量：

- map 中每个迭代器都指向 list 中有效节点。
- list 和 map 的键集合相同。
- 元素数量不超过容量。

进阶要求：

- 模板化键、值、哈希和相等函数。
- 统计命中率。
- 编写随机操作测试验证不变量。

### 项目三：命令行数据查询工具

最低要求：

- 加载 CSV 风格数据。
- 支持按字段筛选、排序、限制数量。
- 使用 `variant` 表示整数、浮点和文本字段。
- 使用 Ranges 组合查询步骤。
- 使用 `ranges::to` 物化最终结果。

进阶要求：

- 支持分组聚合。
- 支持多个排序键。
- 分析 View 生命周期和重复计算。
- 输出查询执行统计。

### 项目四：图算法工具箱

最低要求：

- 使用邻接表表示图。
- BFS 使用 `queue`。
- DFS 使用递归或 `stack`。
- Dijkstra 使用 `priority_queue`。
- 使用 `optional` 表示不存在的路径。
- 返回具名路径结果结构体。

进阶要求：

- 支持字符串节点键。
- 实现拓扑排序和连通分量。
- 对稠密图和稀疏图比较表示方式。

### 项目验收标准

- 容器选择有明确依据。
- 能写出关键操作复杂度。
- 所有保存的迭代器都有稳定性说明。
- 查询操作不会意外修改容器。
- 比较器满足严格弱序，哈希与相等保持一致。
- 标准算法优先于等价手写循环。
- View 不引用已销毁数据。
- 错误和“无结果”使用合适词汇类型表达。
- 测试覆盖空容器、单元素、重复元素和大数据。
- 严格警告、Sanitizer 和测试通过。

---

## 十四、九周学习安排

### 第 1 周：容器选择与 `vector`

- STL 结构、复杂度。
- vector 的 size、capacity、reserve。
- 连续内存和失效规则。

### 第 2 周：其他序列容器

- deque、list、forward_list、array。
- 容器适配器。
- 比较内存布局和访问模式。

### 第 3 周：关联容器

- map/set 与 multimap/multiset。
- unordered 容器、bucket 和 rehash。
- 比较器、哈希与透明查询。

### 第 4 周：迭代器

- 迭代器类别。
- const 与反向迭代器。
- 失效规则和安全擦除。

### 第 5 周：标准算法

- 查找、计数、变换、删除。
- 排序、分区、二分查找。
- 数值算法与输出迭代器。

### 第 6 周：词汇类型

- pair、tuple、optional。
- variant、visit、any。
- expected 和显式错误建模。

### 第 7 周：Ranges

- Range Concepts 和算法。
- Views 与惰性求值。
- C++23 View 扩展。
- 生命周期和 dangling。

### 第 8 周：复杂度与项目设计

- 缓存局部性和隐藏复制。
- 节点句柄。
- 设计 LRU 或日志分析器。

### 第 9 周：项目完成与复盘

- 完成综合项目。
- 检查容器选择和失效风险。
- 运行测试、警告和 Sanitizer。
- 完成阶段自测。

---

## 十五、阶段自测

### 概念题

1. 为什么 `vector` 是动态序列的默认选择？
2. `size()` 与 `capacity()` 有什么区别？
3. `reserve()` 是否创建元素？
4. `deque` 与 `vector` 的内存连续性有何差异？
5. 链表 O(1) 插入为什么不意味着一定更快？
6. `map::operator[]` 对不存在键有什么行为？
7. 有序比较器为什么不能简单使用 `<=`？
8. 哈希函数和相等函数必须满足什么关系？
9. rehash 会影响哪些迭代器或观察位置？
10. `end()` 为什么不能解引用？
11. 什么操作会使 `vector` 迭代器失效？
12. erase-remove 惯用法为什么需要两个步骤？
13. `nth_element` 与完整排序适用场景有何区别？
14. 二分查找算法的前置条件是什么？
15. 投影与比较器分别负责什么？
16. `optional` 和 `expected` 的差异是什么？
17. `variant` 与虚函数多态如何选择？
18. `any` 为什么不应替代清晰的数据模型？
19. View 为什么被称为惰性且通常非拥有？
20. 什么是 borrowed range 和 dangling？

### 编程题

1. 使用标准算法实现去重排序。
2. 使用透明比较器查询字符串 map。
3. 为自定义键实现一致哈希与相等。
4. 用 `expected` 实现带错误位置的解析器。
5. 用 `variant` 和 `visit` 实现小型表达式值。
6. 用 Ranges 管道完成筛选、转换、排序前的数据准备。
7. 实现并验证 LRU 缓存不变量。
8. 使用合适算法求 Top-K 和中位数。

### 进入下一阶段前的检查清单

- [ ] 能根据访问模式选择容器。
- [ ] 能说明关键操作的复杂度。
- [ ] 熟悉常见迭代器失效规则。
- [ ] 会安全地在遍历中删除元素。
- [ ] 熟练使用查找、排序、变换和数值算法。
- [ ] 能设计严格弱序比较器。
- [ ] 能保持哈希和相等判定一致。
- [ ] 熟练使用 optional、variant 和 expected。
- [ ] 能使用 Ranges 算法、投影和 View 管道。
- [ ] 能识别 View 生命周期问题。
- [ ] 完成至少两个阶段项目。
- [ ] 项目通过测试、严格警告和 Sanitizer。

---

## 十六、易错点汇总

1. 不分析需求就默认选择链表或哈希表。
2. 把 `reserve` 当成创建元素。
3. 长期保存 vector 元素指针后继续触发扩容。
4. 遍历容器时错误地删除当前元素。
5. 只读查询 map 却使用 `operator[]`。
6. 比较器使用 `<=` 或依赖可变外部状态。
7. 相等键产生不同哈希值。
8. 认为 unordered 容器操作永远 O(1)。
9. 解引用 `end()`。
10. 在失效后继续使用迭代器。
11. 误以为 `remove` 会缩小容器。
12. 未排序就调用二分查找或集合算法。
13. 完整排序只为获得第 k 个元素。
14. 谓词修改元素或依赖调用次数。
15. 无检查解引用 optional。
16. 使用 tuple 隐藏重要字段含义。
17. 使用 any 逃避类型设计。
18. 返回引用局部容器的 View。
19. 假设所有 Range 都可多次遍历或随机访问。
20. 在 View 转换中加入难以预测的副作用。

---

## 十七、下一阶段

下一阶段建议进入模板与泛型编程：

- 函数模板、类模板和变量模板。
- 模板参数推导与 CTAD。
- 特化、可变参数模板和折叠表达式。
- type traits、`decltype` 和 `std::invoke`。
- SFINAE、检测惯用法和 tag dispatch。
- Concepts、requires-expression 和约束排序。
- 编译期编程与泛型库设计。

完成本阶段的真正标志，是面对一个数据处理问题时，能够选择合适容器和算法，说明复杂度与失效边界，并写出没有生命周期隐患的 Ranges 管道。
