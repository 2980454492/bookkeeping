// ═══════════════════════════════════════════════════════════════════
// app.js — 前端控制器
//
// 职责：通过 HTTP 请求与 C++ 后端 (handlers.cpp) 通信，读写 DOM 更新界面。
// 不会直接调用 C++ 函数；所有数据交换均走 REST API (/api/*)。
// ═══════════════════════════════════════════════════════════════════

// ── 全局状态（客户端内存缓存，类似 C++ 里的全局/成员变量） ────────

// 分类数据：从 GET /api/categories 加载后缓存于此
// expense / income 分别对应支出、收入的一级分类数组
let categories = { expense: [], income: [] };

// 当前页展示的记录列表：从 GET /api/records 加载后缓存于此
let currentRecords = [];

// 分页：当前页码（从 1 开始）
let currentPage = 1;

// 每页记录条数，与后端 RecordQuery.page_size 默认值一致
const PAGE_SIZE = 50;

// ── API 请求封装（HTTP 客户端，类似封装好的 REST SDK） ─────────────
// fetch 是浏览器内置的 HTTP 函数；async/await 用于等待异步网络响应。
const API = {
    // GET 请求：读取数据（分类列表、记录列表等）
    async get(url) {
        const res = await fetch(url);               // 发起 HTTP GET
        if (!res.ok) throw new Error(await res.text()); // res.ok=false 表示 4xx/5xx
        return res.json();                          // 将响应体解析为 JS 对象（JSON 反序列化）
    },

    // POST 请求：创建数据（新增记录等）
    async post(url, data) {
        const res = await fetch(url, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' }, // 告知后端 body 是 JSON
            body: JSON.stringify(data)                     // JS 对象 → JSON 字符串
        });
        if (!res.ok) throw new Error(await res.text());
        return res.json();
    },

    // PUT 请求：更新已有数据（编辑记录等）
    async put(url, data) {
        const res = await fetch(url, {
            method: 'PUT',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(data)
        });
        if (!res.ok) throw new Error(await res.text());
        return res.json();
    },

    // DELETE 请求：删除数据（删除记录等）
    async del(url) {
        const res = await fetch(url, { method: 'DELETE' });
        if (!res.ok) throw new Error(await res.text());
        return res.json();
    }
};

// ── 分类工具函数 ────────────────────────────────────────────────────

// 按类型（"expense" | "income"）从缓存中取一级分类列表
function getCategoriesByType(type) {
    return categories[type] || [];  // 不存在则返回空数组
}

// 根据一级分类名称，从缓存中查找其二级分类名称列表
// 注意：二级分类已在 GET /api/categories 响应的 subcategories 字段中，无需再请求后端
function getSubcategories(l1Name) {
    const all = [...categories.expense, ...categories.income]; // 展开合并两个数组
    const found = all.find(c => c.name === l1Name);            // 按 name 查找
    return found ? (found.subcategories || []) : [];
}

// ── 加载分类 ────────────────────────────────────────────────────────

// 从后端拉取全部分类，写入全局 categories 缓存
async function loadCategories() {
    try {
        const data = await API.get('/api/categories');  // → handlers.cpp GET /api/categories
        categories.expense = [];
        categories.income = [];
        // 后端返回 JSON 数组，按 type 字段拆分到两个列表
        for (const cat of data) {
            if (cat.type === 'expense') categories.expense.push(cat);
            else categories.income.push(cat);
        }
        console.log('[App] 分类已加载:', categories.expense.length + categories.income.length);
    } catch (e) {
        console.error('[App] 加载分类失败:', e);
    }
}

// 将 categories 缓存中的数据填充到页面上的 <select> 下拉框
// 调用时机：init() 启动时、用户切换「支出/收入」类型时
function populateCategorySelects() {
    // document.getElementById(id) 通过 HTML 元素的 id 属性找到 DOM 节点
    // .value 读取 <select> 当前选中项的 value 属性
    const fType = document.getElementById('fType').value;
    const fCatL1 = document.getElementById('fCatL1');       // 新增表单：一级分类

    const cats = getCategoriesByType(fType);  // 仅取当前类型（支出/收入）的分类

    // innerHTML：直接替换元素的 HTML 内容（此处动态生成 <option> 列表）
    fCatL1.innerHTML = '<option value="">请选择</option>';
    for (const cat of cats) {
        // 模板字符串 `...${变量}...` 用于拼接 HTML；escapeHtml 防止 XSS
        fCatL1.innerHTML += `<option value="${escapeHtml(cat.name)}">${cat.icon || ''} ${cat.name}</option>`;
    }
    updateL2Select();  // 一级分类变更后同步更新二级分类
}

