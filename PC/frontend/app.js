// ── State ────────────────────────────────────────────────────────────
let categories = { expense: [], income: [] };
let currentRecords = [];
let currentPage = 1;
const PAGE_SIZE = 50;

// ── API helpers ──────────────────────────────────────────────────────
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

// ── Category helpers ─────────────────────────────────────────────────

function getCategoriesByType(type) {
    return categories[type] || [];
}

function getSubcategories(l1Name) {
    const all = [...categories.expense, ...categories.income];
    const found = all.find(c => c.name === l1Name);
    return found ? (found.subcategories || []) : [];
}

// ── Load categories ──────────────────────────────────────────────────

async function loadCategories() {
    try {
        const data = await API.get('/api/categories');
        categories.expense = [];
        categories.income = [];
        for (const cat of data) {
            if (cat.type === 'expense') categories.expense.push(cat);
            else categories.income.push(cat);
        }
        console.log('[App] Categories loaded:', categories.expense.length + categories.income.length);
    } catch (e) {
        console.error('[App] Failed to load categories:', e);
    }
}

function populateCategorySelects() {
    const fType = document.getElementById('fType').value;
    const fCatL1 = document.getElementById('fCatL1');
    const filterCatL1 = document.getElementById('filterCatL1');

    const cats = getCategoriesByType(fType);

    // Add form L1 select
    fCatL1.innerHTML = '<option value="">请选择</option>';
    for (const cat of cats) {
        fCatL1.innerHTML += `<option value="${escapeHtml(cat.name)}">${cat.icon || ''} ${cat.name}</option>`;
    }
    updateL2Select();

    // Filter L1 select
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

    // Income has no L2 categories
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

// ── Load records ─────────────────────────────────────────────────────

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
        console.error('[App] Failed to load records:', e);
    }
}

// ── Render records table ─────────────────────────────────────────────

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

// ── Summary ──────────────────────────────────────────────────────────

async function updateSummary() {
    try {
        // Simple summary: calculate from all records
        let totalIncome = 0, totalExpense = 0;
        // Fetch all records for summary (without pagination filter)
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
        console.error('[App] Failed to update summary:', e);
    }
}

// ── Add record ───────────────────────────────────────────────────────

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

    // Convert datetime-local format "YYYY-MM-DDTHH:MM" to "YYYY-MM-DD HH:MM"
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
        // Reset form
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

// ── Delete record ────────────────────────────────────────────────────

async function deleteRecord(id) {
    if (!confirm('确认删除这条记录？')) return;
    try {
        await API.del('/api/records/' + id);
        await loadRecords();
    } catch (e) {
        alert('删除失败: ' + e.message);
    }
}

// ── Helpers ──────────────────────────────────────────────────────────

function escapeHtml(str) {
    const div = document.createElement('div');
    div.textContent = str;
    return div.innerHTML;
}

// ── Initialize ───────────────────────────────────────────────────────

async function init() {
    // Set default datetime to now
    const now = new Date();
    const local = new Date(now.getTime() - now.getTimezoneOffset() * 60000)
        .toISOString().slice(0, 16);
    document.getElementById('fDatetime').value = local;

    await loadCategories();
    populateCategorySelects();
    await loadRecords();

    // Event listeners
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
