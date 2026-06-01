#include "import_util.h"

#include <json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <sstream>
#include <unordered_map>

using json = nlohmann::json;

namespace {

std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

std::string normalizeField(const std::string& s) {
    std::string t = trim(s);
    if (t == "—" || t == "-") return "";
    return t;
}

std::optional<std::string> parseType(const std::string& raw) {
    std::string t = trim(raw);
    if (t == "收入" || t == "income") return "income";
    if (t == "支出" || t == "expense") return "expense";
    return std::nullopt;
}

bool parseAmount(const std::string& raw, double& out) {
    std::string t = trim(raw);
    if (t.empty()) return false;
    try {
        size_t pos = 0;
        out = std::stod(t, &pos);
        if (pos != t.size()) return false;
    } catch (...) {
        return false;
    }
    return out > 0 && std::isfinite(out);
}

ImportParseResult fail(const std::string& msg) {
    ImportParseResult r;
    r.error = msg;
    return r;
}

std::string stripUtf8Bom(const std::string& s) {
    if (s.size() >= 3 &&
        static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF) {
        return s.substr(3);
    }
    return s;
}

std::vector<std::string> splitDelimitedLine(const std::string& line, char sep) {
    std::vector<std::string> fields;
    std::string field;
    bool in_quote = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (in_quote) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    in_quote = false;
                }
            } else {
                field += c;
            }
        } else if (c == '"') {
            in_quote = true;
        } else if (c == sep) {
            fields.push_back(field);
            field.clear();
        } else {
            field += c;
        }
    }
    fields.push_back(field);
    return fields;
}

Record recordFromFields(
    const std::vector<std::string>& cols,
    int row_num,
    ImportParseResult& out) {

    if (cols.size() < 3) {
        out.error = "第 " + std::to_string(row_num) + " 行：列数不足";
        return {};
    }

    Record r;
    r.datetime = trim(cols[0]);
    auto type_opt = parseType(cols[1]);
    if (!type_opt) {
        out.error = "第 " + std::to_string(row_num) + " 行：类型无效（应为「收入」或「支出」）";
        return {};
    }
    r.type = *type_opt;
    if (!parseAmount(cols[2], r.amount)) {
        out.error = "第 " + std::to_string(row_num) + " 行：金额无效";
        return {};
    }
    r.category_l1 = cols.size() > 3 ? normalizeField(cols[3]) : "";
    r.category_l2 = cols.size() > 4 ? normalizeField(cols[4]) : "";
    r.note = cols.size() > 5 ? normalizeField(cols[5]) : "";

    if (r.datetime.empty()) {
        out.error = "第 " + std::to_string(row_num) + " 行：时间为空";
        return {};
    }
    return r;
}

bool headerMatches(const std::vector<std::string>& hdr) {
    static const char* expected[] = {"时间", "类型", "金额", "一级分类", "二级分类", "备注"};
    if (hdr.size() < 3) return false;
    for (int i = 0; i < 6; ++i) {
        if (i >= static_cast<int>(hdr.size())) break;
        if (trim(hdr[static_cast<size_t>(i)]) != expected[i]) return false;
    }
    return true;
}

ImportParseResult parseCsvTsv(const std::string& content, char sep) {
    ImportParseResult result;
    std::string text = stripUtf8Bom(content);
    std::istringstream in(text);
    std::string line;
    int row_num = 0;
    bool header_checked = false;

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (trim(line).empty()) continue;
        ++row_num;
        auto cols = splitDelimitedLine(line, sep);
        if (!header_checked) {
            if (!headerMatches(cols)) {
                return fail("无法识别表头，请使用本应用导出的 CSV/TSV 文件");
            }
            header_checked = true;
            continue;
        }
        Record r = recordFromFields(cols, row_num, result);
        if (!result.error.empty()) return result;
        result.records.push_back(std::move(r));
    }

    if (!header_checked) return fail("文件为空或格式无法识别");
    result.ok = true;
    return result;
}

