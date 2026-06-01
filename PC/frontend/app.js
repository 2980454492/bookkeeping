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

// 统计页：选中的日期、当前查看的年/月（0-11）
let statsSelectedDay = '';
let statsViewYear = 0;
let statsViewMonth = 0;

// 统计页缓存：当前查看月 / 当年记录
let statsMonthRecords = [];
let statsYearRecords = [];

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
            const item = {
                ...cat,
                subs: cat.subs || []
            };
            if (cat.type === 'expense') categories.expense.push(item);
            else categories.income.push(item);
        }
        console.log('[App] 分类已加载:', categories.expense.length + categories.income.length);
    } catch (e) {
        console.error('[App] 加载分类失败:', e);
    }
}

// 读取表单中当前选中的类型（单选按钮）
function getFormType(prefix) {
    const el = document.querySelector(`input[name="${prefix}Type"]:checked`);
    return el ? el.value : 'expense';
}

// 设置表单类型单选按钮
function setFormType(prefix, type) {
    const el = document.querySelector(`input[name="${prefix}Type"][value="${type}"]`);
    if (el) el.checked = true;
}

// 渲染一级分类按钮网格；prefix 为 'f'（新增）或 'e'（编辑）
function renderCatL1Picker(prefix, selectedL1 = '', selectedL2 = '') {
    const type = getFormType(prefix);
    const picker = document.getElementById(`${prefix}CatL1Picker`);
    const hidden = document.getElementById(`${prefix}CatL1`);
    const cats = getCategoriesByType(type);

    hidden.value = selectedL1 || '';
    const noneActive = !selectedL1 ? ' active' : '';
    const noneBtn = `<button type="button" class="cat-chip cat-none${noneActive}" data-l1-clear>不选</button>`;
    const addBtn = '<button type="button" class="cat-chip cat-add-chip" data-cat-add="l1">➕ 添加</button>';
    picker.innerHTML = noneBtn + cats.map(cat => {
        const active = cat.name === selectedL1 ? ' active' : '';
        return `<button type="button" class="cat-chip${active}" data-l1="${escapeHtml(cat.name)}">${cat.icon || ''} ${escapeHtml(cat.name)}</button>`;
    }).join('') + addBtn;

    updateCatL2Panel(prefix, selectedL1, selectedL2);
}

// 支出且已选一级分类时，在下方展示二级分类按钮；收入则隐藏
function updateCatL2Panel(prefix, l1Name, selectedL2 = '') {
    const type = getFormType(prefix);
    const groupId = prefix === 'f' ? 'catL2Group' : 'eCatL2Group';
    const group = document.getElementById(groupId);
    const picker = document.getElementById(`${prefix}CatL2Picker`);
    const hidden = document.getElementById(`${prefix}CatL2`);

    if (type !== 'expense' || !l1Name) {
        group.style.display = 'none';
        hidden.value = '';
        picker.innerHTML = '';
        return;
    }

    group.style.display = '';
    const subs = getSubcategories(l1Name);
    hidden.value = selectedL2 || '';
    const noneActive = !selectedL2 ? ' active' : '';
    const noneBtn = `<button type="button" class="cat-chip cat-none${noneActive}" data-l2-clear>不选</button>`;
    const addBtn = '<button type="button" class="cat-chip cat-add-chip" data-cat-add="l2">➕ 添加</button>';
    picker.innerHTML = noneBtn + subs.map(sub => {
        const active = sub === selectedL2 ? ' active' : '';
        return `<button type="button" class="cat-chip${active}" data-l2="${escapeHtml(sub)}">${escapeHtml(sub)}</button>`;
    }).join('') + addBtn;
}

// 切换类型时清空已选分类并重新渲染一级分类
function onFormTypeChange(prefix) {
    renderCatL1Picker(prefix, '', '');
}

// 绑定分类选择器点击事件（一级 / 二级 / 类型切换）
function setupCategoryPicker(prefix) {
    document.getElementById(`${prefix}CatL1Picker`).addEventListener('click', (ev) => {
        const btn = ev.target.closest('.cat-chip');
        if (!btn) return;
        if (btn.dataset.catAdd === 'l1') {
            formAddCategoryL1(prefix);
            return;
        }
        if (btn.hasAttribute('data-l1-clear')) {
            document.getElementById(`${prefix}CatL1`).value = '';
            document.querySelectorAll(`#${prefix}CatL1Picker .cat-chip`).forEach(el => {
                el.classList.toggle('active', el.hasAttribute('data-l1-clear'));
            });
            updateCatL2Panel(prefix, '', '');
            return;
        }
        if (!btn.dataset.l1) return;
        const l1 = btn.dataset.l1;
        document.getElementById(`${prefix}CatL1`).value = l1;
        document.querySelectorAll(`#${prefix}CatL1Picker .cat-chip`).forEach(el => {
            el.classList.toggle('active', el.dataset.l1 === l1);
        });
        updateCatL2Panel(prefix, l1, '');
    });

    document.getElementById(`${prefix}CatL2Picker`).addEventListener('click', (ev) => {
        const btn = ev.target.closest('.cat-chip');
        if (!btn) return;
        if (btn.dataset.catAdd === 'l2') {
            formAddCategoryL2(prefix);
            return;
        }
        if (btn.hasAttribute('data-l2-clear')) {
            document.getElementById(`${prefix}CatL2`).value = '';
            document.querySelectorAll(`#${prefix}CatL2Picker .cat-chip`).forEach(el => {
                el.classList.toggle('active', el.hasAttribute('data-l2-clear'));
            });
            return;
        }
        if (!btn.dataset.l2) return;
        const l2 = btn.dataset.l2;
        document.getElementById(`${prefix}CatL2`).value = l2;
        document.querySelectorAll(`#${prefix}CatL2Picker .cat-chip`).forEach(el => {
            el.classList.toggle('active', el.dataset.l2 === l2);
        });
    });

    document.querySelectorAll(`input[name="${prefix}Type"]`).forEach(radio => {
        radio.addEventListener('change', () => onFormTypeChange(prefix));
    });
}

// ── 新增分类（记账页 / 设置页共用）────────────────────────────────

async function addCategoryL1ForType(type, { selectName } = {}) {
    const label = type === 'expense' ? '支出一级分类名称' : '收入分类名称';
    const name = prompt(label);
    if (!name || !name.trim()) return null;
    const icon = prompt('图标（emoji，可留空）', '📦') || '📦';
    const trimmed = name.trim();
    try {
        await API.post('/api/categories/l1', {
            name: trimmed,
            type,
            icon: icon.trim() || '📦',
            sort_order: getCategoriesByType(type).length
        });
        await refreshCategoriesAll();
        if (selectName) selectName(trimmed);
        return trimmed;
    } catch (e) {
        alert('添加失败: ' + e.message);
        return null;
    }
}

async function addCategoryL2ForL1Name(l1Name, { selectName } = {}) {
    const parent = [...categories.expense, ...categories.income].find(c => c.name === l1Name);
    if (!parent || parent.type !== 'expense') {
        alert('仅支出分类支持二级分类');
        return null;
    }
    const name = prompt(`「${l1Name}」下的二级分类名称`);
    if (!name || !name.trim()) return null;
    const trimmed = name.trim();
    const sortOrder = parent.subs ? parent.subs.length : 0;
    try {
        await API.post('/api/categories/l2', {
            l1_id: parent.id,
            name: trimmed,
            sort_order: sortOrder
        });
        await refreshCategoriesAll();
        if (selectName) selectName(trimmed);
        return trimmed;
    } catch (e) {
        alert('添加失败: ' + e.message);
        return null;
    }
}

