/*
   copyright 2006-2018 Paul Dreik (earlier Paul Sundvall)
   Distributed under GPL v 2.0 or later, at your option.
   See LICENSE for further details.
*/

#ifndef Fileinfo_hh
#define Fileinfo_hh

#include <array>
#include <string>

// os specific headers
#include <sys/types.h> //for off_t and others.

#include <opencv2/opencv.hpp>
#include "Cache.hh"

using namespace std;
using namespace cv;

/**
 Holds information about a file.
 Keeping this small is probably beneficial for performance, because the
 large vector of all found files will be better cached.
 */
class Fileinfo
{
public:
  // constructor
  Fileinfo(string name, int cmdline_index, int depth, Cache* c)
    : m_info()
    , m_filename(move(name))
    , m_invalid_image(false)
    , m_cmdline_index(cmdline_index)
    , m_depth(depth)
    , m_identity(0)
    , m_cache(c)
  {
  }

  /// for storing file size in bytes, defined in sys/types.h
  using filesizetype = off_t;

  int64_t getidentity() const { return m_identity; }
  static int64_t identity(const Fileinfo& A) { return A.getidentity(); }
  void setidentity(int64_t id) { m_identity = id; }

  /**
   * reads info about the file, by querying the filesystem.
   * @return false if it was not possible to get the information.
   */
  bool readfileinfo();
  
  // sets the deleteflag
  void setInvalidImage(bool flag) { m_invalid_image = flag; }

  /// to get the deleteflag
  bool isInvalidImage() const { return m_invalid_image; }

  /// returns the file size in bytes
  filesizetype size() const { return m_info.stat_size; }

  // returns true if A has size zero
  bool isempty() const { return size() == 0; }

  /// filesize comparison
  bool is_smaller_than(Fileinfo::filesizetype minsize) const {
    return size() < minsize;
  }

  // returns the inode number
  unsigned long inode() const { return m_info.stat_ino; }

  // returns the device
  unsigned long device() const { return m_info.stat_dev; }

  // gets the filename
  const string& name() const { return m_filename; }

  // gets the command line index this item was found at
  int get_cmdline_index() const { return m_cmdline_index; }

  // gets the depth
  int depth() const { return m_depth; }

  /// returns true if file is a regular file. call readfileinfo first!
  bool isRegularFile() const { return m_info.is_file; }

  // returns true if file is a directory . call readfileinfo first!
  bool isDirectory() const { return m_info.is_directory; }
  bool isImage();
  void calcHashes();
  
  const Mat& getAHash() const { return aHash; }
  const Mat& getPHash() const { return pHash; }

private:
  // to store info about the file
  struct Fileinfostat
  {
    filesizetype stat_size; // size
    unsigned long stat_ino; // inode
    unsigned long stat_dev; // device
    bool is_file;
    bool is_directory;
    Fileinfostat();
  };
  Fileinfostat m_info;

  // to keep the name of the file, including path
  string m_filename;

  bool m_invalid_image;

  // If two files are found to be identical, the one with highest ranking is
  // chosen. The rules are listed in the man page.
  // lowest cmdlineindex wins, followed by the lowest depth, then first found.

  /**
   * in which order it appeared on the command line. can't be const, because
   * that means the implicitly defined assignment needed by the stl will be
   * illformed.
   * This is fine to be an int, because that is what argc,argv use.
   */
  int m_cmdline_index;

  /**
   * the directory depth at which this file was found.
   */
  int m_depth;

  /**
   * a number to identify this individual file. used for ranking.
   */
  int64_t m_identity;

  Cache* m_cache;
  
  Mat aHash;
  Mat pHash;
};

#endif
