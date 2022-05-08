//
//  Cluster.hpp
//  rdfind
//
//  Created by Alexey Glushkov on 08.05.2022.
//

#ifndef Cluster_hpp
#define Cluster_hpp

#include <vector>
#include <opencv2/opencv.hpp>
#include <opencv2/img_hash.hpp>

#include "Fileinfo.hh" //file container

using namespace std;
using namespace cv;
using namespace cv::img_hash;

struct Cluster {
  string name;
  vector<Fileinfo> files;
  Ptr<ImgHashBase> aHashPtr;
  Ptr<ImgHashBase> pHashPtr;
  double distance = 0.0;
  
public:
    Cluster(
    string name,
    vector<Fileinfo> files,
    Ptr<ImgHashBase> aHashPtr,
    Ptr<ImgHashBase> pHashPtr,
    double d
    )
        : name(name)
        , files(files)
        , aHashPtr(aHashPtr)
        , pHashPtr(pHashPtr)
        , distance(d)
    {}

  void calcDistance(Fileinfo& f, double& outDistance) const;
  bool needAdd(Fileinfo& f, double& outDistance) const;
  
  void add(Fileinfo& f);
  
  std::vector<Fileinfo> filesSortedBySize() const;
  
  bool isSingle() const;
  
  size_t size() const;
  
  Fileinfo::filesizetype fileSize() const;
  
  Fileinfo::filesizetype fileSizeWithoutBiggest() const;
  
  const std::vector<Fileinfo>& getFiles() const {
    return files;
  }
  
  void setDistance(double d) {
    distance = d;
  }
  
  double getDistance() const {
    return distance;
  }
  
  const string& getName() const {
    return name;
  }
};

#endif /* Cluster_hpp */
