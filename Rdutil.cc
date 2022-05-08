/*
   copyright 2006-2017 Paul Dreik (earlier Paul Sundvall)
   Distributed under GPL v 2.0 or later, at your option.
   See LICENSE for further details.
*/

// pick up project config incl. assert control.
#include "config.h"

// std
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <fstream>  //for file writing
#include <iostream> //for cerr
#include <ostream>  //for output
#include <string>   //for easier passing of string arguments
#include <thread>   //sleep
#include <future>

// project
#include "Fileinfo.hh" //file container
#include "RdfindDebug.hh"

// class declaration
#include "Rdutil.hh"
#include "Tools.hh"

using namespace std;
using namespace cv;
using namespace cv::img_hash;

void Rdutil::sortClustersBySize() {
  sort(clusters.begin(), clusters.end(), [](const Cluster& c1, const Cluster& c2) {
      auto c2s = c2.size();
      auto c1s = c1.size();
      return ((c2s < c1s) || (c2s == c1s && c2.distance < c1.distance));
    }
  );
}

int Rdutil::printtofile(const string& filename) const
{
  // open a file to print to
  ofstream f1;
  f1.open(filename.c_str(), ios_base::out);
  if (!f1.is_open()) {
    cerr << "could not open file \"" << filename << "\"\n";
    return -1;
  }

  // exchange f1 for cout to write to terminal instead of file
  ostream& output(f1);

  for (auto& c : clusters) {
    output << "# Section (size:" << c.size() << ", distance:" << c.getDistance() << ')' << '\n';
    int n = 0;
    for (auto& f : c.filesSortedBySize()) {
      output << n << ":" << f.size() << ' ' << f.name() << '\n';
      ++n;
    }
  }

  f1.close();
  return 0;
}

// mark files with a unique number
void Rdutil::markitems()
{
  int64_t fileno = 1;
  for (auto& file : m_list) {
    file.setidentity(fileno++);
  }
}

namespace {

  bool cmpDeviceInode(const Fileinfo& a, const Fileinfo& b) {
    return make_tuple(a.device(), a.inode()) <
           make_tuple(b.device(), b.inode());
  }

  // compares rank as described in RANKING on man page.
  bool cmpRank(const Fileinfo& a, const Fileinfo& b) {
    return make_tuple(a.get_cmdline_index(), a.depth(), a.getidentity()) <
           make_tuple(b.get_cmdline_index(), b.depth(), b.getidentity());
  }

  bool cmpDepthName(const Fileinfo& a, const Fileinfo& b) {
    // inefficient, make it a reference.
    return make_tuple(a.depth(), a.name()) <
           make_tuple(b.depth(), b.name());
  }

  // compares file size
  bool cmpSize(const Fileinfo& a, const Fileinfo& b) {
    return a.size() < b.size();
  }

  // compares file size
  bool cmpSizeReversed(const Fileinfo& a, const Fileinfo& b) {
    return b.size() < a.size();
  }

  /**
   * goes through first to last, finds ranges of equal elements (determined by
   * cmp) and invokes callback on each subrange.
   * @param callback invoked as callback(subrangefirst,subrangelast)
   */
  template<class Iterator, class Cmp, class Callback>
  void apply_on_range(Iterator first, Iterator last, Cmp cmp, Callback callback) {
    assert(is_sorted(first, last, cmp));

    while (first != last) {
      auto sublast = first + 1;
      while (sublast != last && !cmp(*first, *sublast)) {
        ++sublast;
      }
      // a duplicate range with respect to cmp
      callback(first, sublast);

      // keep searching.
      first = sublast;
    }
  }
} // namespace

int Rdutil::sortOnDeviceAndInode() {
  sort(m_list.begin(), m_list.end(), cmpDeviceInode);
  return 0;
}

void Rdutil::sort_on_depth_and_name(size_t index_of_first) {
  assert(index_of_first <= m_list.size());

  auto it = begin(m_list) + static_cast<ptrdiff_t>(index_of_first);
  sort(it, end(m_list), cmpDepthName);
}

