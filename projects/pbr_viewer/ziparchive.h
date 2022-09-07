//
// Created by crocdialer on 25.08.22.
//

#pragma once

#include <filesystem>
#include <istream>
#include <zip.h>

namespace vierkant
{

class ziparchive
{
public:

    class istream : public std::istream
    {
    public:
        istream(std::shared_ptr<zip_t> _archive, const std::filesystem::path &file_path);
        ~istream() override;
    private:
        std::shared_ptr<zip_t> m_archive;
    };

    explicit ziparchive(const std::filesystem::path &archive_path);

    [[nodiscard]] bool has_file(const std::filesystem::path &file_path) const;
    void add_file(const std::filesystem::path &file_path);

    [[nodiscard]] ziparchive::istream open_file(const std::filesystem::path &file_path) const;

private:
    std::shared_ptr<zip_t> m_archive;
};

}