function refreshOpenFormCategoryPickers() {
    for (const prefix of ['f', 'e']) {
        const modalId = prefix === 'f' ? 'addModal' : 'editModal';
        if (document.getElementById(modalId).style.display === 'none') continue;
        const l1 = document.getElementById(`${prefix}CatL1`).value;
        const l2 = document.getElementById(`${prefix}CatL2`).value;
        renderCatL1Picker(prefix, l1, l2);
    }
}

async function formAddCategoryL1(prefix) {
    const type = getFormType(prefix);
    await addCategoryL1ForType(type, {
        selectName: (name) => renderCatL1Picker(prefix, name, '')
    });
}

async function formAddCategoryL2(prefix) {
    const l1 = document.getElementById(`${prefix}CatL1`).value;
    if (!l1) {
        alert('请先选择一级分类');
        return;
    }
    await addCategoryL2ForL1Name(l1, {
        selectName: (name) => updateCatL2Panel(prefix, l1, name)
    });
}

// 按收支类型取一级分类列表（type 为空时返回全部）
function getCategoriesForFilterType(type) {
    if (type === 'expense') return categories.expense;
    if (type === 'income') return categories.income;
    return [...categories.expense, ...categories.income];
}

function fillCategoryL1Select(selectEl, type, keepSelected) {
    const cats = getCategoriesForFilterType(type);
    const prev = keepSelected ? selectEl.value : '';
    selectEl.innerHTML = '<option value="">全部分类</option>';
    for (const cat of cats) {
        selectEl.innerHTML += `<option value="${escapeHtml(cat.name)}">${cat.name}</option>`;
    }
    if (prev && cats.some(c => c.name === prev)) {
        selectEl.value = prev;
    } else {
        selectEl.value = '';
    }
}

// 填充记账页筛选栏一级分类（随「类型」联动）
function populateFilterCategorySelect() {
    const type = document.getElementById('filterType').value;
    fillCategoryL1Select(document.getElementById('filterCatL1'), type, true);
    updateFilterL2Select();
}

function updateFilterCatL2Visibility() {
    const wrap = document.getElementById('filterCatL2Wrap');
    const l2 = document.getElementById('filterCatL2');
    const type = document.getElementById('filterType').value;
    const show = type !== 'income';
    if (wrap) wrap.style.display = show ? '' : 'none';
    if (!show) {
        l2.value = '';
        l2.innerHTML = '<option value="">全部二级</option>';
    }
}

// 根据筛选栏当前选中的一级分类，填充其二级分类下拉框
function updateFilterL2Select() {
    updateFilterCatL2Visibility();
    const type = document.getElementById('filterType').value;
    const filterCatL2 = document.getElementById('filterCatL2');
    if (type === 'income') return;

    const filterCatL1 = document.getElementById('filterCatL1').value;
    const prevL2 = filterCatL2.value;
    const subs = getSubcategories(filterCatL1);

    filterCatL2.innerHTML = '<option value="">全部二级</option>';
    for (const sub of subs) {
        filterCatL2.innerHTML += `<option value="${escapeHtml(sub)}">${sub}</option>`;
    }
    if (prevL2 && subs.includes(prevL2)) {
        filterCatL2.value = prevL2;
    }
}

// ── 筛选条件（记账页 / 导出弹窗共用逻辑）────────────────────────────

const RECORD_FILTER_IDS = {
    type: 'filterType',
    catL1: 'filterCatL1',
    catL2: 'filterCatL2',
    dateFrom: 'filterDateFrom',
    dateTo: 'filterDateTo',
    amountMin: 'filterAmountMin',
    amountMax: 'filterAmountMax',
    keyword: 'filterKeyword',
    sortBy: 'filterSortBy',
    sortOrder: 'filterSortOrder'
};

const EXPORT_FILTER_IDS = {
    type: 'expFilterType',
    catL1: 'expFilterCatL1',
    catL2: 'expFilterCatL2',
    dateFrom: 'expFilterDateFrom',
    dateTo: 'expFilterDateTo',
    amountMin: 'expFilterAmountMin',
    amountMax: 'expFilterAmountMax',
    keyword: 'expFilterKeyword',
    sortBy: 'expFilterSortBy',
    sortOrder: 'expFilterSortOrder'
};

function readFilterFields(ids) {
    return {
        type: document.getElementById(ids.type).value,
        catL1: document.getElementById(ids.catL1).value,
        catL2: document.getElementById(ids.catL2).value,
        dateFrom: document.getElementById(ids.dateFrom).value,
        dateTo: document.getElementById(ids.dateTo).value,
        amountMin: document.getElementById(ids.amountMin).value,
        amountMax: document.getElementById(ids.amountMax).value,
        keyword: document.getElementById(ids.keyword).value.trim(),
        sortBy: document.getElementById(ids.sortBy).value,
        sortOrder: document.getElementById(ids.sortOrder).value
    };
}

function applyFilterFields(ids, f) {
    document.getElementById(ids.type).value = f.type || '';
    if (ids === RECORD_FILTER_IDS) {
        populateFilterCategorySelect();
        if (f.catL1) document.getElementById(ids.catL1).value = f.catL1;
        updateFilterL2Select();
        if (f.catL2) document.getElementById(ids.catL2).value = f.catL2;
    } else {
        populateExportFilterCategorySelect();
        if (f.catL1) document.getElementById(ids.catL1).value = f.catL1;
        updateExportFilterL2Select();
        if (f.catL2) document.getElementById(ids.catL2).value = f.catL2;
    }
    document.getElementById(ids.dateFrom).value = f.dateFrom || '';
    document.getElementById(ids.dateTo).value = f.dateTo || '';
    document.getElementById(ids.amountMin).value = f.amountMin || '';
    document.getElementById(ids.amountMax).value = f.amountMax || '';
    document.getElementById(ids.keyword).value = f.keyword || '';
    document.getElementById(ids.sortBy).value = f.sortBy || 'datetime';
    document.getElementById(ids.sortOrder).value = f.sortOrder || 'desc';
}

function buildFilterQueryObject(f) {
    const q = {
        sort_by: f.sortBy || 'datetime',
        sort_order: f.sortOrder || 'desc'
    };
    if (f.type) q.type = f.type;
    if (f.catL1) q.cat_l1 = f.catL1;
    if (f.catL2) q.cat_l2 = f.catL2;
    if (f.dateFrom) q.date_from = f.dateFrom;
    if (f.dateTo) q.date_to = f.dateTo;
    if (f.amountMin !== '') q.amount_min = parseFloat(f.amountMin);
    if (f.amountMax !== '') q.amount_max = parseFloat(f.amountMax);
    if (f.keyword) q.keyword = f.keyword;
    return q;
}

function buildFilterSearchParams(f, page, pageSize) {
    const params = new URLSearchParams();
    const q = buildFilterQueryObject(f);
    for (const [k, v] of Object.entries(q)) {
        params.set(k, String(v));
    }
    params.set('page', page);
    params.set('page_size', pageSize);
    return params;
}

// ── 加载记录 ────────────────────────────────────────────────────────