// 填充筛选栏的一级分类下拉框（显示所有类型，不过滤 expense/income）
// 仅在初始化时调用一次，避免新增表单切换类型时被连带重置
function populateFilterCategorySelect() {
    const filterCatL1 = document.getElementById('filterCatL1');
    filterCatL1.innerHTML = '<option value="">全部分类</option>';
    for (const cat of [...categories.expense, ...categories.income]) {
        filterCatL1.innerHTML += `<option value="${escapeHtml(cat.name)}">${cat.name}</option>`;
    }
}

// 根据当前选中的一级分类，填充/隐藏二级分类下拉框
function updateL2Select() {
    const fCatL1 = document.getElementById('fCatL1');
    const catL2Group = document.getElementById('catL2Group'); // 二级分类整行容器
    const fCatL2 = document.getElementById('fCatL2');
    const fType = document.getElementById('fType').value;

    // 收入类型无二级分类，隐藏该行
    if (fType === 'income') {
        catL2Group.style.display = 'none';  // CSS display 属性控制可见性
        fCatL2.innerHTML = '<option value="">—</option>';
        return;
    }

    catL2Group.style.display = '';
    const l1Name = fCatL1.value;                    // 当前选中的一级分类名
    const subs = getSubcategories(l1Name);          // 从缓存查找二级分类

    fCatL2.innerHTML = '<option value="">请选择</option>';
    for (const sub of subs) {
        fCatL2.innerHTML += `<option value="${escapeHtml(sub)}">${sub}</option>`;
    }
}

// 根据筛选栏当前选中的一级分类，填充其二级分类下拉框
// 一级分类为空或所选分类无二级时，仅保留「全部二级」占位项
function updateFilterL2Select() {
    const filterCatL1 = document.getElementById('filterCatL1').value;
    const filterCatL2 = document.getElementById('filterCatL2');
    const subs = getSubcategories(filterCatL1);

    filterCatL2.innerHTML = '<option value="">全部二级</option>';
    for (const sub of subs) {
        filterCatL2.innerHTML += `<option value="${escapeHtml(sub)}">${sub}</option>`;
    }
}

// ── 加载记录 ────────────────────────────────────────────────────────

// 从后端拉取记录列表，更新缓存并刷新表格和汇总
async function loadRecords() {
    // URLSearchParams：构建 query string，等价于 C++ 里拼接 ?key=value&...
    const params = new URLSearchParams();

    // 读取筛选面板各控件的值（空值表示该维度不参与筛选）
    const filterType    = document.getElementById('filterType').value;
    const filterCatL1   = document.getElementById('filterCatL1').value;
    const filterCatL2   = document.getElementById('filterCatL2').value;
    const dateFrom      = document.getElementById('filterDateFrom').value;
    const dateTo        = document.getElementById('filterDateTo').value;
    const amountMin     = document.getElementById('filterAmountMin').value;
    const amountMax     = document.getElementById('filterAmountMax').value;
    const keyword       = document.getElementById('filterKeyword').value.trim();
    const sortBy        = document.getElementById('filterSortBy').value;
    const sortOrder     = document.getElementById('filterSortOrder').value;

    if (filterType)  params.set('type', filterType);       // 对应后端 parseRecordQuery().type
    if (filterCatL1) params.set('cat_l1', filterCatL1);    // 对应后端 parseRecordQuery().cat_l1
    if (filterCatL2) params.set('cat_l2', filterCatL2);    // 对应后端 parseRecordQuery().cat_l2
    if (dateFrom)    params.set('date_from', dateFrom);    // 精确到天，后端自动补全为当天 00:00
    if (dateTo)      params.set('date_to', dateTo);        // 后端自动补全为当天 23:59
    if (amountMin !== '') params.set('amount_min', amountMin);
    if (amountMax !== '') params.set('amount_max', amountMax);
    if (keyword)     params.set('keyword', keyword);       // 模糊匹配备注/分类/金额

    params.set('page', currentPage);
    params.set('page_size', PAGE_SIZE);
    params.set('sort_by', sortBy || 'datetime');
    params.set('sort_order', sortOrder || 'desc');

    try {
        const data = await API.get('/api/records?' + params.toString()); // → db.queryRecords()
        currentRecords = data.records || [];  // 缓存当前页记录
        // textContent：设置元素的纯文本内容（不含 HTML 标签）
        document.getElementById('recordCount').textContent =
            `${data.total || 0} 条记录`;
        renderRecords();    // 将 currentRecords 渲染到表格
        updateSummary();    // 重新计算总收入/支出/结余
    } catch (e) {
        console.error('[App] 加载记录失败:', e);
    }
}

