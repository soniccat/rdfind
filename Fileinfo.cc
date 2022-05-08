/*
   copyright 2006-2017 Paul Dreik (earlier Paul Sundvall)
   Distributed under GPL v 2.0 or later, at your option.
   See LICENSE for further details.
*/

#include "config.h"

// std
#include <cassert>
#include <cerrno>   //for errno
#include <cstring>  //for strerror
#include <fstream>  //for file reading
#include <iostream> //for cout etc

// os
#include <sys/stat.h> //for file info
#include <unistd.h>   //for unlink etc.

#include <opencv2/opencv.hpp>
#include <opencv2/img_hash.hpp>

// project
#include "Fileinfo.hh"

using namespace std;
using namespace cv;
using namespace img_hash;

static bool endsWith(string_view str, string_view suffix)
{
    return str.size() >= suffix.size() && 0 == str.compare(str.size()-suffix.size(), suffix.size(), suffix);
}

bool
Fileinfo::isImage() {
  return endsWith(m_filename, string_view(".jpg")) ||
  endsWith(m_filename, string_view(".jpeg")) ||
  endsWith(m_filename, string_view(".png"));
}

void Fileinfo::calcHashes() {
  Mat img;
  Mat imgAHash;
  m_cache->getAverageHash(name(), imgAHash);
  
  if (m_cache->isInvalidImage(name())) {
    setInvalidImage(true);
    return;
  }

  if (imgAHash.empty()) {
    img = imread(m_filename.c_str());
    if (!img.empty()) {
     auto hashPtr = AverageHash::create();
     hashPtr->compute(img, imgAHash);
     m_cache->putAverageHash(name(), imgAHash);
    } else {
     setInvalidImage(true);
     m_cache->putIsInvalidImage(name(), true);
    }
  }
   
  Mat imgPHash;
  if (!isInvalidImage()) {
    m_cache->getPHash(name(), imgPHash);

    if (imgPHash.empty()) {
      if (img.empty()) {
        img = imread(m_filename.c_str());
      }

      if (!img.empty()) {
        auto phashPtr = PHash::create();
        phashPtr->compute(img, imgPHash);
        m_cache->putPHash(name(), imgPHash);
      }
    }
  }
  
  aHash = imgAHash;
  pHash = imgPHash;
}

bool
Fileinfo::readfileinfo()
{
  struct stat info;
  m_info.is_file = false;
  m_info.is_directory = false;

  int res;
  do {
    res = stat(m_filename.c_str(), &info);
  } while (res < 0 && errno == EINTR);

  if (res < 0) {
    m_info.stat_size = 0;
    m_info.stat_ino = 0;
    m_info.stat_dev = 0;
    cerr << "readfileinfo.cc:Something went wrong when reading file "
                 "info from \""
              << m_filename << "\" :" << strerror(errno) << endl;
    return false;
  }

  // only keep the relevant information
  m_info.stat_size = info.st_size;
  m_info.stat_ino = info.st_ino;
  m_info.stat_dev = info.st_dev;

  m_info.is_file = S_ISREG(info.st_mode);
  m_info.is_directory = S_ISDIR(info.st_mode);
  return true;
}

// constructor
Fileinfo::Fileinfostat::Fileinfostat()
{
  stat_size = 99999;
  stat_ino = 99999;
  stat_dev = 99999;
  is_file = false;
  is_directory = false;
}

namespace {
int
simplifyPath(string& path)
{
  // replace a/./b with a/b
  do {
    const auto pos = path.find("/./");
    if (pos == string::npos) {
      break;
    }
    path.replace(pos, 3, "/");
  } while (true);

  // replace repeated slashes
  do {
    const auto pos = path.find("//");
    if (pos == string::npos) {
      break;
    }
    path.replace(pos, 2, "/");
  } while (true);

  // getting rid of /../ is difficult to get correct because of symlinks.
  // do not do it.
  return 0;
}

// prepares target, so that location can point to target in
// the best way possible
int
makeAbsolute(string& target)
{
  // if target is not absolute, let us make it absolute
  if (target.length() > 0 && target.at(0) == '/') {
    // absolute. do nothing.
  } else {
    // not absolute. make it absolute.

    // yes, this is possible to do with dynamically allocated memory,
    // but it is not portable then (and more work).
    // hmm, would it be possible to cache this and gain some speedup?
    const size_t buflength = 256;
    char buf[buflength];
    if (buf != getcwd(buf, buflength)) {
      cerr << "failed to get current working directory" << endl;
      return -1;
    }
    target = string(buf) + string("/") + target;
  }
  return 0;
}

} // namespace