ImportParseResult parseJsonContent(const std::string& content) {
    ImportParseResult result;
    try {
        auto j = json::parse(content);
        if (!j.is_array()) return fail("JSON 格式无效：根节点应为数组");

        int idx = 0;
        for (const auto& item : j) {
            ++idx;
            if (!item.is_object()) return fail("第 " + std::to_string(idx) + " 条：应为对象");

            Record r;
            if (!item.contains("datetime") || !item["datetime"].is_string())
                return fail("第 " + std::to_string(idx) + " 条：缺少 datetime");
            r.datetime = trim(item["datetime"].get<std::string>());

            if (!item.contains("type") || !item["type"].is_string())
                return fail("第 " + std::to_string(idx) + " 条：缺少 type");
            auto type_opt = parseType(item["type"].get<std::string>());
            if (!type_opt)
                return fail("第 " + std::to_string(idx) + " 条：type 无效");
            r.type = *type_opt;

            if (!item.contains("amount") || !item["amount"].is_number())
                return fail("第 " + std::to_string(idx) + " 条：缺少 amount");
            r.amount = item["amount"].get<double>();
            if (r.amount <= 0 || !std::isfinite(r.amount))
                return fail("第 " + std::to_string(idx) + " 条：金额无效");

            r.category_l1 = item.value("category_l1", "");
            r.category_l2 = item.value("category_l2", "");
            r.note = item.value("note", "");
            r.category_l1 = normalizeField(r.category_l1);
            r.category_l2 = normalizeField(r.category_l2);
            r.note = normalizeField(r.note);

            if (r.datetime.empty())
                return fail("第 " + std::to_string(idx) + " 条：datetime 为空");

            result.records.push_back(std::move(r));
        }
    } catch (const json::parse_error&) {
        return fail("JSON 格式无法解析");
    }
    result.ok = true;
    return result;
}

ImportParseResult parseTxtContent(const std::string& content) {
    ImportParseResult result;
    if (content.find("个人记账导出") == std::string::npos)
        return fail("无法识别的文本格式");

    std::istringstream in(content);
    std::string line;
    Record current;
    bool in_record = false;
    int record_num = 0;

    auto flushRecord = [&]() {
        if (!in_record) return true;
        if (current.datetime.empty() || current.type.empty() || current.amount <= 0) {
            result.error = "第 " + std::to_string(record_num) + " 条记录字段不完整";
            return false;
        }
        result.records.push_back(current);
        current = Record{};
        in_record = false;
        return true;
    };

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        line = trim(line);
        if (line.empty()) continue;

        if (line.size() >= 40 && line.find_first_not_of('-') == std::string::npos) {
            if (!flushRecord()) return result;
            continue;
        }

        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = trim(line.substr(0, colon));
        std::string val = normalizeField(line.substr(colon + 1));

        if (key == "时间") {
            if (!flushRecord()) return result;
            ++record_num;
            in_record = true;
            current.datetime = val;
        } else if (key == "类型") {
            auto type_opt = parseType(val);
            if (!type_opt) {
                result.error = "第 " + std::to_string(record_num) + " 条：类型无效";
                return result;
            }
            current.type = *type_opt;
        } else if (key == "金额") {
            if (!parseAmount(val, current.amount)) {
                result.error = "第 " + std::to_string(record_num) + " 条：金额无效";
                return result;
            }
        } else if (key == "一级分类") {
            current.category_l1 = val;
        } else if (key == "二级分类") {
            current.category_l2 = val;
        } else if (key == "备注") {
            current.note = val;
        }
    }

    if (!flushRecord()) return result;
    if (record_num == 0 && result.records.empty())
        return fail("未找到可导入的记录");

    result.ok = true;
    return result;
}

// ── Base64 ─────────────────────────────────────────────────────────

std::string base64Decode(const std::string& input) {
    static const int8_t table[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };

    std::string out;
    out.reserve(input.size() * 3 / 4);
    int val = 0;
    int bits = -8;
    for (unsigned char c : input) {
        if (std::isspace(c)) continue;
        if (c == '=') break;
        int8_t d = table[c];
        if (d < 0) continue;
        val = (val << 6) + d;
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<char>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

uint32_t readLe32(const std::string& data, size_t pos) {
    return static_cast<uint32_t>(static_cast<unsigned char>(data[pos])) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[pos + 1])) << 8) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[pos + 2])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[pos + 3])) << 24);
}

uint16_t readLe16(const std::string& data, size_t pos) {
    return static_cast<uint16_t>(static_cast<unsigned char>(data[pos])) |
           (static_cast<uint16_t>(static_cast<unsigned char>(data[pos + 1])) << 8);
}

bool extractZipEntry(const std::string& zip, const std::string& name, std::string& out) {
    size_t pos = 0;
    while (pos + 30 <= zip.size()) {
        if (readLe32(zip, pos) != 0x04034b50u) break;
        uint16_t method = readLe16(zip, pos + 8);
        uint32_t comp_size = readLe32(zip, pos + 18);
        uint16_t name_len = readLe16(zip, pos + 26);
        uint16_t extra_len = readLe16(zip, pos + 28);
        size_t name_start = pos + 30;
        if (name_start + name_len > zip.size()) break;
        std::string entry_name = zip.substr(name_start, name_len);
        size_t data_start = name_start + name_len + extra_len;
        if (data_start + comp_size > zip.size()) break;
        if (entry_name == name) {
            if (method != 0) return false;
            out = zip.substr(data_start, comp_size);
            return true;
        }
        pos = data_start + comp_size;
    }
    return false;
}

