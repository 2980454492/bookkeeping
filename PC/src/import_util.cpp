#include "import_util.h"

#include <json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <zlib.h>

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

ImportParseResult failImport(
    const std::string& code,
    const std::string& error,
    const std::string& detail = "",
    const std::string& hint = "",
    int row = 0) {

    ImportParseResult r;
    r.error_code = code;
    r.error = error;
    r.detail = detail;
    r.hint = hint;
    r.row = row;
    return r;
}

void setRowError(ImportParseResult& out, const std::string& code, int row, const std::string& error) {
    out.error_code = code;
    out.row = row;
    out.error = error;
    out.hint = "请检查该行的时间、类型（收入/支出）、金额等字段是否符合要求";
}

std::string describeHeaderMismatch(const std::vector<std::string>& hdr) {
    std::ostringstream oss;
    oss << "期望表头：时间, 类型, 金额, 一级分类, 二级分类, 备注";
    if (!hdr.empty()) {
        oss << "；实际首行：";
        size_t n = std::min(hdr.size(), static_cast<size_t>(6));
        for (size_t i = 0; i < n; ++i) {
            if (i > 0) oss << ", ";
            std::string cell = trim(hdr[i]);
            oss << (cell.empty() ? "(空)" : cell);
        }
        if (hdr.size() > 6) oss << ", …";
    } else {
        oss << "；未能读取到表头单元格";
    }
    return oss.str();
}

int countXlsxRows(const std::string& sheet_xml) {
    int count = 0;
    size_t pos = 0;
    while (true) {
        auto row_start = sheet_xml.find("<row", pos);
        if (row_start == std::string::npos) break;
        ++count;
        auto row_end = sheet_xml.find("</row>", row_start);
        if (row_end == std::string::npos) break;
        pos = row_end + 6;
    }
    return count;
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
        setRowError(out, "row_insufficient_columns", row_num,
                    "第 " + std::to_string(row_num) + " 行：列数不足（至少需要时间、类型、金额三列）");
        return {};
    }

    Record r;
    r.datetime = trim(cols[0]);
    auto type_opt = parseType(cols[1]);
    if (!type_opt) {
        setRowError(out, "row_invalid_type", row_num,
                    "第 " + std::to_string(row_num) + " 行：类型无效（应为「收入」或「支出」）");
        return {};
    }
    r.type = *type_opt;
    if (!parseAmount(cols[2], r.amount)) {
        setRowError(out, "row_invalid_amount", row_num,
                    "第 " + std::to_string(row_num) + " 行：金额无效（须为正数）");
        return {};
    }
    r.category_l1 = cols.size() > 3 ? normalizeField(cols[3]) : "";
    r.category_l2 = cols.size() > 4 ? normalizeField(cols[4]) : "";
    r.note = cols.size() > 5 ? normalizeField(cols[5]) : "";

    if (r.datetime.empty()) {
        setRowError(out, "row_empty_datetime", row_num,
                    "第 " + std::to_string(row_num) + " 行：时间为空");
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
                return failImport(
                    "header_mismatch",
                    "无法识别 CSV/TSV 表头",
                    describeHeaderMismatch(cols),
                    "请使用本应用「导出」生成的 CSV/TSV，或确保首行为：时间,类型,金额,一级分类,二级分类,备注");
            }
            header_checked = true;
            continue;
        }
        Record r = recordFromFields(cols, row_num, result);
        if (!result.error.empty()) return result;
        result.records.push_back(std::move(r));
    }

    if (!header_checked) {
        return failImport(
            "empty_file",
            "CSV/TSV 文件为空或无可读行",
            "文件中未找到非空数据行",
            "请确认已选择正确文件，且首行包含表头");
    }
    result.ok = true;
    return result;
}