// 从后端拉取记录列表，更新缓存并刷新表格和汇总
async function loadRecords() {
    const f = readFilterFields(RECORD_FILTER_IDS);
    const params = buildFilterSearchParams(f, currentPage, PAGE_SIZE);

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
        tbody.innerHTML = '<tr class="empty-row"><td colspan="6">暂无记录，添加一条吧 🎉</td></tr>';
        return;
    }
    tbody.innerHTML = currentRecords.map(r => {
        const typeLabel = r.type === 'income' ? '收入' : '支出';
        const typeClass = r.type === 'income' ? 'type-income' : 'type-expense';
        const amountClass = r.type === 'income' ? 'amount-income' : 'amount-expense';
        const sign = r.type === 'income' ? '+' : '-';
        const hasNote = !!(r.note && r.note.trim());
        const mainCls = hasNote ? 'record-main has-note' : 'record-main';
        const noteRow = hasNote
            ? `<tr class="record-note-row"><td colspan="6" class="record-note">${escapeHtml(r.note)}</td></tr>`
            : '';
        return `
            <tr class="${mainCls}">
                <td>${escapeHtml(r.datetime)}</td>
                <td><span class="type-tag ${typeClass}">${typeLabel}</span></td>
                <td class="${amountClass}">${sign}¥${r.amount.toFixed(2)}</td>
                <td>${escapeHtml(r.category_l1 || '—')}</td>
                <td>${escapeHtml(r.category_l2 || '—')}</td>
                <td>
                    <button class="btn btn-edit" onclick="openEditModal(${r.id})">编辑</button>
                    <button class="btn btn-danger" onclick="deleteRecord(${r.id})">删除</button>
                </td>
            </tr>${noteRow}
        `;
    }).join('');
}

// ── 汇总统计 ────────────────────────────────────────────────────────

function formatMoney(amount) {
    return `¥${amount.toFixed(2)}`;
}

function sumRecordsInRange(records, dateFrom, dateTo, type) {
    let sum = 0;
    for (const r of records) {
        const d = recordDateKey(r.datetime);
        if (!d || d < dateFrom || d > dateTo) continue;
        if (r.type === type) sum += r.amount;
    }
    return sum;
}

// 更新页面顶部汇总卡片（该月/上月/今年/去年收支）
async function updateSummary() {
    try {
        const now = new Date();
        const y = now.getFullYear();
        const m = now.getMonth();

        const thisMonthFrom = toDateInputValue(new Date(y, m, 1));
        const thisMonthTo = toDateInputValue(new Date(y, m + 1, 0));
        const lastMonthFrom = toDateInputValue(new Date(y, m - 1, 1));
        const lastMonthTo = toDateInputValue(new Date(y, m, 0));
        const thisYearFrom = toDateInputValue(new Date(y, 0, 1));
        const thisYearTo = toDateInputValue(new Date(y, 11, 31));
        const lastYearFrom = toDateInputValue(new Date(y - 1, 0, 1));
        const lastYearTo = toDateInputValue(new Date(y - 1, 11, 31));

        const params = new URLSearchParams({
            date_from: lastYearFrom,
            date_to: thisYearTo,
            page: '1',
            page_size: '10000',
            sort_by: 'datetime',
            sort_order: 'asc'
        });
        const data = await API.get('/api/records?' + params.toString());
        const records = data.records || [];

        document.getElementById('sumMonthExpense').textContent =
            formatMoney(sumRecordsInRange(records, thisMonthFrom, thisMonthTo, 'expense'));
        document.getElementById('sumLastMonthExpense').textContent =
            formatMoney(sumRecordsInRange(records, lastMonthFrom, lastMonthTo, 'expense'));
        document.getElementById('sumYearExpense').textContent =
            formatMoney(sumRecordsInRange(records, thisYearFrom, thisYearTo, 'expense'));
        document.getElementById('sumYearIncome').textContent =
            formatMoney(sumRecordsInRange(records, thisYearFrom, thisYearTo, 'income'));
        document.getElementById('sumLastYearExpense').textContent =
            formatMoney(sumRecordsInRange(records, lastYearFrom, lastYearTo, 'expense'));
        document.getElementById('sumLastYearIncome').textContent =
            formatMoney(sumRecordsInRange(records, lastYearFrom, lastYearTo, 'income'));
    } catch (e) {
        console.error('[App] 更新汇总失败:', e);
    }
}

// ── 新增记录 ────────────────────────────────────────────────────────

// 打开新增记录弹窗：重置表单为默认值（类型=支出、时间=当前）后显示
function openAddModal() {
    setFormType('f', 'expense');
    document.getElementById('fAmount').value = '';
    document.getElementById('fNote').value = '';

    // 时间默认填当前本地时间，格式 "YYYY-MM-DDTHH:MM"
    const now = new Date();
    document.getElementById('fDatetime').value =
        new Date(now.getTime() - now.getTimezoneOffset() * 60000).toISOString().slice(0, 16);

    renderCatL1Picker('f', '', '');
    document.getElementById('addModal').style.display = 'flex';
}

// 关闭新增记录弹窗
function closeAddModal() {
    document.getElementById('addModal').style.display = 'none';
}

// 表单提交回调：由 addForm 的 'submit' 事件触发（见 init() 中的 addEventListener）
async function addRecord(e) {
    e.preventDefault();  // 阻止表单默认行为（否则会整页刷新并 POST 到当前 URL）

    // 从各表单控件读取用户输入
    const type = getFormType('f');
    const amount = parseFloat(document.getElementById('fAmount').value);
    const datetime = document.getElementById('fDatetime').value;
    const catL1 = document.getElementById('fCatL1').value;
    const catL2 = document.getElementById('fCatL2').value;
    const note = document.getElementById('fNote').value;

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
            category_l2: catL2,
            note: note
        });
        closeAddModal();      // 提交成功后关闭弹窗
        await loadRecords();  // 重新拉取列表，刷新表格
        refreshStatsIfVisible();
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
        refreshStatsIfVisible();
    } catch (e) {
        alert('删除失败: ' + e.message);
    }
}

// ── 编辑记录 ────────────────────────────────────────────────────────

// 由表格中编辑按钮的 onclick="openEditModal(id)" 调用
// 从当前页缓存中找到记录，回填表单后弹出弹窗
function openEditModal(id) {
    const record = currentRecords.find(r => r.id === id);
    if (!record) {
        alert('未找到该记录，请刷新后重试');
        return;
    }

    document.getElementById('eId').value = record.id;
    setFormType('e', record.type);
    document.getElementById('eAmount').value = record.amount;

    // 后端格式 "YYYY-MM-DD HH:MM(:SS)" → datetime-local 需要 "YYYY-MM-DDTHH:MM"
    document.getElementById('eDatetime').value =
        (record.datetime || '').replace(' ', 'T').slice(0, 16);

    document.getElementById('eNote').value = record.note || '';

    renderCatL1Picker('e', record.category_l1 || '', record.category_l2 || '');

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
    const type = getFormType('e');
    const amount = parseFloat(document.getElementById('eAmount').value);
    const datetime = document.getElementById('eDatetime').value;
    const catL1 = document.getElementById('eCatL1').value;
    const catL2 = document.getElementById('eCatL2').value;
    const note = document.getElementById('eNote').value;

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
            category_l2: catL2,
            note: note
        });
        closeEditModal();
        await loadRecords();
        refreshStatsIfVisible();
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

// ── 底部导航与统计页 ────────────────────────────────────────────────

// 分类管理弹窗状态
let settingsCatType = 'expense';
let catManageExpandedL1Id = 0;  // 当前展开的一级分类 id，0 表示均未展开

function switchTab(tab) {
    document.querySelectorAll('.tab-pane').forEach(el => {
        el.classList.toggle('active', el.id === `pane-${tab}`);
    });
    document.querySelectorAll('.nav-item').forEach(el => {
        el.classList.toggle('active', el.dataset.tab === tab);
    });
    if (tab === 'stats') loadStatsPage();
}

// 分类变更后刷新全局缓存；若弹窗打开则刷新当前层级视图
async function refreshCategoriesAll() {
    await loadCategories();
    populateFilterCategorySelect();
    refreshOpenFormCategoryPickers();
    if (document.getElementById('catManageModal').style.display !== 'none') {
        renderCatManageModal();
    }
}

