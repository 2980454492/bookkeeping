// ── 全局状态 ────────────────────────────────────────────────────────
let categories = { expense: [], income: [] };
let currentRecords = [];
let currentPage = 1;
const PAGE_SIZE = 50;

// ── API 请求封装 ────────────────────────────────────────────────────
const API = {
    async get(url) {
        const res = await fetch(url);
        if (!res.ok) throw new Error(await res.text());
        return res.json();
    },
    async post(url, data) {
        const res = await fetch(url, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(data)
        });
        if (!res.ok) throw new Error(await res.text());
        return res.json();
    },
    async del(url) {
        const res = await fetch(url, { method: 'DELETE' });
        if (!res.ok) throw new Error(await res.text());
        return res.json();
    }
};

// ── 分类工具函数 ────────────────────────────────────────────────────

function getCategoriesByType(type) {
    return categories[type] || [];
}

function getSubcategories(l1Name) {
    const all = [...categories.expense, ...categories.income];
    const found = all.find(c => c.name === l1Name);
    return found ? (found.subcategories || []) : [];
}

// ── 加载分类 ──────────────────────────────────────────────────────────

async function loadCategories() {
    try {
        const data = await API.get('/api/categories');
        categories.expense = [];
        categories.income = [];
        for (const cat of data) {
            if (cat.type === 'expense') categories.expense.push(cat);
            else categories.income.push(cat);
        }
        console.log('[App] 分类已加载:', categories.expense.length + categories.income.length);
    } catch (e) {
        console.error('[App] 加载分类失败:', e);
    }
}

function populateCategorySelects() {
    const fType = document.getElementById('fType').value;
    const fCatL1 = document.getElementById('fCatL1');
    const filterCatL1 = document.getElementById('filterCatL1');

    const cats = getCategoriesByType(fType);

    // 填充表单中的一级分类下拉框
    fCatL1.innerHTML = '<option value="">请选择</option>';
    for (const cat of cats) {
        fCatL1.innerHTML += `<option value="${escapeHtml(cat.name)}">${cat.icon || ''} ${cat.name}</option>`;
    }
    updateL2Select();

    // 填充筛选栏中的一级分类下拉框
    filterCatL1.innerHTML = '<option value="">全部分类</option>';
    for (const cat of [...categories.expense, ...categories.income]) {
        filterCatL1.innerHTML += `<option value="${escapeHtml(cat.name)}">${cat.name}</option>`;
    }
}

function updateL2Select() {
    const fCatL1 = document.getElementById('fCatL1');
    const catL2Group = document.getElementById('catL2Group');
    const fCatL2 = document.getElementById('fCatL2');
    const fType = document.getElementById('fType').value;

    // 收入类型没有二级分类，隐藏
    if (fType === 'income') {
        catL2Group.style.display = 'none';
        fCatL2.innerHTML = '<option value="">—</option>';
        return;
    }

    catL2Group.style.display = '';
    const l1Name = fCatL1.value;
    const subs = getSubcategories(l1Name);

    fCatL2.innerHTML = '<option value="">请选择</option>';
    for (const sub of subs) {
        fCatL2.innerHTML += `<option value="${escapeHtml(sub)}">${sub}</option>`;
    }
}

// ── 加载记录 ─────────────────────────────────────────────────────────

async function loadRecords() {
    const params = new URLSearchParams();
    const filterType = document.getElementById('filterType').value;
    const filterCatL1 = document.getElementById('filterCatL1').value;

    if (filterType) params.set('type', filterType);
    if (filterCatL1) params.set('cat_l1', filterCatL1);
    params.set('page', currentPage);
    params.set('page_size', PAGE_SIZE);
    params.set('sort_by', 'datetime');
    params.set('sort_order', 'desc');

    try {
        const data = await API.get('/api/records?' + params.toString());
        currentRecords = data.records || [];
        document.getElementById('recordCount').textContent =
            `${data.total || 0} 条记录`;
        renderRecords();
        updateSummary();
    } catch (e) {
        console.error('[App] 加载记录失败:', e);
    }
}

// ── 渲染记录表格 ─────────────────────────────────────────────────────

function renderRecords() {
    const tbody = document.getElementById('recordBody');
    if (currentRecords.length === 0) {
        tbody.innerHTML = '<tr class="empty-row"><td colspan="7">暂无记录，添加一条吧 🎉</td></tr>';
        return;
    }
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
                <td><button class="btn btn-danger" onclick="deleteRecord(${r.id})">删除</button></td>
            </tr>
        `;
    }).join('');
}

// ── 汇总统计 ──────────────────────────────────────────────────────────

async function updateSummary() {
    try {
        // 从全部记录中计算汇总数据
        let totalIncome = 0, totalExpense = 0;
        // 拉取所有记录用于汇总（不经过分页筛选）
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
        balEl.style.color = balance >= 0 ? '#52c41a' : '#ff4d4f';
    } catch (e) {
        console.error('[App] 更新汇总失败:', e);
    }
}

// ── 新增记录 ─────────────────────────────────────────────────────────

async function addRecord(e) {
    e.preventDefault();
    const type = document.getElementById('fType').value;
    const amount = parseFloat(document.getElementById('fAmount').value);
    const datetime = document.getElementById('fDatetime').value;
    const catL1 = document.getElementById('fCatL1').value;
    const catL2 = document.getElementById('fCatL2').value;
    const note = document.getElementById('fNote').value;

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

    // 将 datetime-local 格式 "YYYY-MM-DDTHH:MM" 转换为 "YYYY-MM-DD HH:MM"
    const formatted = datetime.replace('T', ' ') + ':00';

    try {
        await API.post('/api/records', {
            datetime: formatted,
            type: type,
            amount: amount,
            category_l1: catL1,
            category_l2: type === 'income' ? '' : catL2,
            note: note
        });
        // 重置表单
        document.getElementById('fAmount').value = '';
        document.getElementById('fNote').value = '';
        document.getElementById('fDatetime').value = '';
        document.getElementById('fCatL1').value = '';
        document.getElementById('fCatL2').value = '';
        updateL2Select();
        await loadRecords();
    } catch (e) {
        alert('添加失败: ' + e.message);
    }
}

// ── 删除记录 ─────────────────────────────────────────────────────────

async function deleteRecord(id) {
    if (!confirm('确认删除这条记录？')) return;
    try {
        await API.del('/api/records/' + id);
        await loadRecords();
    } catch (e) {
        alert('删除失败: ' + e.message);
    }
}

// ── 工具函数 ──────────────────────────────────────────────────────────

function escapeHtml(str) {
    const div = document.createElement('div');
    div.textContent = str;
    return div.innerHTML;
}

// ── 初始化 ───────────────────────────────────────────────────────────

async function init() {
    // 将默认时间设为当前时间
    const now = new Date();
    const local = new Date(now.getTime() - now.getTimezoneOffset() * 60000)
        .toISOString().slice(0, 16);
    document.getElementById('fDatetime').value = local;

    await loadCategories();
    populateCategorySelects();
    await loadRecords();

    // 绑定事件监听
    document.getElementById('addForm').addEventListener('submit', addRecord);
    document.getElementById('fType').addEventListener('change', () => {
        populateCategorySelects();
    });
    document.getElementById('fCatL1').addEventListener('change', updateL2Select);
    document.getElementById('filterType').addEventListener('change', () => {
        currentPage = 1;
        loadRecords();
    });
    document.getElementById('filterCatL1').addEventListener('change', () => {
        currentPage = 1;
        loadRecords();
    });
    document.getElementById('btnRefresh').addEventListener('click', loadRecords);
}

init();
