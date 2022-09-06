//
// Created by crocdialer on 25.08.22.
//

#include <cstring>
#include <functional>
#include <fstream>

#include "zipstream.h"

namespace vierkant
{

zip_int64_t zip_source_callback(void *_Nullable, void *_Nullable, zip_uint64_t, zip_source_cmd_t);

void zip_progress_callback(zip_t *archive, double, void *_Nullable);

int zip_cancel_callback(zip_t *archive, void *_Nullable);

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

void zip_progress_callback(zip_t *archive, double progress, void *user_data)
{

}

int zip_cancel_callback(zip_t *archive, void *user_data)
{
    return 0;
}

zipstream::zipstream(const std::filesystem::path &archive_path,
                     const std::filesystem::path &file_path)
{
    int errorp;

    int flags = std::filesystem::exists(archive_path) ? 0: ZIP_EXCL | ZIP_CREATE;
    m_archive = zip_open(archive_path.c_str(), flags, &errorp);

    if(!m_archive)
    {
        zip_error_t ziperror;
        zip_error_init_with_code(&ziperror, errorp);
        throw std::runtime_error(
                "Failed to open archive: " + archive_path.string() + ": " + zip_error_strerror(&ziperror));
    }

    delete _M_streambuf;
    _M_streambuf = new zipstreambuffer(m_archive, file_path);
}

zipstream::~zipstream()
{
    delete _M_streambuf;
    zip_close(m_archive);
}

void zipstream::add_file(const std::filesystem::path &file_path)
{
    std::unique_ptr<zip_source_t, std::function<void(zip_source_t *)>> source;
    source = {zip_source_file(m_archive, file_path.c_str(), 0, -1), zip_source_close};
    zip_file_add(m_archive, file_path.c_str(), source.get(), 0);
}

bool zipstream::has_file(const std::filesystem::path &file_path) const
{
    return m_archive && zip_name_locate(m_archive, file_path.c_str(), 0) != -1;
}

//zip_int64_t zip_source_callback(void *user_data, void *data, zip_uint64_t size, zip_source_cmd_t cmd)
//{
////    auto &zipstreambuffer = *reinterpret_cast<struct zipstreambuffer *>(user_data);
//
//    switch(cmd)
//    {
//        case ZIP_SOURCE_SUPPORTS:return ZIP_SOURCE_SUPPORTS_READABLE | ZIP_SOURCE_SUPPORTS_WRITABLE;
//
//        case ZIP_SOURCE_OPEN:break;
//
//        case ZIP_SOURCE_READ:break;
//
//        case ZIP_SOURCE_BEGIN_WRITE:
//        case ZIP_SOURCE_WRITE:
//        case ZIP_SOURCE_COMMIT_WRITE:break;
//
//        case ZIP_SOURCE_CLOSE:break;
//
//        default:break;
//    }
//
//    return 0;
//}

}