function openCatManageModal() {
    catManageExpandedL1Id = 0;
    renderCatManageModal();
    document.getElementById('catManageModal').style.display = 'flex';
}

function closeCatManageModal() {
    document.getElementById('catManageModal').style.display = 'none';
}

function toggleCatManageL1(id) {
    if (settingsCatType === 'income') return;
    catManageExpandedL1Id = catManageExpandedL1Id === id ? 0 : id;
    renderCatManageModal();
}

function renderCatManageModal() {
    const body = document.getElementById('catManageBody');
    const isExpense = settingsCatType === 'expense';
    document.getElementById('catManageTitle').textContent =
        isExpense ? '支出分类' : '收入分类';

    const cats = getCategoriesByType(settingsCatType);
    const listHtml = cats.length === 0
        ? `<div class="cat-manage-empty">暂无${isExpense ? '支出' : '收入'}分类</div>`
        : `<ul class="cat-manage-groups">${cats.map(cat => {
            if (!isExpense) {
                return `
                <li class="cat-manage-group">
                    <div class="cat-manage-l1-row cat-manage-l1-row-flat">
                        <span class="cat-row-icon">${escapeHtml(cat.icon || '📦')}</span>
                        <span class="cat-row-name">${escapeHtml(cat.name)}</span>
                        <span class="cat-row-btns">
                            <button type="button" class="btn btn-sm" data-act="edit-l1" data-id="${cat.id}">编辑</button>
                            <button type="button" class="btn btn-danger btn-sm" data-act="del-l1" data-id="${cat.id}" data-name="${escapeAttr(cat.name)}">删除</button>
                        </span>
                    </div>
                </li>`;
            }

            const expanded = catManageExpandedL1Id === cat.id;
            const subs = cat.subs || [];
            const subsPanel = expanded
                ? `<div class="cat-manage-l2-panel">
                    ${subs.length === 0
                        ? '<div class="cat-manage-l2-empty">暂无二级分类</div>'
                        : `<ul class="cat-manage-l2-list">${subs.map(s => `
                            <li class="cat-manage-l2-row">
                                <span class="cat-row-name">${escapeHtml(s.name)}</span>
                                <span class="cat-row-btns">
                                    <button type="button" class="btn btn-sm" data-act="edit-l2" data-id="${s.id}" data-l1="${cat.id}" data-name="${escapeAttr(s.name)}">编辑</button>
                                    <button type="button" class="btn btn-danger btn-sm" data-act="del-l2" data-id="${s.id}" data-name="${escapeAttr(s.name)}">删除</button>
                                </span>
                            </li>`).join('')}</ul>`}
                    <button type="button" class="btn btn-sm cat-manage-add-l2" data-act="add-l2" data-l1="${cat.id}" data-l1name="${escapeAttr(cat.name)}">➕ 添加二级分类</button>
                   </div>`
                : '';
            return `
                <li class="cat-manage-group${expanded ? ' expanded' : ''}">
                    <div class="cat-manage-l1-row" data-l1-id="${cat.id}">
                        <span class="cat-row-chevron" aria-hidden="true">${expanded ? '▼' : '›'}</span>
                        <span class="cat-row-icon">${escapeHtml(cat.icon || '📦')}</span>
                        <span class="cat-row-name">${escapeHtml(cat.name)}</span>
                        <span class="cat-row-btns">
                            <button type="button" class="btn btn-sm" data-act="edit-l1" data-id="${cat.id}">编辑</button>
                            <button type="button" class="btn btn-danger btn-sm" data-act="del-l1" data-id="${cat.id}" data-name="${escapeAttr(cat.name)}">删除</button>
                        </span>
                    </div>
                    ${subsPanel}
                </li>`;
        }).join('')}</ul>`;

    body.innerHTML = `
        <div class="settings-type-tabs">
            <button type="button" class="btn btn-sm settings-type-btn${settingsCatType === 'expense' ? ' active' : ''}" data-ctype="expense">支出</button>
            <button type="button" class="btn btn-sm settings-type-btn${settingsCatType === 'income' ? ' active' : ''}" data-ctype="income">收入</button>
        </div>
        ${listHtml}
        <div class="cat-manage-footer">
            <button type="button" class="btn btn-primary btn-sm" data-act="add-l1">➕ 添加${isExpense ? '一级' : ''}分类</button>
            <button type="button" class="btn btn-sm" data-act="reset-cats">↺ 恢复默认</button>
        </div>
    `;
}

async function settingsAddL1() {
    await addCategoryL1ForType(settingsCatType);
}

async function settingsEditL1(id) {
    const cat = [...categories.expense, ...categories.income].find(c => c.id === id);
    if (!cat) return;
    const label = cat.type === 'expense' ? '支出一级分类名称' : '收入分类名称';
    const name = prompt(label, cat.name);
    if (name === null) return;
    if (!name.trim()) { alert('名称不能为空'); return; }
    const icon = prompt('图标（emoji）', cat.icon || '📦');
    if (icon === null) return;
    try {
        await API.put('/api/categories/l1/' + id, {
            name: name.trim(),
            type: cat.type,
            icon: (icon.trim() || '📦'),
            sort_order: cat.sort_order || 0
        });
        await refreshCategoriesAll();
    } catch (e) {
        alert('保存失败: ' + e.message);
    }
}

async function settingsDeleteL1(id, name) {
    const cat = [...categories.expense, ...categories.income].find(c => c.id === id);
    const msg = cat && cat.type === 'income'
        ? `确认删除收入分类「${name}」？`
        : `确认删除一级分类「${name}」及其下所有二级分类？`;
    if (!confirm(msg)) return;
    try {
        await API.del('/api/categories/l1/' + id);
        if (catManageExpandedL1Id === id) catManageExpandedL1Id = 0;
        await refreshCategoriesAll();
    } catch (e) {
        alert('删除失败: ' + e.message);
    }
}

async function settingsAddL2(l1Id, l1Name) {
    await addCategoryL2ForL1Name(l1Name);
}

async function settingsEditL2(id, l1Id, oldName) {
    const name = prompt('二级分类名称', oldName);
    if (name === null) return;
    if (!name.trim()) { alert('名称不能为空'); return; }
    const cat = [...categories.expense, ...categories.income].find(c => c.id === l1Id);
    const sub = cat && cat.subs ? cat.subs.find(s => s.id === id) : null;
    try {
        await API.put('/api/categories/l2/' + id, {
            l1_id: l1Id,
            name: name.trim(),
            sort_order: sub ? (sub.sort_order || 0) : 0
        });
        await refreshCategoriesAll();
    } catch (e) {
        alert('保存失败: ' + e.message);
    }
}

async function settingsDeleteL2(id, name) {
    if (!confirm(`确认删除二级分类「${name}」？`)) return;
    try {
        await API.del('/api/categories/l2/' + id);
        await refreshCategoriesAll();
    } catch (e) {
        alert('删除失败: ' + e.message);
    }
}

async function settingsResetCategories() {
    if (!confirm('将用 categories.json 默认模板覆盖当前分类，是否继续？')) return;
    try {
        await API.post('/api/categories/reset', {});
        await refreshCategoriesAll();
        alert('已恢复默认分类');
    } catch (e) {
        alert('恢复失败: ' + e.message);
    }
}