// ── 渲染记录表格 ────────────────────────────────────────────────────

// 根据 currentRecords 生成 <tr> 行 HTML，写入 #recordBody
function renderRecords() {
    const tbody = document.getElementById('recordBody');
    if (currentRecords.length === 0) {
        tbody.innerHTML = '<tr class="empty-row"><td colspan="7">暂无记录，添加一条吧 🎉</td></tr>';
        return;
    }
    // Array.map：遍历数组，每个元素生成一段 HTML 字符串
    // .join('')：将字符串数组合并为一个完整 HTML
    tbody.innerHTML = currentRecords.map(r => {
        const typeLabel = r.type === 'income' ? '收入' : '支出';
        const typeClass = r.type === 'income' ? 'type-income' : 'type-expense';
        const amountClass = r.type === 'income' ? 'amount-income' : 'amount-expense';
        const sign = r.type === 'income' ? '+' : '-';
        return `
            <tr>
                <td>${escapeHtml(r.datetime)}</td>
                <td><span class="type-tag ${typeClass}">${typeLabel}</span></td>
                <td class="${amountClass}">${sign}¥${r.amount.toFixed(2)}</td>
                <td>${escapeHtml(r.category_l1)}</td>
                <td>${escapeHtml(r.category_l2 || '—')}</td>
                <td>${escapeHtml(r.note || '')}</td>
                <td>
                    <button class="btn btn-edit" onclick="openEditModal(${r.id})">编辑</button>
                    <button class="btn btn-danger" onclick="deleteRecord(${r.id})">删除</button>
                </td>
            </tr>
        `;
    }).join('');
}

// ── 汇总统计 ────────────────────────────────────────────────────────

// 计算并更新页面顶部的「总收入 / 总支出 / 结余」卡片
async function updateSummary() {
    try {
        let totalIncome = 0, totalExpense = 0;
        // 拉取大量记录用于汇总（不受筛选栏影响；page_size=10000 近似全量）
        const allData = await API.get('/api/records?page=1&page_size=10000');
        for (const r of (allData.records || [])) {
            if (r.type === 'income') totalIncome += r.amount;
            else totalExpense += r.amount;
        }
        document.getElementById('sumIncome').textContent = `¥${totalIncome.toFixed(2)}`;
        document.getElementById('sumExpense').textContent = `¥${totalExpense.toFixed(2)}`;
        const balance = totalIncome - totalExpense;
        const balEl = document.getElementById('sumBalance');
        balEl.textContent = `${balance >= 0 ? '+' : ''}¥${balance.toFixed(2)}`;
        balEl.style.color = balance >= 0 ? '#52c41a' : '#ff4d4f';  // 正数绿色，负数红色
    } catch (e) {
        console.error('[App] 更新汇总失败:', e);
    }
}

// ── 新增记录 ────────────────────────────────────────────────────────