ImportParseResult parseJsonContent(const std::string& content) {
    ImportParseResult result;
    try {
        auto j = json::parse(content);
        if (!j.is_array()) {
            return failImport(
                "json_invalid_root",
                "JSON 格式无效",
                "根节点应为数组 []",
                "请使用本应用导出的 JSON 文件");
        }

        int idx = 0;
        for (const auto& item : j) {
            ++idx;
            if (!item.is_object()) {
                return failImport("json_invalid_item", "第 " + std::to_string(idx) + " 条：应为对象",
                                  "", "每条记录应为 JSON 对象", idx);
            }

            Record r;
            if (!item.contains("datetime") || !item["datetime"].is_string()) {
                return failImport("json_missing_field", "第 " + std::to_string(idx) + " 条：缺少 datetime",
                                  "", "需包含字符串字段 datetime", idx);
            }
            r.datetime = trim(item["datetime"].get<std::string>());

            if (!item.contains("type") || !item["type"].is_string()) {
                return failImport("json_missing_field", "第 " + std::to_string(idx) + " 条：缺少 type",
                                  "", "type 应为 income 或 expense（或中文 收入/支出）", idx);
            }
            auto type_opt = parseType(item["type"].get<std::string>());
            if (!type_opt) {
                return failImport("json_invalid_type", "第 " + std::to_string(idx) + " 条：type 无效",
                                  "", "type 应为 income/expense 或 收入/支出", idx);
            }
            r.type = *type_opt;

            if (!item.contains("amount") || !item["amount"].is_number()) {
                return failImport("json_missing_field", "第 " + std::to_string(idx) + " 条：缺少 amount",
                                  "", "需包含数字字段 amount", idx);
            }
            r.amount = item["amount"].get<double>();
            if (r.amount <= 0 || !std::isfinite(r.amount)) {
                return failImport("json_invalid_amount", "第 " + std::to_string(idx) + " 条：金额无效",
                                  "", "amount 须为大于 0 的数字", idx);
            }

            r.category_l1 = item.value("category_l1", "");
            r.category_l2 = item.value("category_l2", "");
            r.note = item.value("note", "");
            r.category_l1 = normalizeField(r.category_l1);
            r.category_l2 = normalizeField(r.category_l2);
            r.note = normalizeField(r.note);

            if (r.datetime.empty()) {
                return failImport("json_empty_datetime", "第 " + std::to_string(idx) + " 条：datetime 为空",
                                  "", "", idx);
            }

            result.records.push_back(std::move(r));
        }
    } catch (const json::parse_error& e) {
        return failImport(
            "json_parse_error",
            "JSON 格式无法解析",
            e.what(),
            "请检查文件是否为合法 JSON，或使用本应用导出的 JSON");
    }
    result.ok = true;
    return result;
}