function escapeAttr(str) {
    return String(str)
        .replace(/&/g, '&amp;')
        .replace(/"/g, '&quot;')
        .replace(/</g, '&lt;');
}

// ── 导出数据 ────────────────────────────────────────────────────────

function populateExportFilterCategorySelect() {
    const type = document.getElementById('expFilterType').value;
    fillCategoryL1Select(document.getElementById('expFilterCatL1'), type, true);
    updateExportFilterL2Select();
}

function updateExportFilterCatL2Visibility() {
    const wrap = document.getElementById('expFilterCatL2Wrap');
    const l2 = document.getElementById('expFilterCatL2');
    const type = document.getElementById('expFilterType').value;
    const show = type !== 'income';
    if (wrap) wrap.style.display = show ? '' : 'none';
    if (!show) {
        l2.value = '';
        l2.innerHTML = '<option value="">全部二级</option>';
    }
}

function updateExportFilterL2Select() {
    updateExportFilterCatL2Visibility();
    const type = document.getElementById('expFilterType').value;
    const sel = document.getElementById('expFilterCatL2');
    if (type === 'income') return;

    const l1 = document.getElementById('expFilterCatL1').value;
    const prevL2 = sel.value;
    const subs = getSubcategories(l1);
    sel.innerHTML = '<option value="">全部二级</option>';
    for (const sub of subs) {
        sel.innerHTML += `<option value="${escapeHtml(sub)}">${sub}</option>`;
    }
    if (prevL2 && subs.includes(prevL2)) {
        sel.value = prevL2;
    }
}

function syncExportFiltersFromRecordsPage() {
    applyFilterFields(EXPORT_FILTER_IDS, readFilterFields(RECORD_FILTER_IDS));
}

/** 导出弹窗默认筛选：全部数据 */
function applyDefaultExportFilters() {
    applyFilterFields(EXPORT_FILTER_IDS, {
        type: '',
        catL1: '',
        catL2: '',
        dateFrom: '',
        dateTo: '',
        amountMin: '',
        amountMax: '',
        keyword: '',
        sortBy: 'datetime',
        sortOrder: 'desc'
    });
}

function openExportModal() {
    populateExportFilterCategorySelect();
    applyDefaultExportFilters();
    const now = new Date();
    const defaultName = `记账导出_${now.getFullYear()}${String(now.getMonth() + 1).padStart(2, '0')}${String(now.getDate()).padStart(2, '0')}`;
    document.getElementById('exportFilename').value = defaultName;
    document.getElementById('exportModal').style.display = 'flex';
}

function closeExportModal() {
    document.getElementById('exportModal').style.display = 'none';
}

/** 解析 API 错误响应体（fetch 失败时 message 常为 JSON 字符串） */
function parseApiErrorPayload(raw) {
    const fallback = { error: raw || '未知错误' };
    if (!raw || typeof raw !== 'string') return fallback;
    try {
        const err = JSON.parse(raw);
        if (err && typeof err === 'object') return err;
    } catch (_) { /* 非 JSON */ }
    return fallback;
}

function clearImportError() {
    const box = document.getElementById('importError');
    if (!box) return;
    box.style.display = 'none';
    ['importErrorTitle', 'importErrorDetail', 'importErrorHint', 'importErrorMeta']
        .forEach((id) => {
            const el = document.getElementById(id);
            if (el) el.textContent = '';
        });
}

function showImportError(err, source = 'server') {
    const box = document.getElementById('importError');
    if (!box) return;
    const title = err.error || '导入失败';
    const detail = err.detail || '';
    const hint = err.hint || '';
    const parts = [];
    if (err.error_code) parts.push('代码: ' + err.error_code);
    if (err.row) parts.push('行号: ' + err.row);
    if (source === 'client') parts.push('来源: 本地校验');
    else if (source === 'server') parts.push('来源: 服务端');

    document.getElementById('importErrorTitle').textContent = title;
    document.getElementById('importErrorDetail').textContent = detail;
    document.getElementById('importErrorHint').textContent = hint;
    document.getElementById('importErrorMeta').textContent = parts.join(' · ');
    box.style.display = 'block';

    console.error('[Import] 失败', { source, ...err });
}

const IMPORT_EXT_BY_FORMAT = {
    csv: ['.csv'],
    tsv: ['.tsv', '.txt'],
    txt: ['.txt'],
    json: ['.json'],
    xlsx: ['.xlsx']
};

function validateImportFileClient(format, file) {
    if (!file) {
        return {
            error: '未选择文件',
            error_code: 'client_no_file',
            hint: '请点击「文件」选择要导入的数据文件'
        };
    }
    if (file.size === 0) {
        return {
            error: '文件大小为 0',
            error_code: 'client_empty_file',
            hint: '请选择非空文件'
        };
    }
    const name = (file.name || '').toLowerCase();
    const ext = name.includes('.') ? name.slice(name.lastIndexOf('.')) : '';
    const allowed = IMPORT_EXT_BY_FORMAT[format] || [];
    if (ext && allowed.length && !allowed.includes(ext)) {
        return {
            error: '文件扩展名与所选格式不一致',
            error_code: 'client_format_mismatch',
            detail: `已选格式：${format}，文件：${file.name}`,
            hint: `请将导入格式改为与文件匹配，或选择扩展名为 ${allowed.join(' / ')} 的文件`
        };
    }
    if (format === 'xlsx' && file.size < 100) {
        return {
            error: 'Excel 文件过小，可能已损坏',
            error_code: 'client_xlsx_too_small',
            detail: `文件大小 ${file.size} 字节`,
            hint: '请确认文件完整，或使用本应用导出的 .xlsx'
        };
    }
    return null;
}

function openImportModal() {
    document.getElementById('importFormat').value = 'csv';
    document.getElementById('importFile').value = '';
    clearImportError();
    document.getElementById('importModal').style.display = 'flex';
}

function closeImportModal() {
    document.getElementById('importModal').style.display = 'none';
    clearImportError();
}

let exportConflictResolver = null;

function openExportConflictModal() {
    document.getElementById('exportConflictModal').style.display = 'flex';
}

function closeExportConflictModal() {
    document.getElementById('exportConflictModal').style.display = 'none';
}

function resolveExportConflict(choice) {
    if (exportConflictResolver) {
        exportConflictResolver(choice);
        exportConflictResolver = null;
    }
    closeExportConflictModal();
}

function chooseExportConflictAction() {
    return new Promise((resolve) => {
        exportConflictResolver = resolve;
        openExportConflictModal();
    });
}

function readFileAsText(file) {
    return new Promise((resolve, reject) => {
        const reader = new FileReader();
        reader.onload = () => resolve(typeof reader.result === 'string' ? reader.result : '');
        reader.onerror = () => reject(new Error('读取文件失败'));
        reader.readAsText(file);
    });
}

function readFileAsBase64(file) {
    return new Promise((resolve, reject) => {
        const reader = new FileReader();
        reader.onload = () => {
            const result = typeof reader.result === 'string' ? reader.result : '';
            const comma = result.indexOf(',');
            resolve(comma >= 0 ? result.slice(comma + 1) : '');
        };
        reader.onerror = () => reject(new Error('读取文件失败'));
        reader.readAsDataURL(file);
    });
}

async function submitImport() {
    const format = document.getElementById('importFormat').value;
    const fileInput = document.getElementById('importFile');
    const file = fileInput.files && fileInput.files[0];
    if (!file) {
        alert('请选择要导入的文件');
        return;
    }

    try {
        const contentIsBase64 = format === 'xlsx';
        const content = contentIsBase64
            ? await readFileAsBase64(file)
            : await readFileAsText(file);
        const data = await API.post('/api/records/import', {
            format,
            content,
            content_is_base64: contentIsBase64
        });
        alert(`导入成功，共导入 ${data.count} 条记录`);
        closeImportModal();
        await loadRecords();
    } catch (e) {
        let msg = e.message || String(e);
        try {
            const err = JSON.parse(msg);
            if (err.error) msg = err.error;
        } catch (_) { /* 非 JSON */ }
        alert('导入失败：' + msg);
    }
}

async function submitExport() {
    const filename = document.getElementById('exportFilename').value.trim();
    if (!filename) {
        alert('请输入文件名');
        return;
    }
    const format = document.getElementById('exportFormat').value;
    const f = readFilterFields(EXPORT_FILTER_IDS);
    const payload = {
        filename,
        format,
        ...buildFilterQueryObject(f)
    };

    while (true) {
        try {
            const data = await API.post('/api/records/export', payload);
            const matched = data.total_matched != null ? data.total_matched : data.count;
            alert(`导出成功\n文件：${data.filename}\n路径：${data.path}\n导出 ${data.count} 条（筛选命中 ${matched} 条）`);
            closeExportModal();
            return;
        } catch (e) {
            let msg = e.message || String(e);
            let errCode = '';
            try {
                const err = JSON.parse(msg);
                if (err.error) msg = err.error;
                if (err.error_code) errCode = err.error_code;
            } catch (_) { /* 非 JSON */ }

            if (errCode === 'file_exists') {
                const choice = await chooseExportConflictAction();
                if (choice === 'replace') {
                    payload.conflict_strategy = 'replace';
                    continue;
                }
                if (choice === 'keep_both') {
                    payload.conflict_strategy = 'keep_both';
                    continue;
                }
                alert('已取消导出');
                return;
            }

            alert('导出失败：' + msg);
            return;
        }
    }
}

function setupSettingsPage() {
    document.getElementById('btnOpenExport').addEventListener('click', openExportModal);
    document.getElementById('exportClose').addEventListener('click', closeExportModal);
    document.getElementById('exportCancel').addEventListener('click', closeExportModal);
    document.getElementById('exportSubmit').addEventListener('click', submitExport);
    document.getElementById('exportSyncFilters').addEventListener('click', syncExportFiltersFromRecordsPage);
    document.getElementById('exportModal').addEventListener('click', (ev) => {
        if (ev.target.id === 'exportModal') closeExportModal();
    });
    document.getElementById('expFilterType').addEventListener('change', populateExportFilterCategorySelect);
    document.getElementById('expFilterCatL1').addEventListener('change', updateExportFilterL2Select);
    document.querySelectorAll('[data-exp-range]').forEach(btn => {
        btn.addEventListener('click', () => setExportQuickDateRange(btn.dataset.expRange));
    });
    document.getElementById('btnOpenImport').addEventListener('click', openImportModal);
    document.getElementById('importClose').addEventListener('click', closeImportModal);
    document.getElementById('importCancel').addEventListener('click', closeImportModal);
    document.getElementById('importSubmit').addEventListener('click', submitImport);
    document.getElementById('importFormat').addEventListener('change', clearImportError);
    document.getElementById('importFile').addEventListener('change', clearImportError);
    document.getElementById('importModal').addEventListener('click', (ev) => {
        if (ev.target.id === 'importModal') closeImportModal();
    });
    document.getElementById('exportConflictReplace').addEventListener('click', () => {
        resolveExportConflict('replace');
    });
    document.getElementById('exportConflictKeepBoth').addEventListener('click', () => {
        resolveExportConflict('keep_both');
    });
    document.getElementById('exportConflictCancel').addEventListener('click', () => {
        resolveExportConflict('cancel');
    });
    document.getElementById('exportConflictModal').addEventListener('click', (ev) => {
        if (ev.target.id === 'exportConflictModal') {
            resolveExportConflict('cancel');
        }
    });

    document.getElementById('btnOpenCatManage').addEventListener('click', openCatManageModal);
    document.getElementById('catManageClose').addEventListener('click', closeCatManageModal);
    document.getElementById('catManageModal').addEventListener('click', (ev) => {
        if (ev.target.id === 'catManageModal') closeCatManageModal();
    });

    document.getElementById('catManageBody').addEventListener('click', (ev) => {
        const btn = ev.target.closest('button');
        if (btn && btn.dataset.ctype) {
            settingsCatType = btn.dataset.ctype;
            catManageExpandedL1Id = 0;
            renderCatManageModal();
            return;
        }

        if (btn && btn.dataset.act) {
            const act = btn.dataset.act;
            if (act === 'add-l1') settingsAddL1();
            else if (act === 'edit-l1') settingsEditL1(parseInt(btn.dataset.id, 10));
            else if (act === 'del-l1') settingsDeleteL1(parseInt(btn.dataset.id, 10), btn.dataset.name);
            else if (act === 'reset-cats') settingsResetCategories();
            else if (act === 'add-l2') settingsAddL2(parseInt(btn.dataset.l1, 10), btn.dataset.l1name);
            else if (act === 'edit-l2') settingsEditL2(parseInt(btn.dataset.id, 10), parseInt(btn.dataset.l1, 10), btn.dataset.name);
            else if (act === 'del-l2') settingsDeleteL2(parseInt(btn.dataset.id, 10), btn.dataset.name);
            return;
        }

        const l1Row = ev.target.closest('.cat-manage-l1-row[data-l1-id]');
        if (l1Row) {
            toggleCatManageL1(parseInt(l1Row.dataset.l1Id, 10));
        }
    });
}

// 拉取指定日期区间内的全部记录（用于统计聚合）
async function fetchRecordsInRange(dateFrom, dateTo) {
    const params = new URLSearchParams({
        date_from: dateFrom,
        date_to: dateTo,
        page: '1',
        page_size: '10000',
        sort_by: 'datetime',
        sort_order: 'asc'
    });
    const data = await API.get('/api/records?' + params.toString());
    return data.records || [];
}

// 从记录 datetime 字段提取日期部分 YYYY-MM-DD
function recordDateKey(datetime) {
    return (datetime || '').slice(0, 10);
}

// 按日期聚合收入/支出
function aggregateByDay(records) {
    const map = {};
    for (const r of records) {
        const key = recordDateKey(r.datetime);
        if (!key) continue;
        if (!map[key]) map[key] = { income: 0, expense: 0 };
        if (r.type === 'income') map[key].income += r.amount;
        else map[key].expense += r.amount;
    }
    return map;
}

// 按月份聚合（key: YYYY-MM）
function aggregateByMonth(records) {
    const map = {};
    for (const r of records) {
        const key = recordDateKey(r.datetime).slice(0, 7);
        if (!key || key.length < 7) continue;
        if (!map[key]) map[key] = { income: 0, expense: 0 };
        if (r.type === 'income') map[key].income += r.amount;
        else map[key].expense += r.amount;
    }
    return map;
}

// 渲染统计页顶部：选中日期的记录列表
function renderStatsDayList(records, dayStr) {
    const dayRecords = records.filter(r => recordDateKey(r.datetime) === dayStr);
    let income = 0, expense = 0;
    for (const r of dayRecords) {
        if (r.type === 'income') income += r.amount;
        else expense += r.amount;
    }

    const isToday = dayStr === toDateInputValue(new Date());
    document.getElementById('statsDayTitle').textContent =
        isToday ? '今日记录' : `${dayStr} 记录`;
    document.getElementById('statsDayIncome').textContent = `¥${income.toFixed(2)}`;
    document.getElementById('statsDayExpense').textContent = `¥${expense.toFixed(2)}`;

    const tbody = document.getElementById('statsDayBody');
    if (dayRecords.length === 0) {
        tbody.innerHTML = '<tr class="empty-row"><td colspan="5">暂无记录</td></tr>';
        return;
    }
    tbody.innerHTML = dayRecords.map(r => {
        const typeLabel = r.type === 'income' ? '收入' : '支出';
        const typeClass = r.type === 'income' ? 'type-income' : 'type-expense';
        const amountClass = r.type === 'income' ? 'amount-income' : 'amount-expense';
        const sign = r.type === 'income' ? '+' : '-';
        const timePart = (r.datetime || '').length > 11 ? r.datetime.slice(11) : r.datetime;
        const l1 = r.category_l1 || '';
        const l2 = r.category_l2 || '';
        const cat = l1 && l2 ? `${l1} / ${l2}` : (l1 || l2 || '—');
        return `
            <tr>
                <td>${escapeHtml(timePart)}</td>
                <td><span class="type-tag ${typeClass}">${typeLabel}</span></td>
                <td class="${amountClass}">${sign}¥${r.amount.toFixed(2)}</td>
                <td>${escapeHtml(cat)}</td>
                <td>${escapeHtml(r.note || '')}</td>
            </tr>
        `;
    }).join('');
}

// 渲染当月日历
function renderStatsCalendar(year, month, dayTotals) {
    const grid = document.getElementById('statsCalGrid');
    const firstDay = new Date(year, month, 1).getDay();
    const daysInMonth = new Date(year, month + 1, 0).getDate();
    const todayStr = toDateInputValue(new Date());
    const cells = [];

    for (let i = 0; i < firstDay; i++) {
        cells.push('<div class="cal-cell empty"></div>');
    }
    for (let d = 1; d <= daysInMonth; d++) {
        const dateStr = `${year}-${String(month + 1).padStart(2, '0')}-${String(d).padStart(2, '0')}`;
        const totals = dayTotals[dateStr] || { income: 0, expense: 0 };
        const hasData = totals.income > 0 || totals.expense > 0;
        let cls = 'cal-cell';
        if (dateStr === todayStr) cls += ' today';
        if (dateStr === statsSelectedDay) cls += ' selected';
        if (hasData) cls += ' has-data';

        const incHtml = totals.income > 0
            ? `<div class="cal-inc">+${totals.income.toFixed(0)}</div>`
            : '<div class="cal-zero">—</div>';
        const expHtml = totals.expense > 0
            ? `<div class="cal-exp">-${totals.expense.toFixed(0)}</div>`
            : '';

        cells.push(`
            <div class="${cls}" data-date="${dateStr}">
                <div class="cal-day-num">${d}</div>
                <div class="cal-amounts">${incHtml}${expHtml}</div>
            </div>
        `);
    }
    grid.innerHTML = cells.join('');

    grid.querySelectorAll('.cal-cell[data-date]').forEach(cell => {
        cell.addEventListener('click', () => {
            statsSelectedDay = cell.dataset.date;
            renderStatsDayList(statsMonthRecords, statsSelectedDay);
            grid.querySelectorAll('.cal-cell').forEach(c => c.classList.remove('selected'));
            cell.classList.add('selected');
        });
    });
}

// 统计图月份范围：全年 12 个月（0—11 对应 1—12 月）
const STATS_LAST_MONTH = 11;

// 确保选中日期落在当前查看的月份内
function ensureSelectedDayInMonth(year, month) {
    const prefix = `${year}-${String(month + 1).padStart(2, '0')}`;
    if (statsSelectedDay.startsWith(prefix)) return;

    const now = new Date();
    if (year === now.getFullYear() && month === now.getMonth()) {
        statsSelectedDay = toDateInputValue(now);
    } else {
        statsSelectedDay = toDateInputValue(new Date(year, month, 1));
    }
}

// 图表金额格式化（纵轴刻度，可带单位）
function formatAxisAmount(n) {
    if (n >= 10000) {
        const w = n / 10000;
        return (w % 1 === 0 ? w.toFixed(0) : w.toFixed(1)) + '万';
    }
    if (n >= 1000) {
        const k = n / 1000;
        return (k % 1 === 0 ? k.toFixed(0) : k.toFixed(1)) + 'k';
    }
    return Math.round(n).toString();
}

// 柱顶金额标签（显示具体数额）
function formatBarAmount(n) {
    if (n <= 0) return '';
    if (n >= 10000) {
        const w = n / 10000;
        return '¥' + (w % 1 === 0 ? w.toFixed(0) : w.toFixed(1)) + '万';
    }
    return '¥' + (Number.isInteger(n) ? n : n.toFixed(2));
}

// 渲染纵坐标刻度（0 ～ max，共 5 档）
function renderStatsYAxis(maxVal) {
    const axis = document.getElementById('statsYAxis');
    const ticks = 4;
    const labels = [];
    for (let i = ticks; i >= 0; i--) {
        const v = (maxVal * i) / ticks;
        labels.push(`<span class="y-tick">¥${formatAxisAmount(v)}</span>`);
    }
    axis.innerHTML = labels.join('');
}

// 填充年份列表：2100→1900；展开时把当前年滚到列表最上方
function populateStatsYearList() {
    const list = document.getElementById('statsYearList');
    if (list.dataset.ready === '1') return;

    let html = '';
    for (let y = 2100; y >= 1900; y--) {
        html += `<li><button type="button" class="year-option" data-year="${y}" role="option">${y}年</button></li>`;
    }
    list.innerHTML = html;
    list.dataset.ready = '1';

    list.querySelectorAll('.year-option').forEach(btn => {
        btn.addEventListener('click', () => {
            selectStatsYear(parseInt(btn.dataset.year, 10));
        });
    });
}

function updateStatsYearDisplay() {
    document.getElementById('statsYearDisplay').textContent = `${statsViewYear}年`;
    document.querySelectorAll('#statsYearList .year-option').forEach(btn => {
        const y = parseInt(btn.dataset.year, 10);
        const active = y === statsViewYear;
        btn.classList.toggle('active', active);
        btn.setAttribute('aria-selected', active ? 'true' : 'false');
    });
}

function openStatsYearPanel() {
    const panel = document.getElementById('statsYearPanel');
    const list = document.getElementById('statsYearList');
    const trigger = document.getElementById('statsYearTrigger');

    panel.hidden = false;
    trigger.setAttribute('aria-expanded', 'true');
    updateStatsYearDisplay();

    const active = list.querySelector(`.year-option[data-year="${statsViewYear}"]`);
    if (active) {
        list.scrollTop = active.offsetTop;
    }
}

function closeStatsYearPanel() {
    document.getElementById('statsYearPanel').hidden = true;
    document.getElementById('statsYearTrigger').setAttribute('aria-expanded', 'false');
}

function toggleStatsYearPanel() {
    const panel = document.getElementById('statsYearPanel');
    if (panel.hidden) openStatsYearPanel();
    else closeStatsYearPanel();
}

async function selectStatsYear(year) {
    statsViewYear = year;
    updateStatsYearDisplay();
    closeStatsYearPanel();

    ensureSelectedDayInMonth(statsViewYear, statsViewMonth);
    await loadStatsYearData();
}

function updateStatsYearTitle() {
    document.getElementById('statsYearTitle').textContent =
        `${statsViewYear}年 · 各月收支（1月—12月）`;
}

// 渲染年度柱状图（全年 12 个月），月份按钮可跳转日历
function renderStatsYearChart(year, monthTotals, viewMonth) {
    const container = document.getElementById('statsYearChart');

    let maxVal = 1;
    for (let m = 0; m <= STATS_LAST_MONTH; m++) {
        const key = `${year}-${String(m + 1).padStart(2, '0')}`;
        const t = monthTotals[key] || { income: 0, expense: 0 };
        maxVal = Math.max(maxVal, t.income, t.expense);
    }
    renderStatsYAxis(maxVal);

    const cols = [];
    for (let m = 0; m <= STATS_LAST_MONTH; m++) {
        const key = `${year}-${String(m + 1).padStart(2, '0')}`;
        const t = monthTotals[key] || { income: 0, expense: 0 };
        const incH = t.income > 0 ? Math.max(4, (t.income / maxVal) * 100) : 0;
        const expH = t.expense > 0 ? Math.max(4, (t.expense / maxVal) * 100) : 0;
        const activeCol = m === viewMonth ? ' active' : '';
        const activeBtn = m === viewMonth ? ' active' : '';
        const incLabel = formatBarAmount(t.income);
        const expLabel = formatBarAmount(t.expense);

        cols.push(`
            <div class="bar-col${activeCol}">
                <div class="bar-pair">
                    <div class="bar-wrap">
                        <span class="bar-label inc${incLabel ? '' : ' empty'}">${incLabel || '0'}</span>
                        <div class="bar income" style="height:${incH}%"></div>
                    </div>
                    <div class="bar-wrap">
                        <span class="bar-label exp${expLabel ? '' : ' empty'}">${expLabel || '0'}</span>
                        <div class="bar expense" style="height:${expH}%"></div>
                    </div>
                </div>
                <button type="button" class="month-btn${activeBtn}" data-month="${m}">
                    ${m + 1}月
                </button>
            </div>
        `);
    }
    container.innerHTML = cols.join('');

    container.querySelectorAll('.month-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            selectStatsMonth(parseInt(btn.dataset.month, 10));
        });
    });
}