// 表单提交回调：由 addForm 的 'submit' 事件触发（见 init() 中的 addEventListener）
async function addRecord(e) {
    e.preventDefault();  // 阻止表单默认行为（否则会整页刷新并 POST 到当前 URL）

    // 从各表单控件读取用户输入（id 对应 index.html 中的元素）
    const type = document.getElementById('fType').value;
    const amount = parseFloat(document.getElementById('fAmount').value);
    const datetime = document.getElementById('fDatetime').value;
    const catL1 = document.getElementById('fCatL1').value;
    const catL2 = document.getElementById('fCatL2').value;
    const note = document.getElementById('fNote').value;

    // 前端校验（后端 handlers.cpp POST /api/records 也会再次校验）
    if (!catL1) {
        alert('请选择一级分类');
        return;
    }
    if (type === 'expense' && !catL2) {
        alert('请选择二级分类');
        return;
    }
    if (!datetime) {
        alert('请选择时间');
        return;
    }

    // <input type="datetime-local"> 格式为 "YYYY-MM-DDTHH:MM"
    // 后端期望格式为 "YYYY-MM-DD HH:MM:SS"
    const formatted = datetime.replace('T', ' ') + ':00';

    try {
        await API.post('/api/records', {  // → handlers.cpp → db.createRecord()
            datetime: formatted,
            type: type,
            amount: amount,
            category_l1: catL1,
            category_l2: type === 'income' ? '' : catL2,
            note: note
        });
        // 提交成功后清空表单
        document.getElementById('fAmount').value = '';
        document.getElementById('fNote').value = '';
        document.getElementById('fDatetime').value = '';
        document.getElementById('fCatL1').value = '';
        document.getElementById('fCatL2').value = '';
        updateL2Select();
        await loadRecords();  // 重新拉取列表，刷新表格
    } catch (e) {
        alert('添加失败: ' + e.message);
    }
}

// ── 删除记录 ────────────────────────────────────────────────────────

// 由表格中删除按钮的 onclick="deleteRecord(id)" 调用
// 注意：deleteRecord 未用 addEventListener 绑定，而是通过 inline onclick 直接引用
async function deleteRecord(id) {
    if (!confirm('确认删除这条记录？')) return;
    try {
        await API.del('/api/records/' + id);  // → handlers.cpp → db.deleteRecord()
        await loadRecords();
    } catch (e) {
        alert('删除失败: ' + e.message);
    }
}

// ── 编辑记录 ────────────────────────────────────────────────────────

// 根据编辑弹窗当前选中的类型，填充其一级分类下拉框
function populateEditCategorySelects() {
    const eType = document.getElementById('eType').value;
    const eCatL1 = document.getElementById('eCatL1');
    const cats = getCategoriesByType(eType);

    eCatL1.innerHTML = '<option value="">请选择</option>';
    for (const cat of cats) {
        eCatL1.innerHTML += `<option value="${escapeHtml(cat.name)}">${cat.icon || ''} ${cat.name}</option>`;
    }
    updateEditL2Select();
}

// 根据编辑弹窗当前选中的一级分类，填充/隐藏二级分类下拉框
function updateEditL2Select() {
    const eCatL1 = document.getElementById('eCatL1');
    const eCatL2Group = document.getElementById('eCatL2Group');
    const eCatL2 = document.getElementById('eCatL2');
    const eType = document.getElementById('eType').value;

    if (eType === 'income') {
        eCatL2Group.style.display = 'none';
        eCatL2.innerHTML = '<option value="">—</option>';
        return;
    }

    eCatL2Group.style.display = '';
    const subs = getSubcategories(eCatL1.value);
    eCatL2.innerHTML = '<option value="">请选择</option>';
    for (const sub of subs) {
        eCatL2.innerHTML += `<option value="${escapeHtml(sub)}">${sub}</option>`;
    }
}