size_t Rdutil::removeIdenticalInodes() {
  auto initialSize = m_list.size();

  // sort list on device and inode.
  auto cmp = cmpDeviceInode;
  sort(m_list.begin(), m_list.end(), cmp);
  
  auto filesToRemove = set<int64_t>();

  // loop over ranges of adjacent elements
  using Iterator = decltype(m_list.begin());
  apply_on_range(
    m_list.begin(), m_list.end(), cmp, [&filesToRemove](Iterator first, Iterator last) {
      // let the highest-ranking element not be deleted. do this in order, to be
      // cache friendly.
      auto best = min_element(first, last, cmpRank);
      for_each(first, best, [&filesToRemove](Fileinfo& f) mutable {
        filesToRemove.insert(f.getidentity());
      });
      
      filesToRemove.erase(best->getidentity());
      
      for_each(best + 1, last, [&filesToRemove](Fileinfo& f) mutable {
        filesToRemove.insert(f.getidentity());
      });
    });

  auto it = std::remove_if(
    m_list.begin(), m_list.end(), [&filesToRemove](Fileinfo& f) {
        return filesToRemove.find(f.getidentity()) != filesToRemove.end();
    }
  );
    
  m_list.erase(it, m_list.end());
    
  return initialSize - m_list.size();
}

size_t Rdutil::removeNonImages() {
    auto initialSize = m_list.size();
    auto it = std::remove_if(
        m_list.begin(), m_list.end(), [](Fileinfo& f) {
            return !f.isImage();
        }
    );
    
    m_list.erase(it, m_list.end());
    return initialSize - m_list.size();
}

int64_t minInt64(int64_t l, int64_t r) {
    if (l < r) {
        return l;
    } else {
        return r;
    }
}

int64_t maxInt64(int64_t l, int64_t r) {
    if (l > r) {
        return l;
    } else {
        return r;
    }
}

size_t Rdutil::removeInvalidImages() {
  return removeInvalidImages(m_list);
}

size_t Rdutil::removeInvalidImages(vector<Fileinfo>& files) {
  const auto size_before = files.size();
  auto it = remove_if(files.begin(), files.end(), [](const Fileinfo& A) {
    return A.isInvalidImage();
  });

  files.erase(it, files.end());

  const auto size_after = files.size();

  return size_before - size_after;
}

Fileinfo::filesizetype Rdutil::totalsizeinbytes() const
{
  Fileinfo::filesizetype totalsize = 0;
  for (const auto& elem : m_list) {
    totalsize += elem.size();
  }

  return totalsize;
}

namespace littlehelper {
  // helper to make "size" into a more readable form.
  int calcrange(Fileinfo::filesizetype& size) {
    int range = 0;
    Fileinfo::filesizetype tmp = 0;
    while (size > 1024) {
      tmp = size >> 9;
      size = (tmp >> 1);
      ++range;
    }

    // round up if necessary
    if (tmp & 0x1) {
      ++size;
    }

    return range;
  }

  // source of capitalization rules etc:
  // https://en.wikipedia.org/wiki/Binary_prefix
  string byteprefix(int range) {
    switch (range) {
      case 0:
        return "B";
      case 1:
        return "KiB";
      case 2:
        return "MiB";
      case 3:
        return "GiB";
      case 4:
        return "TiB"; // Tebibyte
      case 5:
        return "PiB"; // Pebibyte
      case 6:
        return "EiB"; // Exbibyte
      default:
        return "!way too much!";
    }
  }
} // namespace littlehelper

ostream& Rdutil::totalsize(ostream& out) const {
  auto size = totalsizeinbytes();
  const int range = littlehelper::calcrange(size);
  out << size << " " << littlehelper::byteprefix(range);
  return out;
}

ostream& Rdutil::saveablespace(ostream& out) const {
  Fileinfo::filesizetype size = 0;
  for (auto& c : clusters) {
    size += c.fileSizeWithoutBiggest();
  }
  
  int range = littlehelper::calcrange(size);
  out << size << " " << littlehelper::byteprefix(range);
  return out;
}

