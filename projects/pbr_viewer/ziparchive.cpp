//
// Created by crocdialer on 25.08.22.
//

#include <cstring>
#include <functional>
#include <fstream>

#include "ziparchive.h"

namespace vierkant
{

// maaaybe need those later
//zip_int64_t zip_source_callback(void *_Nullable, void *_Nullable, zip_uint64_t, zip_source_cmd_t);
//
//void zip_progress_callback(zip_t *archive, double, void *_Nullable);
//
//int zip_cancel_callback(zip_t *archive, void *_Nullable);

struct zipstreambuffer : public std::streambuf
{
    zipstreambuffer(zip_t *_archive, const std::filesystem::path &file_path) : archive(_archive)
    {
        zip_file = {zip_fopen(archive, file_path.c_str(), 0), zip_fclose};
    }

    std::streamsize xsgetn(char *s, std::streamsize n) override
    {
        if(zip_file){ return zip_fread(zip_file.get(), s, n); }
        return EOF;
    }

    zip_t *archive = nullptr;
    std::unique_ptr<struct zip_file, std::function<int(struct zip_file *)>> zip_file;
};

ziparchive::istream::istream(std::shared_ptr<zip_t> _archive, const std::filesystem::path &file_path) :
        std::istream(new zipstreambuffer(_archive.get(), file_path)),
        m_archive(std::move(_archive)){}

ziparchive::istream::~istream(){ delete rdbuf(); }

ziparchive::ziparchive(const std::filesystem::path &archive_path)
{
    int errorp;

    int flags = std::filesystem::exists(archive_path) ? 0 : ZIP_EXCL | ZIP_CREATE;
    m_archive = {zip_open(archive_path.c_str(), flags, &errorp), zip_close};

    if(!m_archive)
    {
        zip_error_t ziperror;
        zip_error_init_with_code(&ziperror, errorp);
        throw std::runtime_error(
                "Failed to open archive: " + archive_path.string() + ": " + zip_error_strerror(&ziperror));
    }
}

void ziparchive::add_file(const std::filesystem::path &file_path)
{
    std::unique_ptr<zip_source_t, std::function<void(zip_source_t *)>> source;
    source = {zip_source_file(m_archive.get(), file_path.c_str(), 0, -1), zip_source_close};
    zip_file_add(m_archive.get(), file_path.c_str(), source.get(), 0);
}

bool ziparchive::has_file(const std::filesystem::path &file_path) const
{
    return m_archive && zip_name_locate(m_archive.get(), file_path.c_str(), 0) != -1;
}

ziparchive::istream ziparchive::open_file(const std::filesystem::path &file_path) const
{
    return {m_archive, file_path};
}

}