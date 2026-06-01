// export_util.cpp — 导出工具实现
//
// 支持 txt / csv / tsv / json / xlsx 五种导出格式。
// xlsx 导出不依赖第三方库，用纯 C++ 实现 ZIP 打包 + Office Open XML 结构组装。

#include "export_util.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace {

std::string typeLabel(const std::string& type) {
    return type == "income" ? "收入" : "支出";
}

std::string sanitizeFilenameInput(const std::string& input) {
    std::string s;
    s.reserve(input.size());
    for (unsigned char ch : input) {
        if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' ||
            ch == '"' || ch == '<' || ch == '>' || ch == '|' || ch == '\0') {
            continue;
        }
        if (ch == '.' && s.empty()) continue;
        s.push_back(static_cast<char>(ch));
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '.')) {
        s.pop_back();
    }
    if (s.size() > 80) s.resize(80);
    return s;
}

std::string extensionForFormat(const std::string& format) {
    static const std::unordered_map<std::string, std::string> ext = {
        {"txt", ".txt"}, {"csv", ".csv"}, {"tsv", ".tsv"},
        {"json", ".json"}, {"xlsx", ".xlsx"}
    };
    auto it = ext.find(format);
    return it != ext.end() ? it->second : "";
}

std::string escapeCsvField(const std::string& s) {
    bool need_quote = false;
    for (char c : s) {
        if (c == '"' || c == ',' || c == '\n' || c == '\r' || c == '\t') {
            need_quote = true;
            break;
        }
    }
    if (!need_quote) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else out.push_back(c);
    }
    out += '"';
    return out;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

std::string xmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

// ── CRC32 校验（用于 ZIP 条目） ────────────────────────────────────

uint32_t crc32Update(uint32_t crc, uint8_t byte) {
    crc ^= byte;
    for (int i = 0; i < 8; ++i) {
        uint32_t mask = -(crc & 1u);
        crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
    return crc;
}

uint32_t crc32(const std::string& data) {
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char b : data) {
        crc = crc32Update(crc, b);
    }
    return crc ^ 0xFFFFFFFFu;
}

void writeLe16(std::ostringstream& os, uint16_t v) {
    os.put(static_cast<char>(v & 0xFF));
    os.put(static_cast<char>((v >> 8) & 0xFF));
}

void writeLe32(std::ostringstream& os, uint32_t v) {
    os.put(static_cast<char>(v & 0xFF));
    os.put(static_cast<char>((v >> 8) & 0xFF));
    os.put(static_cast<char>((v >> 16) & 0xFF));
    os.put(static_cast<char>((v >> 24) & 0xFF));
}

// ── 纯内存 ZIP 打包（STORE 模式，无压缩）──────────────────────────
// 用于生成 .xlsx 文件（xlsx 本质是 ZIP 容器），不依赖外部压缩库

std::string buildZipStore(const std::vector<std::pair<std::string, std::string>>& files) {
    struct Entry {
        std::string name;
        std::string data;
        uint32_t crc = 0;
        uint32_t offset = 0;
    };
    std::vector<Entry> entries;
    entries.reserve(files.size());
    for (const auto& f : files) {
        Entry e;
        e.name = f.first;
        e.data = f.second;
        e.crc = crc32(e.data);
        entries.push_back(std::move(e));
    }

    std::ostringstream zip;
    for (auto& e : entries) {
        e.offset = static_cast<uint32_t>(zip.tellp());
        writeLe32(zip, 0x04034b50u);
        writeLe16(zip, 20);
        writeLe16(zip, 0);
        writeLe16(zip, 0);
        writeLe16(zip, 0);
        writeLe16(zip, 0);
        writeLe32(zip, e.crc);
        writeLe32(zip, static_cast<uint32_t>(e.data.size()));
        writeLe32(zip, static_cast<uint32_t>(e.data.size()));
        writeLe16(zip, static_cast<uint16_t>(e.name.size()));
        writeLe16(zip, 0);
        zip.write(e.name.data(), static_cast<std::streamsize>(e.name.size()));
        zip.write(e.data.data(), static_cast<std::streamsize>(e.data.size()));
    }

    uint32_t central_offset = static_cast<uint32_t>(zip.tellp());
    for (const auto& e : entries) {
        writeLe32(zip, 0x02014b50u);
        writeLe16(zip, 20);
        writeLe16(zip, 20);
        writeLe16(zip, 0);
        writeLe16(zip, 0);
        writeLe16(zip, 0);
        writeLe16(zip, 0);
        writeLe32(zip, e.crc);
        writeLe32(zip, static_cast<uint32_t>(e.data.size()));
        writeLe32(zip, static_cast<uint32_t>(e.data.size()));
        writeLe16(zip, static_cast<uint16_t>(e.name.size()));
        writeLe16(zip, 0);
        writeLe16(zip, 0);
        writeLe16(zip, 0);
        writeLe16(zip, 0);
        writeLe32(zip, 0);
        writeLe32(zip, e.offset);
        zip.write(e.name.data(), static_cast<std::streamsize>(e.name.size()));
    }

    uint32_t central_size = static_cast<uint32_t>(zip.tellp()) - central_offset;
    writeLe32(zip, 0x06054b50u);
    writeLe16(zip, 0);
    writeLe16(zip, 0);
    writeLe16(zip, static_cast<uint16_t>(entries.size()));
    writeLe16(zip, static_cast<uint16_t>(entries.size()));
    writeLe32(zip, central_size);
    writeLe32(zip, central_offset);
    writeLe16(zip, 0);
    return zip.str();
}