std::string xmlUnescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '&' && i + 3 < s.size()) {
            if (s.compare(i, 4, "&lt;") == 0) { out += '<'; i += 3; continue; }
            if (s.compare(i, 4, "&gt;") == 0) { out += '>'; i += 3; continue; }
            if (s.compare(i, 5, "&amp;") == 0) { out += '&'; i += 4; continue; }
            if (s.compare(i, 6, "&quot;") == 0) { out += '"'; i += 5; continue; }
            if (s.compare(i, 6, "&apos;") == 0) { out += '\''; i += 5; continue; }
        }
        out += s[i];
    }
    return out;
}

int colFromCellRef(const std::string& ref) {
    int col = 0;
    for (char c : ref) {
        if (c >= 'A' && c <= 'Z') col = col * 26 + (c - 'A' + 1);
        else break;
    }
    return col - 1;
}

std::string extractInlineCellText(const std::string& cell_xml) {
    auto t_open = cell_xml.find("<t>");
    if (t_open == std::string::npos) {
        t_open = cell_xml.find("<t ");
        if (t_open == std::string::npos) return "";
        t_open = cell_xml.find('>', t_open);
        if (t_open == std::string::npos) return "";
        ++t_open;
    } else {
        t_open += 3;
    }
    auto t_close = cell_xml.find("</t>", t_open);
    if (t_close == std::string::npos) return "";
    return xmlUnescape(cell_xml.substr(t_open, t_close - t_open));
}

ImportParseResult parseXlsxBinary(const std::string& binary) {
    ImportParseResult result;
    if (binary.size() < 4 ||
        static_cast<unsigned char>(binary[0]) != 0x50 ||
        static_cast<unsigned char>(binary[1]) != 0x4B) {
        return fail("不是有效的 Excel (.xlsx) 文件");
    }

    std::string sheet;
    if (!extractZipEntry(binary, "xl/worksheets/sheet1.xml", sheet))
        return fail("无法读取 Excel 工作表");

    size_t pos = 0;
    int row_num = 0;
    bool header_checked = false;

    while (true) {
        auto row_start = sheet.find("<row", pos);
        if (row_start == std::string::npos) break;
        auto row_end = sheet.find("</row>", row_start);
        if (row_end == std::string::npos) break;
        std::string row_xml = sheet.substr(row_start, row_end - row_start + 6);
        pos = row_end + 6;
        ++row_num;

        std::unordered_map<int, std::string> cells;
        size_t cpos = 0;
        while (true) {
            auto c_start = row_xml.find("<c ", cpos);
            if (c_start == std::string::npos) break;
            auto c_end = row_xml.find("</c>", c_start);
            if (c_end == std::string::npos) break;
            std::string cell = row_xml.substr(c_start, c_end - c_start + 4);
            cpos = c_end + 4;

            auto r_attr = cell.find(" r=\"");
            if (r_attr == std::string::npos) continue;
            r_attr += 4;
            auto r_end = cell.find('"', r_attr);
            if (r_end == std::string::npos) continue;
            std::string ref = cell.substr(r_attr, r_end - r_attr);
            int col = colFromCellRef(ref);
            cells[col] = extractInlineCellText(cell);
        }

        std::vector<std::string> cols(6);
        for (int i = 0; i < 6; ++i) {
            auto it = cells.find(i);
            if (it != cells.end()) cols[static_cast<size_t>(i)] = it->second;
        }

        if (!header_checked) {
            if (!headerMatches(cols))
                return fail("无法识别 Excel 表头，请使用本应用导出的 .xlsx 文件");
            header_checked = true;
            continue;
        }

        if (std::all_of(cols.begin(), cols.end(), [](const std::string& s) { return trim(s).empty(); }))
            continue;

        Record r = recordFromFields(cols, row_num, result);
        if (!result.error.empty()) return result;
        result.records.push_back(std::move(r));
    }

    if (!header_checked) return fail("Excel 文件为空或格式无法识别");
    result.ok = true;
    return result;
}

} // namespace

ImportParseResult parseImportedRecords(
    const std::string& format,
    const std::string& content,
    bool content_is_base64) {

    static const std::vector<std::string> kFormats = {"txt", "csv", "tsv", "json", "xlsx"};
    std::string fmt = format;
    std::transform(fmt.begin(), fmt.end(), fmt.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (std::find(kFormats.begin(), kFormats.end(), fmt) == kFormats.end())
        return fail("不支持的导入格式");

    if (content.empty()) return fail("文件内容为空");

    if (fmt == "csv") return parseCsvTsv(content, ',');
    if (fmt == "tsv") return parseCsvTsv(content, '\t');
    if (fmt == "json") return parseJsonContent(content);
    if (fmt == "txt") return parseTxtContent(content);

    std::string binary = content_is_base64 ? base64Decode(content) : content;
    if (binary.empty()) return fail("Excel 文件内容无效");
    return parseXlsxBinary(binary);
}
