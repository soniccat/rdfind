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
#include <opencv2/opencv.hpp>
#include <opencv2/img_hash.hpp>

#include "Fileinfo.hh" //file container
#include "Cluster.hh"
#include "Dirlist.hh"

using namespace std;

class Rdutil
{
public:
  explicit Rdutil(vector<Fileinfo>& list)
    : m_list(list)
  {}

  /**
   * print file names to a file, with extra information.
   * @return zero on success
   */
  int printtofile(const string& filename) const;

  /// mark files with a unique number
  void markitems();

  /**
   * sorts the list on device and inode. not guaranteed to be stable.
   */
  int sortOnDeviceAndInode();

  /**
   * sorts from the given index to the end on depth, then name.
   * this is useful to be independent of the filesystem order.
   */
  void sort_on_depth_and_name(size_t index_of_first);

  /**
   * for each group of identical inodes, only keep the one with the highest
   * rank.
   * @return number of elements removed
   */
  size_t removeIdenticalInodes();
    
  size_t removeNonImages();

  /**
   * Assumes the list is already sorted on size, and all elements with the same
   * size have the same buffer. Marks duplicates with tags, depending on their
   * nature. Shall be used when everything is done, and sorted.
   * For each sequence of duplicates, the original will be placed first but no
   * other guarantee on ordering is given.
   *
   */
  
  size_t removeInvalidImages();
  size_t removeInvalidImages(vector<Fileinfo>& files);

  /// removes all items from the list, that have the deleteflag set to true.
  size_t cleanup();
  
  void calcHashes();
  void calcHashes(vector<Fileinfo>& files);
  
  long readyToCleanup();
  
  void buildClusters();
  void sortClustersBySize();

  /**
   * gets the total size, in bytes.
   * m_duptype=Fileinfo::DUPTYPE_FIRST_OCCURRENCE
   */
  [[gnu::pure]] Fileinfo::filesizetype totalsizeinbytes() const;

  /**
   * outputs a nicely formatted string "45 bytes" or "3 Gibytes"
   * where 1024 is used as base
   */
  ostream& totalsize(ostream& out) const;

  /// outputs the saveable amount of space
  ostream& saveablespace(ostream& out) const;
  
  const vector<Cluster>& getClusters() const { return clusters; }
  const map<string, Cluster>& getPathClusters() const { return pathClusters; }
  size_t removeSingleClusters();
  size_t clusterFileCount();
  
  void buildPathClusters(const char* path, Dirlist& dirlist, Cache& cache);
  void calcClusterSortSuggestions();

private:
    vector<Fileinfo>& m_list;
    map<string, Cluster> pathClusters;
    vector<Cluster> clusters;
    
    map<Cluster, vector<pair<int, Cluster>>> clusterSortSuggestions;
};

#endif
