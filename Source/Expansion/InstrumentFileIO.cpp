#include "InstrumentFileIO.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#if defined(_WIN32)
 #include <windows.h>
 #include <sys/stat.h>
#else
 #include <dirent.h>
 #include <limits.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace sampler
{
namespace
{
constexpr std::uint64_t maximumInstrumentFileBytes = 16u * 1024u * 1024u;

bool hasJaszExtension(const std::string& file)
{
    const auto slash = file.find_last_of("/\\");
    const auto dot = file.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return false;
    auto extension = file.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return extension == ".jasz";
}

std::string filenameOf(const std::string& path)
{
    const auto separator = path.find_last_of("/\\");
    return separator == std::string::npos ? path : path.substr(separator + 1);
}

std::string joinPath(const std::string& directory, const std::string& name)
{
    if (directory.empty())
        return name;
    const char last = directory.back();
    if (last == '/' || last == '\\')
        return directory + name;
#if defined(_WIN32)
    return directory + "\\" + name;
#else
    return directory + "/" + name;
#endif
}

bool isRegularFile(const std::string& path)
{
#if defined(_WIN32)
    struct _stat64 info {};
    return _stat64(path.c_str(), &info) == 0 && (info.st_mode & _S_IFREG) != 0;
#else
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
#endif
}

bool isDirectory(const std::string& path)
{
#if defined(_WIN32)
    const auto attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

std::uint64_t fileSize(const std::string& path, bool& valid)
{
#if defined(_WIN32)
    struct _stat64 info {};
    valid = _stat64(path.c_str(), &info) == 0 && (info.st_mode & _S_IFREG) != 0;
#else
    struct stat info {};
    valid = stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
#endif
    return valid ? std::uint64_t(info.st_size) : 0;
}

std::string normalizedPath(const std::string& path)
{
#if defined(_WIN32)
    char buffer[MAX_PATH] {};
    const auto length = GetFullPathNameA(path.c_str(), MAX_PATH, buffer, nullptr);
    return length > 0 && length < MAX_PATH ? std::string(buffer, length) : path;
#else
    char buffer[PATH_MAX] {};
    return realpath(path.c_str(), buffer) ? std::string(buffer) : path;
#endif
}

void addMatch(std::vector<std::string>& matches, const std::string& path)
{
    const auto normalized = normalizedPath(path);
    if (std::find(matches.begin(), matches.end(), normalized) == matches.end())
        matches.push_back(normalized);
}

#if defined(_WIN32)
void findExactFilenameRecursive(const std::string& directory,
                                const std::string& filename,
                                std::vector<std::string>& matches,
                                unsigned depth)
{
    if (depth > 64)
        return;
    WIN32_FIND_DATAA data {};
    const auto pattern = joinPath(directory, "*");
    const auto handle = FindFirstFileA(pattern.c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE)
        return;
    do
    {
        const std::string name(data.cFileName);
        if (name == "." || name == ".." || (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            continue;
        const auto fullPath = joinPath(directory, name);
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            findExactFilenameRecursive(fullPath, filename, matches, depth + 1);
        else if (name == filename)
            addMatch(matches, fullPath);
    } while (FindNextFileA(handle, &data));
    FindClose(handle);
}
#else
void findExactFilenameRecursive(const std::string& directory,
                                const std::string& filename,
                                std::vector<std::string>& matches,
                                unsigned depth)
{
    if (depth > 64)
        return;
    DIR* handle = opendir(directory.c_str());
    if (!handle)
        return;

    while (const auto* entry = readdir(handle))
    {
        const std::string name(entry->d_name);
        if (name == "." || name == "..")
            continue;
        const auto fullPath = joinPath(directory, name);
        struct stat info {};
        if (lstat(fullPath.c_str(), &info) != 0 || S_ISLNK(info.st_mode))
            continue;
        if (S_ISDIR(info.st_mode))
            findExactFilenameRecursive(fullPath, filename, matches, depth + 1);
        else if (S_ISREG(info.st_mode) && name == filename)
            addMatch(matches, fullPath);
    }
    closedir(handle);
}
#endif

std::vector<std::string> findExactFilename(const std::string& filename,
                                           const std::vector<std::string>& roots)
{
    std::vector<std::string> matches;
    for (const auto& root : roots)
        if (isDirectory(root))
            findExactFilenameRecursive(root, filename, matches, 0);
    return matches;
}

bool replaceFileAtomically(const std::string& temporary, const std::string& destination,
                           std::string* error)
{
#if defined(_WIN32)
    if (!MoveFileExA(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        if (error)
            *error = "Could not atomically replace the instrument file.";
        std::remove(temporary.c_str());
        return false;
    }
#else
    if (std::rename(temporary.c_str(), destination.c_str()) != 0)
    {
        if (error)
            *error = "Could not atomically replace the instrument file.";
        std::remove(temporary.c_str());
        return false;
    }
#endif
    return true;
}
}

bool saveInstrumentFile(const InstrumentModel& model,
                        const std::string& filePath,
                        std::string* error)
{
    if (error)
        error->clear();
    const auto fail = [error](const char* message)
    {
        if (error)
            *error = message;
        return false;
    };

    if (!hasJaszExtension(filePath))
        return fail("Instrument files must use the .jasz extension.");
    if (model.size() == 0 || model.size() > ZoneSelection::capacity)
        return fail("A .jasz instrument must contain between 1 and 128 zones.");

    std::string serializationError;
    const auto bytes = model.serialize(&serializationError);
    if (bytes.empty())
    {
        if (error)
            *error = serializationError.empty() ? "Instrument contains no serializable zones." : serializationError;
        return false;
    }
    if (bytes.size() > maximumInstrumentFileBytes)
        return fail("Instrument file exceeds the 16 MB safety limit.");

    const auto temporary = filePath + ".tmp-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream)
            return fail("Could not create the temporary instrument file.");
        stream.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        stream.flush();
        if (!stream)
        {
            stream.close();
            std::remove(temporary.c_str());
            return fail("Failed while writing the instrument file.");
        }
    }

    if (!replaceFileAtomically(temporary, filePath, error))
        return false;
    if (error)
        error->clear();
    return true;
}

bool loadInstrumentFile(const std::string& filePath,
                        InstrumentModel& model,
                        std::string* error)
{
    if (error)
        error->clear();
    const auto fail = [error](const char* message)
    {
        if (error)
            *error = message;
        return false;
    };

    if (!hasJaszExtension(filePath))
        return fail("Instrument files must use the .jasz extension.");

    bool validFile = false;
    const auto size = fileSize(filePath, validFile);
    if (!validFile || size == 0)
        return fail("Instrument file is missing or empty.");
    if (size > maximumInstrumentFileBytes)
        return fail("Instrument file exceeds the 16 MB safety limit.");

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    std::ifstream stream(filePath, std::ios::binary);
    if (!stream)
        return fail("Could not open the instrument file.");
    stream.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!stream || std::size_t(stream.gcount()) != bytes.size())
        return fail("Instrument file could not be read completely.");

    InstrumentModel candidate;
    if (!candidate.deserialize(bytes, error))
        return false;
    if (candidate.size() == 0)
        return fail("Instrument file contains no sample zones.");
    model = std::move(candidate);
    if (error)
        error->clear();
    return true;
}

bool relinkMissingSamples(const InstrumentModel& source,
                          const std::vector<std::string>& searchRoots,
                          InstrumentModel& destination,
                          RelinkReport& report,
                          std::string* error)
{
    report = {};
    if (error)
        error->clear();
    if (source.size() == 0 || !source.validate(error))
        return false;

    InstrumentModel candidate = source;
    std::map<std::string, std::vector<std::string>> lookupCache;
    for (auto& zone : candidate.zones())
    {
        if (isRegularFile(zone.sample.path))
            continue;

        const auto originalPath = zone.sample.path;
        const auto filename = filenameOf(originalPath);
        if (filename.empty())
        {
            report.unresolved.push_back({ zone.sample.id, originalPath, {} });
            continue;
        }

        auto found = lookupCache.find(filename);
        if (found == lookupCache.end())
            found = lookupCache.emplace(filename, findExactFilename(filename, searchRoots)).first;

        if (found->second.size() == 1)
        {
            zone.sample.path = found->second.front();
            ++report.relinkedSamples;
        }
        else
        {
            report.unresolved.push_back({ zone.sample.id, originalPath, found->second });
        }
    }

    if (!report.unresolved.empty())
    {
        if (error)
            *error = "One or more sample references are missing or ambiguous; no changes were applied.";
        return false;
    }
    if (!candidate.validate(error))
        return false;
    destination = std::move(candidate);
    return true;
}
}
