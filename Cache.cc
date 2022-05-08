//
//  Cache.cpp
//  rdfindtool
//
//  Created by Alexey Glushkov on 02.05.2022.
//

#include "Cache.hh"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <sstream>

using namespace std;
using namespace cv;

using json = nlohmann::json;

Cache::Cache() {
}

void Cache::load(const string& path) {
  filePath = path;

  ifstream file;
  file.open(filePath.c_str(), ifstream::in);
  if (file.is_open()) {
    stringstream buffer;
    buffer << file.rdbuf();

    try {
        auto jData = json::parse(buffer.str());
        for (auto& v : jData.items()) {
          auto k = v.key();
          auto vObj = v.value();
          
          Mat aHash;
          if (vObj.contains("aHash")) {
            auto aHashArray = vObj["aHash"];
            if (aHashArray.is_array()) {
              Mat r(1, (int)aHashArray.size(), CV_8U);
              int c = 0;
              for (auto& x : aHashArray.items()) {
                *r.ptr(0, c) = x.value().get<uchar>();
                ++c;
              }
              vconcat(&r, 1, aHash);
            }
          }
          
          Mat pHash;
          if (vObj.contains("pHash")) {
            auto pHashArray = vObj["pHash"];
            // TODO: extract it into a function
            if (pHashArray.is_array()) {
              Mat r(1, (int)pHashArray.size(), CV_8U);
              int c = 0;
              for (auto& x : pHashArray.items()) {
                *r.ptr(0, c) = x.value().get<uchar>();
                ++c;
              }
              vconcat(&r, 1, pHash);
            }
          }
          
          bool isInvalidImage = false;
          if (vObj.contains("isInvalidImage")) {
            isInvalidImage = vObj["isInvalidImage"];
          }
          
          map.insert({k, {aHash, pHash, isInvalidImage}});
        }
        cout << "Loaded " << map.size() << " records from cache" << endl;
    } catch (...) {
      cerr << "Couldn't load cache file " << path << endl;
    }
  }
  
  file.close();
}

void Cache::putAverageHash(const string& name, Mat& averageHash) {
  auto fileIterator = map.find(name);
  CacheEntry entry;
  if (fileIterator != map.end()) {
    fileIterator->second.averageHash = averageHash;
  } else {
    CacheEntry entry;
    entry.averageHash = averageHash;
    map.insert({name, entry});
  }
}

void Cache::putPHash(const string& name, Mat& pHash) {
  auto fileIterator = map.find(name);
  if (fileIterator != map.end()) {
    fileIterator->second.pHash = pHash;
  } else {
    CacheEntry entry;
    entry.pHash = pHash;
    map.insert({name, entry});
  }
}

void Cache::putIsInvalidImage(const string& name, bool isInvalidImage) {
  auto fileIterator = map.find(name);
  if (fileIterator != map.end()) {
    fileIterator->second.isInvalidImage = isInvalidImage;
  } else {
    CacheEntry entry;
    entry.isInvalidImage = isInvalidImage;
    map.insert({name, entry});
  }
}

void Cache::getAverageHash(const string& name, Mat& averageHash) {
  auto fileIterator = map.find(name);
  if (fileIterator != map.end()) {
    averageHash = fileIterator->second.averageHash;
  }
}

void Cache::getPHash(const string& name, Mat& pHash) {
  auto fileIterator = map.find(name);
  if (fileIterator != map.end()) {
    pHash = fileIterator->second.pHash;
  }
}

bool Cache::isInvalidImage(const string& name) {
  auto fileIterator = map.find(name);
  if (fileIterator != map.end()) {
    return fileIterator->second.isInvalidImage;
  } else {
    return false;
  }
}

void matFirstLineToJson(Mat& mat, json& j) {
    for (int i = 0; i < mat.cols; ++i) {
      j.push_back(mat.at<uchar>(0, i));
    }
}

void Cache::save() {
  ofstream file;
  file.open(filePath.c_str(), ios_base::out);
  if (!file.is_open()) {
    cerr << "Could not open cache file \"" << filePath << "\"\n";
    return;
  }
  
  json outJson;
  
  for (auto& entry : map) {
    json pj;
    if (!entry.second.averageHash.empty()) {
      json aHashJson;
      matFirstLineToJson(entry.second.averageHash, aHashJson);
      pj["aHash"] = aHashJson;
    }
    
    if (!entry.second.pHash.empty()) {
      json pHashJson;
      matFirstLineToJson(entry.second.pHash, pHashJson);
      pj["pHash"] = pHashJson;
    }
    
    if (entry.second.isInvalidImage) {
      pj["isInvalidImage"] = true;
    }
    
    if (!pj.empty()) {
      outJson[entry.first] = pj;
    }
  }

  file << outJson.dump();
  file.close();
}
