//
//  Cluster.cpp
//  rdfind
//
//  Created by Alexey Glushkov on 08.05.2022.
//

#include "Cluster.hh"

void Cluster::calcDistance(Ptr<Fileinfo> f, double& outDistance) const {
  double resultDistance = 0.0;
  for (auto& clusterFile : files) {
    if (!clusterFile.get()->isInvalidImage()) {
      auto aDistance = aHashPtr->compare(f.get()->getAHash(), clusterFile.get()->getAHash());
      auto pDistance = pHashPtr->compare(f.get()->getPHash(), clusterFile.get()->getPHash());
      auto d = std::fmax(aDistance, pDistance);
      resultDistance = std::fmax(resultDistance, d);
    }
  }

  outDistance = resultDistance;
}

bool Cluster::needAdd(Ptr<Fileinfo> f, double& outDistance) const {
  calcDistance(f, outDistance);
  return outDistance <= 3.0;
}

void Cluster::add(Ptr<Fileinfo> f) {
  files.push_back(f);
}

std::vector<Ptr<Fileinfo>> Cluster::filesSortedBySize() const {
  std::vector<Ptr<Fileinfo>> sorted = files;
  std::sort(sorted.begin(), sorted.end(), [](const Ptr<Fileinfo>& f1, const Ptr<Fileinfo>& f2) {
    return f2.get()->size() < f1.get()->size();
  });

  return sorted;
}

bool Cluster::isSingle() const {
  return files.size() == 1;
}

size_t Cluster::size() const {
  return files.size();
}

Fileinfo::filesizetype Cluster::fileSize() const {
  Fileinfo::filesizetype size = 0;
  for (auto& f : files) {
    size += f.get()->size();
  }
  
  return size;
}
  
Fileinfo::filesizetype Cluster::fileSizeWithoutBiggest() const {
  Fileinfo::filesizetype size = 0;
  Fileinfo::filesizetype biggestSize = 0;
  for (auto& f : files) {
    biggestSize = std::fmax(biggestSize, f.get()->size());
    size += f.get()->size();
  }
  
  return size - biggestSize;
}
