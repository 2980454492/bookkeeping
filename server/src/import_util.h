// import_util.h — 导入工具接口
//
// 职责：解析导入内容（csv/tsv/json/txt/xlsx）为 Record 列表。
// 实现见 import_util.cpp；handlers.cpp 通过 parseImportedRecords() 调用。

#pragma once

#include "db.h"
#include <string>
#include <vector>

struct ImportParseResult {
    bool ok = false;
    std::string error;
    std::string error_code;
    std::string detail;
    std::string hint;
    int row = 0;
    std::vector<Record> records;
};

/** 解析导入内容；format: txt|csv|tsv|json|xlsx；xlsx 时 content 为 base64 解码前的原始字节串 */
ImportParseResult parseImportedRecords(
    const std::string& format,
    const std::string& content,
    bool content_is_base64 = false);