// 由表格中编辑按钮的 onclick="openEditModal(id)" 调用
// 从当前页缓存中找到记录，回填表单后弹出弹窗
function openEditModal(id) {
    const record = currentRecords.find(r => r.id === id);
    if (!record) {
        alert('未找到该记录，请刷新后重试');
        return;
    }

    document.getElementById('eId').value = record.id;
    document.getElementById('eType').value = record.type;
    document.getElementById('eAmount').value = record.amount;

    // 后端格式 "YYYY-MM-DD HH:MM(:SS)" → datetime-local 需要 "YYYY-MM-DDTHH:MM"
    document.getElementById('eDatetime').value =
        (record.datetime || '').replace(' ', 'T').slice(0, 16);

    document.getElementById('eNote').value = record.note || '';

    // 先按类型填充一级分类，再回填选中的分类值
    populateEditCategorySelects();
    document.getElementById('eCatL1').value = record.category_l1 || '';
    updateEditL2Select();
    document.getElementById('eCatL2').value = record.category_l2 || '';

    document.getElementById('editModal').style.display = 'flex';
}

// 关闭编辑弹窗
function closeEditModal() {
    document.getElementById('editModal').style.display = 'none';
}

// 编辑表单提交回调：校验后通过 PUT 更新记录
async function submitEdit(e) {
    e.preventDefault();

    const id = parseInt(document.getElementById('eId').value, 10);
    const type = document.getElementById('eType').value;
    const amount = parseFloat(document.getElementById('eAmount').value);
    const datetime = document.getElementById('eDatetime').value;
    const catL1 = document.getElementById('eCatL1').value;
    const catL2 = document.getElementById('eCatL2').value;
    const note = document.getElementById('eNote').value;

    if (!catL1) {
        alert('请选择一级分类');
        return;
    }
    if (type === 'expense' && !catL2) {
        alert('请选择二级分类');
        return;
    }
    if (!datetime) {
        alert('请选择时间');
        return;
    }

    const formatted = datetime.replace('T', ' ') + ':00';

    try {
        await API.put('/api/records/' + id, {  // → handlers.cpp PUT → db.updateRecord()
            datetime: formatted,
            type: type,
            amount: amount,
            category_l1: catL1,
            category_l2: type === 'income' ? '' : catL2,
            note: note
        });
        closeEditModal();
        await loadRecords();
    } catch (e) {
        alert('保存失败: ' + e.message);
    }
}

// ── 工具函数 ────────────────────────────────────────────────────────

// HTML 转义：防止用户输入中的 < > & 等字符被浏览器当作 HTML 标签解析（XSS 防护）
function escapeHtml(str) {
    const div = document.createElement('div');
    div.textContent = str;       // 以纯文本方式赋值
    return div.innerHTML;        // 读取时自动转义为 &lt; &gt; 等
}

// ── 筛选辅助 ────────────────────────────────────────────────────────

// 将 Date 对象格式化为 <input type="date"> 需要的 "YYYY-MM-DD"
function toDateInputValue(d) {
    const local = new Date(d.getTime() - d.getTimezoneOffset() * 60000);
    return local.toISOString().slice(0, 10);
}

// 快捷日期：根据预设区间设置开始/结束日期输入框，并立即查询
function setQuickDateRange(range) {
    const fromEl = document.getElementById('filterDateFrom');
    const toEl = document.getElementById('filterDateTo');
    const now = new Date();
    const y = now.getFullYear();
    const m = now.getMonth();  // 0 ~ 11

    if (range === 'all') {
        fromEl.value = '';
        toEl.value = '';
    } else if (range === 'thisMonth') {
        fromEl.value = toDateInputValue(new Date(y, m, 1));
        toEl.value = toDateInputValue(new Date(y, m + 1, 0));  // 下月第 0 天 = 本月最后一天
    } else if (range === 'lastMonth') {
        fromEl.value = toDateInputValue(new Date(y, m - 1, 1));
        toEl.value = toDateInputValue(new Date(y, m, 0));
    } else if (range === 'thisYear') {
        fromEl.value = toDateInputValue(new Date(y, 0, 1));
        toEl.value = toDateInputValue(new Date(y, 11, 31));
    }

    currentPage = 1;
    loadRecords();
}

// 重置全部筛选条件，回到默认视图
function resetFilters() {
    document.getElementById('filterType').value = '';
    document.getElementById('filterCatL1').value = '';
    updateFilterL2Select();
    document.getElementById('filterCatL2').value = '';
    document.getElementById('filterDateFrom').value = '';
    document.getElementById('filterDateTo').value = '';
    document.getElementById('filterAmountMin').value = '';
    document.getElementById('filterAmountMax').value = '';
    document.getElementById('filterKeyword').value = '';
    document.getElementById('filterSortBy').value = 'datetime';
    document.getElementById('filterSortOrder').value = 'desc';
    currentPage = 1;
    loadRecords();
}

