
#ifndef Tools_hpp
#define Tools_hpp

#include <vector>

using namespace std;

template <class T, class Creator>
vector<thread> runInParallel(vector<T>& v, Creator creator) {
  const auto coreCount = thread::hardware_concurrency();
  const auto bucketSize = max((size_t)1, v.size() / (coreCount - 1));
  
  auto threads = vector<thread>();
  for (int i = 0; i < v.size(); i += bucketSize) {
    size_t resultBucketSize = 0;
    if (i + bucketSize <= v.size()) {
        resultBucketSize = bucketSize;
    } else {
        resultBucketSize = v.size() - i;
    }

    threads.push_back(thread(
      creator(
         v.begin() + i,
         v.begin() + i + resultBucketSize
       )
      )
    );
  }

  return threads;
}

#endif /* Tools_hpp */
