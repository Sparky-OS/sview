// MIT license; see docs/license-mit.txt.
#include <StFile/StFileTransaction.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <limits>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/xattr.h>
extern "C" {
#include <libavutil/hash.h>
}

namespace {
    bool readAll(int theFd, std::vector<stUByte_t>& theData) {
        struct stat aStat;
        if(fstat(theFd, &aStat) || aStat.st_size < 0
        || uint64_t(aStat.st_size) > std::numeric_limits<size_t>::max()) { return false; }
        theData.resize(size_t(aStat.st_size));
        size_t aDone = 0;
        while(aDone < theData.size()) {
            const size_t aCount = stMin(theData.size() - aDone, size_t(1024 * 1024));
            const ssize_t aRead = pread(theFd, theData.data() + aDone, aCount, off_t(aDone));
            if(aRead < 0 && errno == EINTR) { continue; }
            if(aRead <= 0) { if(aRead == 0) errno = EIO; return false; }
            aDone += size_t(aRead);
        }
        return true;
    }
    bool writeAll(int theFd, const std::vector<stUByte_t>& theData) {
        size_t aDone = 0;
        while(aDone < theData.size()) {
            const size_t aCount = stMin(theData.size() - aDone, size_t(1024 * 1024));
            const ssize_t aWritten = pwrite(theFd, theData.data() + aDone, aCount, off_t(aDone));
            if(aWritten < 0 && errno == EINTR) { continue; }
            if(aWritten <= 0) { if(aWritten == 0) errno = EIO; return false; }
            aDone += size_t(aWritten);
        }
        return ftruncate(theFd, off_t(theData.size())) == 0 && fsync(theFd) == 0;
    }
    bool hashFd(int theFd, std::vector<stUByte_t>& theDigest) {
        AVHashContext* aHash = NULL;
        if(av_hash_alloc(&aHash, "sha256") < 0) { errno = ENOMEM; return false; }
        av_hash_init(aHash);
        std::vector<stUByte_t> aBuffer(1024 * 1024);
        struct stat aStat;
        bool isOk = fstat(theFd, &aStat) == 0 && S_ISREG(aStat.st_mode) && aStat.st_size >= 0;
        for(off_t aPos = 0; isOk && aPos < aStat.st_size;) {
            size_t aSize = size_t(stMin(off_t(aBuffer.size()), aStat.st_size - aPos));
            const ssize_t n = pread(theFd, aBuffer.data(), aSize, aPos);
            if(n < 0 && errno == EINTR) { continue; }
            if(n <= 0) { errno = EIO; isOk = false; break; }
            av_hash_update(aHash, aBuffer.data(), size_t(n));
            aPos += n;
        }
        theDigest.resize(32);
        if(isOk) { av_hash_final_bin(aHash, theDigest.data(), theDigest.size()); }
        av_hash_freep(&aHash);
        return isOk;
    }
    bool copyFile(int theSource, int theDestination, off_t theSize) {
        std::vector<stUByte_t> aBuffer(1024 * 1024);
        for(off_t aPos = 0; aPos < theSize;) {
            const ssize_t n = pread(theSource, aBuffer.data(), size_t(stMin(off_t(aBuffer.size()), theSize - aPos)), aPos);
            if(n < 0 && errno == EINTR) { continue; }
            if(n <= 0) { errno = EIO; return false; }
            for(ssize_t aDone = 0; aDone < n;) {
                const ssize_t w = pwrite(theDestination, aBuffer.data() + aDone, size_t(n - aDone), aPos + aDone);
                if(w < 0 && errno == EINTR) { continue; }
                if(w <= 0) { errno = EIO; return false; }
                aDone += w;
            }
            aPos += n;
        }
        return ftruncate(theDestination, theSize) == 0 && fsync(theDestination) == 0;
    }
    bool copyAttributes(int theSource, int theDestination) {
        const ssize_t aCount = flistxattr(theSource, NULL, 0);
        if(aCount < 0) { return errno == ENOTSUP; }
        std::vector<char> aNames(size_t(aCount), '\0');
        if(flistxattr(theSource, aNames.data(), aNames.size()) != aCount) { return false; }
        for(size_t anOffset = 0; anOffset < aNames.size();) {
            const char* aName = aNames.data() + anOffset;
            const size_t aLength = strnlen(aName, aNames.size() - anOffset);
            if(aLength == aNames.size() - anOffset) { errno = EIO; return false; }
            anOffset += aLength + 1;
            const ssize_t aSize = fgetxattr(theSource, aName, NULL, 0);
            if(aSize < 0) { return false; }
            std::vector<char> aValue(size_t(aSize), '\0');
            if(fgetxattr(theSource, aName, aValue.data(), aValue.size()) != aSize
            || fsetxattr(theDestination, aName, aValue.data(), aValue.size(), 0)) { return false; }
        }
        return true;
    }
}
struct StFileTransaction::Impl {
    StString Path, Temporary, Error;
    int Source = -1, Destination = -1;
    struct stat Stat;
    std::vector<stUByte_t> Bytes, OriginalHash, VerifiedHash;
    bool Sealed = false;
    bool Valid = false;
    bool fail() { Error = StString(strerror(errno ? errno : EIO)); Valid = false; return false; }
};
StFileTransaction::StFileTransaction(const StString& thePath, bool theToBuffer) : myImpl(new Impl) {
    Impl& a = *myImpl;
    a.Path = thePath;
    a.Source = open(thePath.toCString(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if(a.Source < 0 || fstat(a.Source, &a.Stat)) { a.fail(); return; }
    if(!S_ISREG(a.Stat.st_mode)) { errno = EINVAL; a.fail(); return; }
    if(!hashFd(a.Source, a.OriginalHash) || (theToBuffer && !readAll(a.Source, a.Bytes))) { a.fail(); return; }
    const char* aSlash = strrchr(thePath.toCString(), '/');
    std::string aFolder = aSlash ? std::string(thePath.toCString(), size_t(aSlash - thePath.toCString()) + 1) : "./";
    std::string aTemplate = aFolder + ".stereo-save-XXXXXX";
    std::vector<char> aName(aTemplate.begin(), aTemplate.end());
    aName.push_back(0);
    a.Destination = mkstemp(aName.data());
    if(a.Destination < 0) { a.fail(); return; }
    a.Temporary = StString(aName.data());
    fcntl(a.Destination, F_SETFD, FD_CLOEXEC);
    const bool isWritten = theToBuffer ? writeAll(a.Destination, a.Bytes) : copyFile(a.Source, a.Destination, a.Stat.st_size);
    std::vector<stUByte_t> aDigest;
    if(!isWritten || !hashFd(a.Destination, aDigest)) { a.fail(); return; }
    if(aDigest != a.OriginalHash) { errno = EIO; a.fail(); return; }
    a.Valid = true;
}
StFileTransaction::~StFileTransaction() {
    Impl& a = *myImpl;
    if(a.Destination >= 0) { close(a.Destination); }
    if(a.Source >= 0) { close(a.Source); }
    if(!a.Temporary.isEmpty()) { unlink(a.Temporary.toCString()); }
}
bool StFileTransaction::isValid() const { return myImpl->Valid; }
const std::vector<stUByte_t>& StFileTransaction::original() const { return myImpl->Bytes; }
const StString& StFileTransaction::temporaryPath() const { return myImpl->Temporary; }
const StString& StFileTransaction::error() const { return myImpl->Error; }
bool StFileTransaction::readBack(std::vector<stUByte_t>& theBytes) {
    Impl& a = *myImpl;
    // An external editor may replace the temporary inode rather than edit it in place.
    const int aFd = open(a.Temporary.toCString(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if(aFd < 0) { return a.fail(); }
    struct stat aStat;
    bool isOk = fstat(aFd, &aStat) == 0 && S_ISREG(aStat.st_mode) && readAll(aFd, theBytes);
    if(close(aFd)) { isOk = false; }
    return isOk || a.fail();
}
bool StFileTransaction::write(const std::vector<stUByte_t>& theBytes) {
    Impl& a = *myImpl;
    if(!a.Valid) { return false; }
    a.Sealed = false;
    std::vector<stUByte_t> aRead;
    if(!writeAll(a.Destination, theBytes) || !readBack(aRead)) { return a.fail(); }
    if(aRead != theBytes) { errno = EIO; return a.fail(); }
    return true;
}
bool StFileTransaction::seal() {
    Impl& a = *myImpl;
    if(!a.Valid) { return false; }
    const int aFd = open(a.Temporary.toCString(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if(aFd < 0) { return a.fail(); }
    const bool isOk = hashFd(aFd, a.VerifiedHash);
    const bool isClosed = close(aFd) == 0;
    if(!isOk || !isClosed) { return a.fail(); }
    a.Sealed = true;
    return true;
}
bool StFileTransaction::commit(const std::vector<stUByte_t>& theVerifiedBytes) {
    Impl& a = *myImpl;
    AVHashContext* aHash = NULL;
    if(av_hash_alloc(&aHash, "sha256") < 0) { errno = ENOMEM; return a.fail(); }
    av_hash_init(aHash);
    av_hash_update(aHash, theVerifiedBytes.data(), theVerifiedBytes.size());
    a.VerifiedHash.resize(32);
    av_hash_final_bin(aHash, a.VerifiedHash.data(), a.VerifiedHash.size());
    av_hash_freep(&aHash);
    a.Sealed = true;
    return commit();
}
bool StFileTransaction::commit() {
    Impl& a = *myImpl;
    if(!a.Valid || !a.Sealed) { errno = EINVAL; return a.fail(); }
    const int aFd = open(a.Temporary.toCString(), O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if(aFd < 0) { return a.fail(); }
    struct stat aTemp;
    std::vector<stUByte_t> aRead;
    bool isOk = fstat(aFd, &aTemp) == 0 && S_ISREG(aTemp.st_mode)
             && fchown(aFd, a.Stat.st_uid, a.Stat.st_gid) == 0 && fchmod(aFd, a.Stat.st_mode & 07777) == 0
             && copyAttributes(a.Source, aFd) && fsync(aFd) == 0 && hashFd(aFd, aRead);
    if(isOk && aRead != a.VerifiedHash) { errno = EIO; isOk = false; }
    if(close(aFd)) { isOk = false; }
    if(!isOk) { return a.fail(); }
    struct stat aCurrent;
    if(lstat(a.Path.toCString(), &aCurrent) || aCurrent.st_dev != a.Stat.st_dev || aCurrent.st_ino != a.Stat.st_ino
    || aCurrent.st_size != a.Stat.st_size || aCurrent.st_mtim.tv_sec != a.Stat.st_mtim.tv_sec
    || aCurrent.st_mtim.tv_nsec != a.Stat.st_mtim.tv_nsec || aCurrent.st_ctim.tv_sec != a.Stat.st_ctim.tv_sec
    || aCurrent.st_ctim.tv_nsec != a.Stat.st_ctim.tv_nsec || !hashFd(a.Source, aRead) || aRead != a.OriginalHash) {
        errno = EBUSY; return a.fail();
    }
    if(close(a.Destination)) { a.Destination = -1; return a.fail(); }
    a.Destination = -1;
    if(rename(a.Temporary.toCString(), a.Path.toCString())) { return a.fail(); }
    a.Temporary.clear();
    a.Valid = false;
    return true;
}