// 点击月份：切换日历到该月并刷新日记录
async function selectStatsMonth(month) {
    statsViewMonth = month;
    ensureSelectedDayInMonth(statsViewYear, month);

    const monthTotals = aggregateByMonth(statsYearRecords);
    renderStatsYearChart(statsViewYear, monthTotals, month);
    await loadStatsMonthView();
}

// 加载当前查看月份的日历与日记录
async function loadStatsMonthView() {
    const year = statsViewYear;
    const month = statsViewMonth;
    const monthFrom = toDateInputValue(new Date(year, month, 1));
    const monthTo = toDateInputValue(new Date(year, month + 1, 0));

    document.getElementById('statsDateLabel').textContent = `${year}年`;
    document.getElementById('statsMonthTitle').textContent =
        `${year}年${month + 1}月 · 按日汇总`;

    ensureSelectedDayInMonth(year, month);

    try {
        statsMonthRecords = await fetchRecordsInRange(monthFrom, monthTo);
        const dayTotals = aggregateByDay(statsMonthRecords);
        renderStatsCalendar(year, month, dayTotals);
        renderStatsDayList(statsMonthRecords, statsSelectedDay);
    } catch (e) {
        console.error('[App] 加载月份统计失败:', e);
    }
}

// 加载选中年份的柱状图数据
async function loadStatsYearData() {
    const yearFrom = toDateInputValue(new Date(statsViewYear, 0, 1));
    const yearTo = toDateInputValue(new Date(statsViewYear, 11, 31));

    updateStatsYearTitle();

    try {
        statsYearRecords = await fetchRecordsInRange(yearFrom, yearTo);
        const monthTotals = aggregateByMonth(statsYearRecords);
        renderStatsYearChart(statsViewYear, monthTotals, statsViewMonth);
        await loadStatsMonthView();
    } catch (e) {
        console.error('[App] 加载年度统计失败:', e);
    }
}

