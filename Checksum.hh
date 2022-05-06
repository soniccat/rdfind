/*
   copyright 2006-2017 Paul Dreik (earlier Paul Sundvall)
   Distributed under GPL v 2.0 or later, at your option.
   See LICENSE for further details.
*/

#ifndef RDFIND_CHECKSUM_HH
#define RDFIND_CHECKSUM_HH

#include <cstddef>

#include <nettle/md5.h>
#include <nettle/sha.h>

/**
 * class for checksum calculation
 */
class Checksum
{
public:
  // these are the checksums that can be calculated
  enum checksumtypes
  {
    NOTSET = 0,
    MD5,
    SHA1,
    SHA256,
    AVERAGE_HASH,
    PHASH
  };

  explicit Checksum(checksumtypes type);

  int update(std::size_t length, const unsigned char* buffer);
  int update(std::size_t length, const char* buffer);

#if 0
  /// prints the checksum on stdout
  int print();
#endif
  // writes the checksum to buffer.
  // returns 0 if everything went ok.
  int printToBuffer(void* buffer, std::size_t N);

  // returns the number of bytes that the buffer needs to be
  // returns negative if something is wrong.
  [[gnu::pure]] int getDigestLength() const;

private:
  // to know what type of checksum we are doing
  const int m_checksumtype = NOTSET;
  // the checksum calculation internal state
  union ChecksumStruct
  {
    sha1_ctx sha1;
    sha256_ctx sha256;
    md5_ctx md5;
  } m_state;
};

#endif // RDFIND_CHECKSUM_HH