// ── XLSX 生成（Office Open XML → ZIP） ─────────────────────────────
// xlsx 文件结构: [Content_Types].xml + _rels/.rels + xl/workbook.xml + xl/worksheets/sheet1.xml
// 所有 XML 作为 ZIP 条目打包为单个 xlsx 字节流

std::string buildXlsxSheet(const std::vector<Record>& records) {
    auto cell = [](int row, int col, const std::string& text) {
        char col_ch = static_cast<char>('A' + col);
        std::ostringstream c;
        c << "<c r=\"" << col_ch << row << "\" t=\"inlineStr\"><is><t>"
          << xmlEscape(text) << "</t></is></c>";
        return c.str();
    };

    std::ostringstream sheet;
    sheet << R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)"
          << R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          << "<sheetData>";
    const char* headers[] = {"时间", "类型", "金额", "一级分类", "二级分类", "备注"};
    sheet << "<row r=\"1\">";
    for (int i = 0; i < 6; ++i) sheet << cell(1, i, headers[i]);
    sheet << "</row>";

    int row = 2;
    for (const auto& r : records) {
        sheet << "<row r=\"" << row << "\">";
        sheet << cell(row, 0, r.datetime);
        sheet << cell(row, 1, typeLabel(r.type));
        std::ostringstream amt;
        amt << std::fixed << std::setprecision(2) << r.amount;
        sheet << cell(row, 2, amt.str());
        sheet << cell(row, 3, r.category_l1);
        sheet << cell(row, 4, r.category_l2);
        sheet << cell(row, 5, r.note);
        sheet << "</row>";
        ++row;
    }
    sheet << "</sheetData></worksheet>";
    return sheet.str();
}

std::string buildXlsxPackage(const std::vector<Record>& records) {
    const std::string sheet = buildXlsxSheet(records);
    const std::vector<std::pair<std::string, std::string>> parts = {
        {"[Content_Types].xml",
         R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)"
         R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
         R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
         R"(<Default Extension="xml" ContentType="application/xml"/>)"
         R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
         R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
         R"(</Types>)"},
        {"_rels/.rels",
         R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)"
         R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
         R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
         R"(</Relationships>)"},
        {"xl/_rels/workbook.xml.rels",
         R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)"
         R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
         R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
         R"(</Relationships>)"},
        {"xl/workbook.xml",
         R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)"
         R"(<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" )"
         R"(xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
         R"(<sheets><sheet name="记账记录" sheetId="1" r:id="rId1"/></sheets></workbook>)"},
        {"xl/worksheets/sheet1.xml", sheet}
    };
    return buildZipStore(parts);
}

bool writeBinaryFile(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return out.good();
}

bool writeTextFile(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << content;
    return out.good();
}

} // namespace

