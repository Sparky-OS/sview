// A verified temporary replacement of an explicitly chosen local file.
// MIT license; see docs/license-mit.txt.
#ifndef __StFileTransaction_h_
#define __StFileTransaction_h_
#include <StStrings/StString.h>
#include <vector>
#include <memory>

class StFileTransaction {
public:
    ST_CPPEXPORT explicit StFileTransaction(const StString& thePath, bool theToBuffer = true);
    ST_CPPEXPORT ~StFileTransaction();
    ST_CPPEXPORT bool isValid() const;
    ST_CPPEXPORT const std::vector<stUByte_t>& original() const;
    ST_CPPEXPORT const StString& temporaryPath() const;
    ST_CPPEXPORT const StString& error() const;
    ST_CPPEXPORT bool write(const std::vector<stUByte_t>& theBytes);
    ST_CPPEXPORT bool readBack(std::vector<stUByte_t>& theBytes);
    // Streaming read-back for large container files; commit rechecks the sealed bytes.
    ST_CPPEXPORT bool seal();
    ST_CPPEXPORT bool commit();
    // Call only after the format reader verified the chosen layout and eyes.
    ST_CPPEXPORT bool commit(const std::vector<stUByte_t>& theVerifiedBytes);
private:
    StFileTransaction(const StFileTransaction&) = delete;
    StFileTransaction& operator=(const StFileTransaction&) = delete;
    struct Impl;
    std::unique_ptr<Impl> myImpl;
};
#endif