ImportParseResult parseTxtContent(const std::string& content) {
    ImportParseResult result;
    if (content.find("个人记账导出") == std::string::npos) {
        return failImport(
            "txt_unrecognized",
            "无法识别的文本格式",
            "文件中未找到「个人记账导出」标记",
            "请使用本应用导出的 .txt 文件");
    }

    std::istringstream in(content);
    std::string line;
    Record current;
    bool in_record = false;
    int record_num = 0;

    auto flushRecord = [&]() {
        if (!in_record) return true;
        if (current.datetime.empty() || current.type.empty() || current.amount <= 0) {
            setRowError(result, "txt_incomplete_record", record_num,
                        "第 " + std::to_string(record_num) + " 条记录字段不完整");
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
                setRowError(result, "txt_invalid_type", record_num,
                            "第 " + std::to_string(record_num) + " 条：类型无效");
                return result;
            }
            current.type = *type_opt;
        } else if (key == "金额") {
            if (!parseAmount(val, current.amount)) {
                setRowError(result, "txt_invalid_amount", record_num,
                            "第 " + std::to_string(record_num) + " 条：金额无效");
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
    if (record_num == 0 && result.records.empty()) {
        return failImport(
            "txt_no_records",
            "未找到可导入的记录",
            "文本中无完整收支条目",
            "请确认文件为本应用导出的记账文本");
    }

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

struct ZipEntryMeta {
    uint16_t method = 0;
    uint32_t comp_size = 0;
    uint32_t uncomp_size = 0;
    uint32_t local_header_offset = 0;
};

bool inflateDeflateData(const std::string& in, size_t expected_size, std::string& out) {
    z_stream zs{};
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    zs.avail_in = static_cast<uInt>(in.size());
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return false;

    out.clear();
    out.resize(expected_size > 0 ? expected_size : (in.size() * 3 + 256));
    int ret = Z_OK;
    while (ret == Z_OK) {
        if (zs.total_out >= out.size()) out.resize(out.size() + in.size() + 256);
        zs.next_out = reinterpret_cast<Bytef*>(&out[zs.total_out]);
        zs.avail_out = static_cast<uInt>(out.size() - zs.total_out);
        ret = inflate(&zs, Z_NO_FLUSH);
    }
    inflateEnd(&zs);
    if (ret != Z_STREAM_END) return false;
    out.resize(zs.total_out);
    return true;
}

bool buildZipIndex(
    const std::string& zip,
    std::unordered_map<std::string, ZipEntryMeta>& index) {

    if (zip.size() < 22) return false;
    size_t search_start = zip.size() > (0x10000 + 22) ? (zip.size() - (0x10000 + 22)) : 0;
    size_t eocd = std::string::npos;
    for (size_t p = zip.size() - 22; p >= search_start; --p) {
        if (readLe32(zip, p) == 0x06054b50u) {
            eocd = p;
            break;
        }
        if (p == 0) break;
    }
    if (eocd == std::string::npos || eocd + 22 > zip.size()) return false;

    uint16_t total_entries = readLe16(zip, eocd + 10);
    uint32_t cd_size = readLe32(zip, eocd + 12);
    uint32_t cd_offset = readLe32(zip, eocd + 16);
    if (static_cast<size_t>(cd_offset) + cd_size > zip.size()) return false;

    size_t pos = cd_offset;
    for (uint16_t i = 0; i < total_entries; ++i) {
        if (pos + 46 > zip.size()) return false;
        if (readLe32(zip, pos) != 0x02014b50u) return false;
        uint16_t method = readLe16(zip, pos + 10);
        uint32_t comp_size = readLe32(zip, pos + 20);
        uint32_t uncomp_size = readLe32(zip, pos + 24);
        uint16_t name_len = readLe16(zip, pos + 28);
        uint16_t extra_len = readLe16(zip, pos + 30);
        uint16_t comment_len = readLe16(zip, pos + 32);
        uint32_t local_header_offset = readLe32(zip, pos + 42);
        size_t name_start = pos + 46;
        size_t entry_end = name_start + name_len + extra_len + comment_len;
        if (entry_end > zip.size()) return false;
        std::string name = zip.substr(name_start, name_len);
        index[name] = ZipEntryMeta{method, comp_size, uncomp_size, local_header_offset};
        pos = entry_end;
    }
    return true;
}

bool extractZipEntry(const std::string& zip, const std::string& name, std::string& out) {
    std::unordered_map<std::string, ZipEntryMeta> index;
    if (!buildZipIndex(zip, index)) return false;
    auto it = index.find(name);
    if (it == index.end()) return false;
    const auto& meta = it->second;

    size_t local = static_cast<size_t>(meta.local_header_offset);
    if (local + 30 > zip.size()) return false;
    if (readLe32(zip, local) != 0x04034b50u) return false;
    uint16_t name_len = readLe16(zip, local + 26);
    uint16_t extra_len = readLe16(zip, local + 28);
    size_t data_start = local + 30 + name_len + extra_len;
    size_t data_end = data_start + static_cast<size_t>(meta.comp_size);
    if (data_end > zip.size()) return false;
    std::string payload = zip.substr(data_start, static_cast<size_t>(meta.comp_size));

    if (meta.method == 0) {
        out = std::move(payload);
        return true;
    }
    if (meta.method == 8) {
        return inflateDeflateData(payload, meta.uncomp_size, out);
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

std::string getXmlAttr(const std::string& xml, const std::string& key) {
    std::string token = key + "=\"";
    auto pos = xml.find(token);
    if (pos == std::string::npos) return "";
    pos += token.size();
    auto end = xml.find('"', pos);
    if (end == std::string::npos) return "";
    return xml.substr(pos, end - pos);
}

std::string extractTagText(const std::string& xml, const std::string& tag) {
    auto open = xml.find("<" + tag);
    if (open == std::string::npos) return "";
    auto gt = xml.find('>', open);
    if (gt == std::string::npos) return "";
    auto close = xml.find("</" + tag + ">", gt + 1);
    if (close == std::string::npos) return "";
    return xmlUnescape(xml.substr(gt + 1, close - gt - 1));
}

int colFromCellRef(const std::string& ref) {
    int col = 0;
    for (char c : ref) {
        if (c >= 'A' && c <= 'Z') col = col * 26 + (c - 'A' + 1);
        else break;
    }
    return col - 1;
}

std::vector<std::string> parseSharedStrings(const std::string& xml) {
    std::vector<std::string> table;
    size_t pos = 0;
    while (true) {
        auto si_start = xml.find("<si", pos);
        if (si_start == std::string::npos) break;
        auto si_gt = xml.find('>', si_start);
        if (si_gt == std::string::npos) break;
        auto si_end = xml.find("</si>", si_gt + 1);
        if (si_end == std::string::npos) break;
        std::string si_xml = xml.substr(si_gt + 1, si_end - si_gt - 1);

        std::string value;
        size_t t_pos = 0;
        while (true) {
            auto t_start = si_xml.find("<t", t_pos);
            if (t_start == std::string::npos) break;
            auto t_gt = si_xml.find('>', t_start);
            if (t_gt == std::string::npos) break;
            auto t_end = si_xml.find("</t>", t_gt + 1);
            if (t_end == std::string::npos) break;
            value += xmlUnescape(si_xml.substr(t_gt + 1, t_end - t_gt - 1));
            t_pos = t_end + 4;
        }
        table.push_back(value);
        pos = si_end + 5;
    }
    return table;
}

std::string cellValue(
    const std::string& cell_xml,
    const std::vector<std::string>& shared_strings) {

    std::string type = getXmlAttr(cell_xml, "t");
    if (type == "inlineStr") return extractTagText(cell_xml, "t");
    if (type == "s") {
        std::string v = trim(extractTagText(cell_xml, "v"));
        if (v.empty()) return "";
        try {
            size_t idx = static_cast<size_t>(std::stoul(v));
            if (idx < shared_strings.size()) return shared_strings[idx];
        } catch (...) {
            return "";
        }
        return "";
    }
    std::string v = extractTagText(cell_xml, "v");
    if (!v.empty()) return v;
    return extractTagText(cell_xml, "t");
}

ImportParseResult parseXlsxBinary(const std::string& binary) {
    ImportParseResult result;
    if (binary.size() < 4 ||
        static_cast<unsigned char>(binary[0]) != 0x50 ||
        static_cast<unsigned char>(binary[1]) != 0x4B) {
        return failImport(
            "xlsx_invalid_zip",
            "不是有效的 Excel (.xlsx) 文件",
            "文件头不是 ZIP 格式（xlsx 实为压缩包）",
            "请确认扩展名为 .xlsx，且未选错 CSV/文本格式");
    }

    std::string sheet;
    if (!extractZipEntry(binary, "xl/worksheets/sheet1.xml", sheet)) {
        return failImport(
            "xlsx_sheet_missing",
            "无法读取 Excel 第一个工作表",
            "压缩包中缺少 xl/worksheets/sheet1.xml",
            "请使用本应用「导出」生成的 .xlsx，或将数据放在第一个工作表后重试");
    }
    std::string shared_xml;
    std::vector<std::string> shared_strings;
    if (extractZipEntry(binary, "xl/sharedStrings.xml", shared_xml)) {
        shared_strings = parseSharedStrings(shared_xml);
    }

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
            auto self_end = row_xml.find("/>", c_start);
            std::string cell;
            if (self_end != std::string::npos && (c_end == std::string::npos || self_end < c_end)) {
                cell = row_xml.substr(c_start, self_end - c_start + 2);
                cpos = self_end + 2;
            } else {
                if (c_end == std::string::npos) break;
                cell = row_xml.substr(c_start, c_end - c_start + 4);
                cpos = c_end + 4;
            }

            std::string ref = getXmlAttr(cell, "r");
            if (ref.empty()) continue;
            int col = colFromCellRef(ref);
            cells[col] = cellValue(cell, shared_strings);
        }

        std::vector<std::string> cols(6);
        for (int i = 0; i < 6; ++i) {
            auto it = cells.find(i);
            if (it != cells.end()) cols[static_cast<size_t>(i)] = it->second;
        }

        if (!header_checked) {
            if (!headerMatches(cols)) {
                return failImport(
                    "xlsx_header_mismatch",
                    "无法识别 Excel 表头",
                    describeHeaderMismatch(cols),
                    "请使用本应用导出的 .xlsx；第三方 Excel 另存后表头或工作表结构可能不兼容");
            }
            header_checked = true;
            continue;
        }

        if (std::all_of(cols.begin(), cols.end(), [](const std::string& s) { return trim(s).empty(); }))
            continue;

        Record r = recordFromFields(cols, row_num, result);
        if (!result.error.empty()) return result;
        result.records.push_back(std::move(r));
    }

    if (!header_checked) {
        int xlsx_rows = countXlsxRows(sheet);
        if (xlsx_rows == 0) {
            return failImport(
                "xlsx_empty_sheet",
                "Excel 第一个工作表为空",
                "sheet1 中未找到任何数据行",
                "请确认记账数据在第一个工作表；多工作表文件请只保留 sheet1 或改用 CSV 导入");
        }
        return failImport(
            "xlsx_unreadable",
            "Excel 内容无法解析",
            "共发现 " + std::to_string(xlsx_rows) + " 行，但未能识别表头",
            "请勿使用 WPS/Excel 多 sheet 仅写在 sheet2 的文件；建议用本应用导出或选 CSV 格式");
    }
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

    if (std::find(kFormats.begin(), kFormats.end(), fmt) == kFormats.end()) {
        return failImport(
            "unsupported_format",
            "不支持的导入格式",
            "format=" + format,
            "可选：txt、csv、tsv、json、xlsx");
    }

    if (content.empty()) {
        return failImport(
            "empty_content",
            "文件内容为空",
            "",
            "请选择非空文件，并确认导入格式与文件类型一致");
    }

    if (fmt == "csv") return parseCsvTsv(content, ',');
    if (fmt == "tsv") return parseCsvTsv(content, '\t');
    if (fmt == "json") return parseJsonContent(content);
    if (fmt == "txt") return parseTxtContent(content);

    std::string binary = content_is_base64 ? base64Decode(content) : content;
    if (binary.empty()) {
        return failImport(
            "xlsx_decode_failed",
            "Excel 文件内容无效",
            "Base64 解码后无数据或文件损坏",
            "请重新选择 .xlsx 文件，并确保导入格式选为 Excel (.xlsx)");
    }
    return parseXlsxBinary(binary);
}
