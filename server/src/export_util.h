#pragma once

#include "db.h"
#include <filesystem>
#include <string>
#include <vector>

struct ExportFileResult {
    bool ok = false;
    std::string error;
    std::filesystem::path filepath;
    std::string filename;
    int count = 0;
};

/** 将记录写入应用根目录；format: txt|csv|tsv|json|xlsx；conflict_strategy: cancel|replace|keep_both */
ExportFileResult exportRecordsToFile(
    const std::filesystem::path& root_dir,
    const std::string& filename_input,
    const std::string& format,
    const std::string& conflict_strategy,
    const std::vector<Record>& records);
