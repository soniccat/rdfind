/*
   copyright 2006-2017 Paul Dreik (earlier Paul Sundvall)
   Distributed under GPL v 2.0 or later, at your option.
   See LICENSE for further details.

   this file contains functions and templates that implement most of the
   functionality in rdfind.
 */
#ifndef rdutil_hh
#define rdutil_hh

#include <vector>

#include "Fileinfo.hh" //file container

#include <opencv2/opencv.hpp>
#include <opencv2/img_hash.hpp>

struct PhashDistance {
    Fileinfo f1;
    Fileinfo f2;
    double distance;
};


struct Cluster {
  std::vector<Fileinfo> files;
  cv::Ptr<cv::img_hash::ImgHashBase> aHashPtr;
  cv::Ptr<cv::img_hash::ImgHashBase> pHashPtr;
  double distance = 0.0;
  
public:
    Cluster(
    std::vector<Fileinfo> files,
    cv::Ptr<cv::img_hash::ImgHashBase> aHashPtr,
    cv::Ptr<cv::img_hash::ImgHashBase> pHashPtr,
    double d
    )
        : files(files)
        , aHashPtr(aHashPtr)
        , pHashPtr(pHashPtr)
        , distance(d)
    {}

  bool needAdd(Fileinfo& f, double& outDistance) {
    double resultDistance = 0.0;
    /*std::for_each(files.begin(), files.end(), [this, &dinstance, &f](const Fileinfo& clusterFile)*/
    for (auto& clusterFile : files) {
      auto aDistance = aHashPtr->compare(f.getAHash(), clusterFile.getAHash());
      auto pDistance = pHashPtr->compare(f.getPHash(), clusterFile.getPHash());
      auto d = std::fmax(aDistance, pDistance);
      resultDistance = std::fmax(resultDistance, d);
    }

    outDistance = resultDistance;
    return resultDistance <= 3.0;
  }
  
  void add(Fileinfo& f) {
    files.push_back(f);
  }
  
  std::vector<Fileinfo>& getFiles() {
    return files;
  }
  
  std::vector<Fileinfo> filesSortedBySize() const {
    std::vector<Fileinfo> sorted = files;
    std::sort(sorted.begin(), sorted.end(), [](const Fileinfo& f1, const Fileinfo& f2) {
      return f2.size() < f1.size();
    });
    
//    std::partial_sort_copy(files.begin(), files.end(), sorted.begin(), sorted.end(), [](const Fileinfo& f1, const Fileinfo& f2) {
//      return f2.size() < f1.size();
//    });
    return sorted;
  }
  
  bool isSingle() const {
    return files.size() == 1;
  }
  
  size_t size() const {
    return files.size();
  }
  
  Fileinfo::filesizetype fileSize() const {
    Fileinfo::filesizetype size = 0;
    for (auto& f : files) {
      size += f.size();
    }
    
    return size;
  }
  
  Fileinfo::filesizetype fileSizeWithoutBiggest() const {
    Fileinfo::filesizetype size = 0;
    Fileinfo::filesizetype biggestSize = 0;
    for (auto& f : files) {
      biggestSize = std::fmax(biggestSize, f.size());
      size += f.size();
    }
    
    return size - biggestSize;
  }
  
  void setDistance(double d) {
    distance = d;
  }
  
  double getDistance() const {
    return distance;
  }
};

class Rdutil
{
public:
  explicit Rdutil(std::vector<Fileinfo>& list)
    : m_list(list)
  {}

  /**
   * print file names to a file, with extra information.
   * @param filename
   * @return zero on success
   */
  int printtofile(const std::string& filename) const;

  /// mark files with a unique number
  void markitems();

  /**
   * sorts the list on device and inode. not guaranteed to be stable.
   * @return
   */
  int sortOnDeviceAndInode();

  /**
   * sorts from the given index to the end on depth, then name.
   * this is useful to be independent of the filesystem order.
   */
  void sort_on_depth_and_name(std::size_t index_of_first);
  
  void sort_by_size_reversed();

  /**
   * for each group of identical inodes, only keep the one with the highest
   * rank.
   * @return number of elements removed
   */
  std::size_t removeIdenticalInodes();

  /**
   * remove files with unique size from the list.
   * @return
   */
  std::size_t removeUniqueSizes();

  std::size_t removeUniqueNames();
    
    std::size_t removeNonImages();

  /**
   * remove files with unique combination of size and buffer from the list.
   * @return
   */
  std::size_t removeUniqSizeAndBuffer();

  void markImagesWithUniqueBuffer(bool skipTrueDeleteFlag);

  /**
   * Assumes the list is already sorted on size, and all elements with the same
   * size have the same buffer. Marks duplicates with tags, depending on their
   * nature. Shall be used when everything is done, and sorted.
   * For each sequence of duplicates, the original will be placed first but no
   * other guarantee on ordering is given.
   *
   */
  void markduplicates();
  
  std::size_t removeInvalidImages();

  /// removes all items from the list, that have the deleteflag set to true.
  std::size_t cleanup();
  
  void calcHashes();
  
  long readyToCleanup();
  
  void buildClusters();
  void sortClustersBySize();

  /**
   * Removes items with file size less than minsize
   * @return the number of removed elements.
   */
  std::size_t remove_small_files(Fileinfo::filesizetype minsize);

  // read some bytes. note! destroys the order of the list.
  // if lasttype is supplied, it does not reread files if they are shorter
  // than the file length. (unnecessary!). if -1, feature is turned off.
  // and file is read anyway.
  // if there is trouble with too much disk reading, sleeping for nsecsleep
  // nanoseconds can be made between each file.
  int fillwithbytes(enum Fileinfo::readtobuffermode type,
                    enum Fileinfo::readtobuffermode lasttype =
                      Fileinfo::readtobuffermode::NOT_DEFINED,
                    long nsecsleep = 0);

  /// make symlinks of duplicates.
  std::size_t makesymlinks(bool dryrun) const;

  /// make hardlinks of duplicates.
  std::size_t makehardlinks(bool dryrun) const;

  /// delete duplicates from file system.
  std::size_t deleteduplicates(bool dryrun) const;
    
  void verifyByPhash();
  size_t phashDistanceCount();

  /**
   * gets the total size, in bytes.
   * @param opmode 0 just add everything, 1 only elements with
   * m_duptype=Fileinfo::DUPTYPE_FIRST_OCCURRENCE
   * @return
   */
  [[gnu::pure]] Fileinfo::filesizetype totalsizeinbytes(int opmode = 0) const;

  /**
   * outputs a nicely formatted string "45 bytes" or "3 Gibytes"
   * where 1024 is used as base
   * @param out
   * @param opmode
   * @return
   */
  std::ostream& totalsize(std::ostream& out, int opmode = 0) const;

  /// outputs the saveable amount of space
  std::ostream& saveablespace(std::ostream& out) const;
  
  std::vector<Cluster>& getClusters() { return clusters; }
  size_t removeSingleClusters();
  size_t clusterFileCount();

private:
    std::vector<Fileinfo>& m_list;
    std::vector<PhashDistance> phashDistance;
    std::vector<Cluster> clusters;
};

#endif
