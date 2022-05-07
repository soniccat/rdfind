//
//  Cache.hpp
//  rdfindtool
//
//  Created by Alexey Glushkov on 02.05.2022.
//

#ifndef Cache_hpp
#define Cache_hpp

#include <string>
#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>

using json = nlohmann::json;

struct CacheEntry {
  cv::Mat averageHash;
  cv::Mat pHash;
  bool isInvalidImage;
};

class Cache {
private:
 std::string filePath;
// path to entry
 std::map<std::string, CacheEntry> map;

public:

  Cache();
  
  void load(const std::string& path);
  void putAverageHash(const std::string& name, cv::Mat& averageHash);
  void putPHash(const std::string& name, cv::Mat& pHash);
  void putIsInvalidImage(const std::string& name, bool isInvalidImage);
  void save();
  
  void getAverageHash(const std::string& name, cv::Mat& averageHash);
  void getPHash(const std::string& name, cv::Mat& pHash);
  bool isInvalidImage(const std::string& name);
};


#endif /* Cache_hpp */
