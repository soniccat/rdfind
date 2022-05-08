//
//  Cluster.cpp
//  rdfind
//
//  Created by Alexey Glushkov on 08.05.2022.
//

#include "Cluster.hh"

bool Cluster::needAdd(Fileinfo& f, double& outDistance) const {
  double resultDistance = 0.0;
  for (auto& clusterFile : files) {
    auto aDistance = aHashPtr->compare(f.getAHash(), clusterFile.getAHash());
    auto pDistance = pHashPtr->compare(f.getPHash(), clusterFile.getPHash());
    auto d = std::fmax(aDistance, pDistance);
    resultDistance = std::fmax(resultDistance, d);
  }

  outDistance = resultDistance;
  return resultDistance <= 3.0;
}

void Cluster::add(Fileinfo& f) {
  files.push_back(f);
}

std::vector<Fileinfo> Cluster::filesSortedBySize() const {
  std::vector<Fileinfo> sorted = files;
  std::sort(sorted.begin(), sorted.end(), [](const Fileinfo& f1, const Fileinfo& f2) {
    return f2.size() < f1.size();
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
    size += f.size();
  }
  
  return size;
}
  
Fileinfo::filesizetype Cluster::fileSizeWithoutBiggest() const {
  Fileinfo::filesizetype size = 0;
  Fileinfo::filesizetype biggestSize = 0;
  for (auto& f : files) {
    biggestSize = std::fmax(biggestSize, f.size());
    size += f.size();
  }
  
  return size - biggestSize;
}
