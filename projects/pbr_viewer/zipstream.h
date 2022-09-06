//
// Created by crocdialer on 25.08.22.
//

#pragma once

#include <zip.h>

#include <filesystem>
#include <array>
#include <streambuf>
#include <iostream>

namespace vierkant
{

class zipstream : public std::iostream
{
public:
    zipstream(const std::filesystem::path &archive_path, const std::filesystem::path &file_path);

    ~zipstream() override;

    bool has_file(const std::filesystem::path &file_path) const;
    void add_file(const std::filesystem::path &file_path);

private:
    zip_t *m_archive = nullptr;
};

}