class CalcHashesThread {
    vector<Fileinfo>::iterator begin;
    vector<Fileinfo>::iterator end;

public:
    CalcHashesThread(
      vector<Fileinfo>::iterator b,
      vector<Fileinfo>::iterator e
    ) {
        begin = b;
        end = e;
    }
    
    void operator()(){
        for_each(begin, end, [this](Fileinfo& f) {
            f.calcHashes();
        });
    }
};

void Rdutil::calcHashes() {
  calcHashes(m_list);
}

void Rdutil::calcHashes(vector<Fileinfo>& files) {
  auto threads = runInParallel(
    files,
    [](vector<Fileinfo>::iterator begin, vector<Fileinfo>::iterator end) {
      return CalcHashesThread(
         begin,
         end
      );
    }
  );

  for_each(threads.begin(), threads.end(), mem_fn(&thread::join));
}

void Rdutil::buildClusters() {
//  for_each(m_list.begin(), m_list.end(), [this](const Fileinfo& f) {
//
//
//
//  });

  Ptr<ImgHashBase> aHashPtr = AverageHash::create();
  Ptr<ImgHashBase> pHashPtr = PHash::create();

  for (auto& lf : m_list) {
    double distance = 0.0;
    auto clusterToAddIn = find_if(clusters.begin(), clusters.end(), [&lf, &distance](Cluster& c) mutable {
      return c.needAdd(lf, distance);
    });
    
    if (clusterToAddIn != clusters.end()) {
      clusterToAddIn->setDistance(distance);
      clusterToAddIn->add(lf);
    } else {
      clusters.emplace_back(
        "",
        vector<Fileinfo>({lf}),
        aHashPtr,
        pHashPtr,
        0.0
      );
    }
  }
}

size_t Rdutil::removeSingleClusters() {
  auto size = clusters.size();
  auto it = remove_if(clusters.begin(), clusters.end(), [](const Cluster& c) {
    return c.isSingle();
  });

  clusters.erase(it, clusters.end());
  return size - clusters.size();
}

size_t Rdutil::clusterFileCount() {
  size_t count = 0;
  for (auto& c : clusters) {
    count += c.size();
  }
  
  return count;
}

void Rdutil::buildPathClusters(const char* path, Dirlist& dirlist, Cache& cache) {
  Ptr<ImgHashBase> aHashPtr = AverageHash::create();
  Ptr<ImgHashBase> pHashPtr = PHash::create();
  vector<Fileinfo> files;

  dirlist.setcallbackfcn([this, &aHashPtr, &pHashPtr, &cache, &files](const string& path, const string& name, int depth) {
    string expandedname = path.empty() ? name : (path + "/" + name);
    Fileinfo f = Fileinfo(move(expandedname), 0, depth, &cache);
    files.push_back(f);
    
    auto entry = pathClusters.find(path);
    if (entry == pathClusters.end()) {
      pathClusters.emplace(
        path,
        Cluster(
          path,
          vector<Fileinfo>({f}),
          aHashPtr,
          pHashPtr,
          0.0
        )
      );
    } else {
      entry->second.add(f);
    }
    
    return 0;
  });

  dirlist.walk(string(path));
  calcHashes(files);
}

void Rdutil::calcClusterSortSuggestions() {
  for (auto& c : clusters) {
    cout << "Sorting cluster(size:" << c.size() << ", distance:" << c.distance << "with:" << endl;
    for (auto& f : c.files) { cout << "  " << f.name() << endl; }
  
    for (auto& pathC : pathClusters) {
      double minDistance = numeric_limits<double>::max();
      double maxDistance = 0;
      for (auto& f : c.files) {
        double d;
        if (!f.isInvalidImage()) {
          pathC.second.calcDistance(f, d);
          minDistance = fmin(minDistance, d);
          maxDistance = fmax(maxDistance, d);
        }
      }
      
      cout << "Sorting cluster with:" << endl;
      cout << " " << pathC.second.getName() << " min:" << minDistance << " max:" << maxDistance << endl;
    }
  }
}