// 加载并刷新统计页全部区块（年 → 月 → 日）
async function loadStatsPage() {
    const now = new Date();
    if (!statsSelectedDay) {
        statsSelectedDay = toDateInputValue(now);
    }
    ensureSelectedDayInMonth(statsViewYear, statsViewMonth);

    updateStatsYearDisplay();
    await loadStatsYearData();
}

// 记账操作后若当前在统计页则同步刷新
function refreshStatsIfVisible() {
    const statsPane = document.getElementById('pane-stats');
    if (statsPane && statsPane.classList.contains('active')) {
        loadStatsPage();
    }
}

// ── 筛选辅助 ────────────────────────────────────────────────────────

// 将 Date 对象格式化为 <input type="date"> 需要的 "YYYY-MM-DD"
function toDateInputValue(d) {
    const local = new Date(d.getTime() - d.getTimezoneOffset() * 60000);
    return local.toISOString().slice(0, 10);
}

// 快捷日期：根据预设区间设置开始/结束日期输入框
function applyQuickDateRangeToElements(fromEl, toEl, range) {
    const now = new Date();
    const y = now.getFullYear();
    const m = now.getMonth();

    if (range === 'all') {
        fromEl.value = '';
        toEl.value = '';
    } else if (range === 'thisMonth') {
        fromEl.value = toDateInputValue(new Date(y, m, 1));
        toEl.value = toDateInputValue(new Date(y, m + 1, 0));
    } else if (range === 'lastMonth') {
        fromEl.value = toDateInputValue(new Date(y, m - 1, 1));
        toEl.value = toDateInputValue(new Date(y, m, 0));
    } else if (range === 'thisYear') {
        fromEl.value = toDateInputValue(new Date(y, 0, 1));
        toEl.value = toDateInputValue(new Date(y, 11, 31));
    }
}