// ── 初始化 ───────────────────────────────────────────────────────────

// 页面加载完成后执行一次：拉数据 → 渲染 → 绑定事件
async function init() {
    // 将「时间」输入框默认值设为当前本地时间
    const now = new Date();
    const local = new Date(now.getTime() - now.getTimezoneOffset() * 60000)
        .toISOString().slice(0, 16);  // 格式 "YYYY-MM-DDTHH:MM"
    document.getElementById('fDatetime').value = local;

    await loadCategories();          // 1. 拉分类 → 写入 categories 缓存
    populateCategorySelects();       // 2. 填充新增表单下拉框
    populateFilterCategorySelect();  // 3. 填充筛选栏一级分类下拉框
    await loadRecords();             // 4. 拉记录 → 渲染表格和汇总

    // ── 事件绑定（用户操作 → 回调函数） ─────────────────────────────
    // addEventListener(事件名, 回调函数)：注册事件监听，类似 C++ 信号/槽

    // 表单提交 → 新增记录
    document.getElementById('addForm').addEventListener('submit', addRecord);

    // 切换「支出/收入」→ 重新填充一级分类（选项不同）
    document.getElementById('fType').addEventListener('change', () => {
        populateCategorySelects();
    });

    // 切换一级分类 → 更新二级分类选项
    document.getElementById('fCatL1').addEventListener('change', updateL2Select);

    // 下拉类筛选：变化即重置到第 1 页并重新加载
    ['filterType', 'filterCatL2', 'filterSortBy', 'filterSortOrder'].forEach(id => {
        document.getElementById(id).addEventListener('change', () => {
            currentPage = 1;
            loadRecords();
        });
    });

    // 一级分类变化 → 联动二级分类下拉框，再重新加载
    document.getElementById('filterCatL1').addEventListener('change', () => {
        updateFilterL2Select();
        currentPage = 1;
        loadRecords();
    });

    // 文本/数字/日期类输入：回车即查询
    ['filterDateFrom', 'filterDateTo', 'filterAmountMin', 'filterAmountMax', 'filterKeyword'].forEach(id => {
        document.getElementById(id).addEventListener('keydown', (ev) => {
            if (ev.key === 'Enter') {
                currentPage = 1;
                loadRecords();
            }
        });
    });

    // 查询按钮 → 应用全部筛选条件
    document.getElementById('btnSearch').addEventListener('click', () => {
        currentPage = 1;
        loadRecords();
    });

    // 重置按钮 → 清空全部筛选条件
    document.getElementById('btnReset').addEventListener('click', resetFilters);

    // 快捷日期按钮（本月/上月/今年/全部）
    document.querySelectorAll('.quick-dates [data-range]').forEach(btn => {
        btn.addEventListener('click', () => setQuickDateRange(btn.dataset.range));
    });

    // ── 编辑弹窗事件绑定 ──────────────────────────────────────────
    // 编辑表单提交 → 保存修改
    document.getElementById('editForm').addEventListener('submit', submitEdit);

    // 弹窗内切换「支出/收入」→ 重新填充一级分类
    document.getElementById('eType').addEventListener('change', populateEditCategorySelects);

    // 弹窗内切换一级分类 → 更新二级分类选项
    document.getElementById('eCatL1').addEventListener('change', updateEditL2Select);

    // 关闭按钮 / 取消按钮 → 关闭弹窗
    document.getElementById('editClose').addEventListener('click', closeEditModal);
    document.getElementById('editCancel').addEventListener('click', closeEditModal);

    // 点击弹窗遮罩层（非内容区域）→ 关闭弹窗
    document.getElementById('editModal').addEventListener('click', (ev) => {
        if (ev.target.id === 'editModal') closeEditModal();
    });
}

// 脚本加载后立即执行 init()（index.html 中 <script> 位于 </body> 前，DOM 已就绪）
init();