ExportFileResult exportRecordsToFile(
    const fs::path& root_dir,
    const std::string& filename_input,
    const std::string& format,
    const std::string& conflict_strategy,
    const std::vector<Record>& records) {

    ExportFileResult result;
    static const std::vector<std::string> kFormats = {"txt", "csv", "tsv", "json", "xlsx"};

    std::string fmt = format;
    std::transform(fmt.begin(), fmt.end(), fmt.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (std::find(kFormats.begin(), kFormats.end(), fmt) == kFormats.end()) {
        result.error = "不支持的导出格式";
        return result;
    }

    std::string stem = sanitizeFilenameInput(filename_input);
    if (stem.empty()) {
        result.error = "文件名无效";
        return result;
    }

    std::string ext = extensionForFormat(fmt);
    std::string lower_stem = stem;
    std::transform(lower_stem.begin(), lower_stem.end(), lower_stem.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower_stem.size() >= ext.size() &&
        lower_stem.compare(lower_stem.size() - ext.size(), ext.size(), ext) == 0) {
        stem = stem.substr(0, stem.size() - ext.size());
        while (!stem.empty() && stem.back() == '.') stem.pop_back();
        if (stem.empty()) {
            result.error = "文件名无效";
            return result;
        }
    }

    std::string strategy = conflict_strategy;
    std::transform(strategy.begin(), strategy.end(), strategy.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (strategy.empty()) strategy = "cancel";
    if (strategy != "cancel" && strategy != "replace" && strategy != "keep_both") {
        result.error = "无效的重名处理策略";
        return result;
    }

    result.filename = stem + ext;
    fs::path out_path = root_dir / result.filename;

    if (fs::exists(out_path)) {
        if (strategy == "cancel") {
            result.error = "文件已存在";
            return result;
        }
        if (strategy == "keep_both") {
            int suffix = 1;
            while (true) {
                result.filename = stem + "(" + std::to_string(suffix) + ")" + ext;
                out_path = root_dir / result.filename;
                if (!fs::exists(out_path)) break;
                ++suffix;
                if (suffix > 9999) {
                    result.error = "重名文件过多，无法自动命名";
                    return result;
                }
            }
        }
    }

    std::ostringstream content;
    const char* hdr_csv[] = {"时间", "类型", "金额", "一级分类", "二级分类", "备注"};

    if (fmt == "txt") {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf{};
#ifdef _WIN32
        localtime_s(&tm_buf, &t);
#else
        localtime_r(&t, &tm_buf);
#endif
        content << "个人记账导出\n";
        content << "导出时间: " << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << "\n";
        content << "记录条数: " << records.size() << "\n";
        content << std::string(60, '-') << "\n";
        for (const auto& r : records) {
            content << "时间: " << r.datetime << "\n"
                    << "类型: " << typeLabel(r.type) << "\n"
                    << "金额: " << std::fixed << std::setprecision(2) << r.amount << "\n"
                    << "一级分类: " << (r.category_l1.empty() ? "—" : r.category_l1) << "\n"
                    << "二级分类: " << (r.category_l2.empty() ? "—" : r.category_l2) << "\n"
                    << "备注: " << (r.note.empty() ? "—" : r.note) << "\n"
                    << std::string(40, '-') << "\n";
        }
        if (!writeTextFile(out_path, content.str())) {
            result.error = "写入文件失败";
            return result;
        }
    } else if (fmt == "csv" || fmt == "tsv") {
        const char sep = (fmt == "tsv") ? '\t' : ',';
        for (int i = 0; i < 6; ++i) {
            if (i) content << sep;
            content << escapeCsvField(hdr_csv[i]);
        }
        content << "\n";
        for (const auto& r : records) {
            std::ostringstream amt;
            amt << std::fixed << std::setprecision(2) << r.amount;
            std::vector<std::string> cols = {
                r.datetime, typeLabel(r.type), amt.str(),
                r.category_l1, r.category_l2, r.note
            };
            for (size_t i = 0; i < cols.size(); ++i) {
                if (i) content << sep;
                content << escapeCsvField(cols[i]);
            }
            content << "\n";
        }
        if (fmt == "csv") {
            if (!writeTextFile(out_path, "\xEF\xBB\xBF" + content.str())) {
                result.error = "写入文件失败";
                return result;
            }
        } else if (!writeTextFile(out_path, content.str())) {
            result.error = "写入文件失败";
            return result;
        }
    } else if (fmt == "json") {
        content << "[\n";
        for (size_t i = 0; i < records.size(); ++i) {
            const auto& r = records[i];
            content << "  {\n"
                    << "    \"id\": " << r.id << ",\n"
                    << "    \"datetime\": \"" << jsonEscape(r.datetime) << "\",\n"
                    << "    \"type\": \"" << jsonEscape(r.type) << "\",\n"
                    << "    \"amount\": " << std::fixed << std::setprecision(2) << r.amount << ",\n"
                    << "    \"category_l1\": \"" << jsonEscape(r.category_l1) << "\",\n"
                    << "    \"category_l2\": \"" << jsonEscape(r.category_l2) << "\",\n"
                    << "    \"note\": \"" << jsonEscape(r.note) << "\"\n"
                    << "  }" << (i + 1 < records.size() ? "," : "") << "\n";
        }
        content << "]\n";
        if (!writeTextFile(out_path, content.str())) {
            result.error = "写入文件失败";
            return result;
        }
    } else if (fmt == "xlsx") {
        if (!writeBinaryFile(out_path, buildXlsxPackage(records))) {
            result.error = "写入文件失败";
            return result;
        }
    }

    result.ok = true;
    result.filepath = out_path;
    result.count = static_cast<int>(records.size());
    return result;
}