function setQuickDateRange(range) {
    const fromEl = document.getElementById('filterDateFrom');
    const toEl = document.getElementById('filterDateTo');
    applyQuickDateRangeToElements(fromEl, toEl, range);
    currentPage = 1;
    loadRecords();
}

function setExportQuickDateRange(range) {
    applyQuickDateRangeToElements(
        document.getElementById('expFilterDateFrom'),
        document.getElementById('expFilterDateTo'),
        range
    );
}

// 重置全部筛选条件，回到默认视图
function resetFilters() {
    document.getElementById('filterType').value = '';
    populateFilterCategorySelect();
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
    const now = new Date();
    statsSelectedDay = toDateInputValue(now);
    statsViewYear = now.getFullYear();
    statsViewMonth = now.getMonth();

    await loadCategories();          // 1. 拉分类 → 写入 categories 缓存
    populateFilterCategorySelect();  // 2. 填充筛选栏一级分类下拉框
    await loadRecords();             // 3. 拉记录 → 渲染表格和汇总

    // 分类选择器事件（新增 / 编辑弹窗各一套）
    setupCategoryPicker('f');
    setupCategoryPicker('e');

    // ── 事件绑定（用户操作 → 回调函数） ─────────────────────────────
    // addEventListener(事件名, 回调函数)：注册事件监听，类似 C++ 信号/槽

    // 底部导航切换
    document.querySelectorAll('.nav-item').forEach(btn => {
        btn.addEventListener('click', () => switchTab(btn.dataset.tab));
    });

    setupSettingsPage();

    populateStatsYearList();
    updateStatsYearDisplay();
    document.getElementById('statsYearTrigger').addEventListener('click', (ev) => {
        ev.stopPropagation();
        toggleStatsYearPanel();
    });
    document.getElementById('statsYearPicker').addEventListener('click', (ev) => {
        ev.stopPropagation();
    });
    document.addEventListener('click', () => closeStatsYearPanel());

    // 「添加记录」按钮 → 打开新增弹窗
    document.getElementById('btnOpenAdd').addEventListener('click', openAddModal);

    // 表单提交 → 新增记录
    document.getElementById('addForm').addEventListener('submit', addRecord);

    // 关闭按钮 / 取消按钮 → 关闭新增弹窗
    document.getElementById('addClose').addEventListener('click', closeAddModal);
    document.getElementById('addCancel').addEventListener('click', closeAddModal);

    // 点击遮罩层（非内容区域）→ 关闭新增弹窗
    document.getElementById('addModal').addEventListener('click', (ev) => {
        if (ev.target.id === 'addModal') closeAddModal();
    });

    document.getElementById('filterType').addEventListener('change', () => {
        populateFilterCategorySelect();
        currentPage = 1;
        loadRecords();
    });

    // 下拉类筛选：变化即重置到第 1 页并重新加载
    ['filterCatL2', 'filterSortBy', 'filterSortOrder'].forEach(id => {
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
