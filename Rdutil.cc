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
using namespace cv::ml;

void Rdutil::sortClustersBySize() {
  sort(clusters.begin(), clusters.end(), [](const Cluster& c1, const Cluster& c2) {
      auto c2s = c2.size();
      auto c1s = c1.size();
      return ((c2s < c1s) || (c2s == c1s && c2.distance < c1.distance));
    }
  );
}

int Rdutil::printtofile(const string& filename) {
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
      output << n << ":" << f.get()->size() << ' ' << f.get()->name() << '\n';
      ++n;
    }
  }

  if (!pathClusters.empty()) {
    output << "\n\n### Sorting ###\n\n";
    //calcClusterSortSuggestions(output);
    
    buildTrainData(output);
  }

  f1.close();
  return 0;
}

// mark files with a unique number
void Rdutil::markitems() {
  int64_t fileno = 1;
  for (auto& file : m_list) {
    file.get()->setidentity(fileno++);
  }
}

namespace {

  bool cmpDeviceInode(const Ptr<Fileinfo>& a, const Ptr<Fileinfo>& b) {
    return make_tuple(a.get()->device(), a.get()->inode()) <
           make_tuple(b.get()->device(), b.get()->inode());
  }

  // compares rank as described in RANKING on man page.
  bool cmpRank(const Ptr<Fileinfo>& a, const Ptr<Fileinfo>& b) {
    return make_tuple(a.get()->get_cmdline_index(), a.get()->depth(), a.get()->getidentity()) <
           make_tuple(b.get()->get_cmdline_index(), b.get()->depth(), b.get()->getidentity());
  }

  bool cmpDepthName(const Ptr<Fileinfo>& a, const Ptr<Fileinfo>& b) {
    // inefficient, make it a reference.
    return make_tuple(a.get()->depth(), a.get()->name()) <
           make_tuple(b.get()->depth(), b.get()->name());
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
      for_each(first, best, [&filesToRemove](Ptr<Fileinfo>& f) mutable {
        filesToRemove.insert(f.get()->getidentity());
      });
      
      filesToRemove.erase(best->get()->getidentity());
      
      for_each(best + 1, last, [&filesToRemove](Ptr<Fileinfo>& f) mutable {
        filesToRemove.insert(f.get()->getidentity());
      });
    });

  auto it = remove_if(
    m_list.begin(), m_list.end(), [&filesToRemove](Ptr<Fileinfo>& f) {
        return filesToRemove.find(f.get()->getidentity()) != filesToRemove.end();
    }
  );
    
  m_list.erase(it, m_list.end());
    
  return initialSize - m_list.size();
}

size_t Rdutil::removeNonImages() {
    auto initialSize = m_list.size();
    auto it = remove_if(
        m_list.begin(), m_list.end(), [](Ptr<Fileinfo>& f) {
            return !f.get()->isImage();
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

size_t Rdutil::removeInvalidImages(vector<Ptr<Fileinfo>>& files) {
  const auto size_before = files.size();
  auto it = remove_if(files.begin(), files.end(), [](const Ptr<Fileinfo>& f) {
    return f.get()->isInvalidImage();
  });

  files.erase(it, files.end());

  const auto size_after = files.size();

  return size_before - size_after;
}

Fileinfo::filesizetype Rdutil::totalsizeinbytes() const
{
  Fileinfo::filesizetype totalsize = 0;
  for (const auto& elem : m_list) {
    totalsize += elem.get()->size();
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
    vector<Ptr<Fileinfo>>::iterator begin;
    vector<Ptr<Fileinfo>>::iterator end;

public:
    CalcHashesThread(
      vector<Ptr<Fileinfo>>::iterator b,
      vector<Ptr<Fileinfo>>::iterator e
    ) {
        begin = b;
        end = e;
    }
    
    void operator()(){
        for_each(begin, end, [this](Ptr<Fileinfo>& f) {
            f.get()->calcHashes();
        });
    }
};

void Rdutil::calcHashes() {
  calcHashes(m_list);
}

void Rdutil::calcHashes(vector<Ptr<Fileinfo>>& files) {
  auto threads = runInParallel(
    files,
    [](vector<Ptr<Fileinfo>>::iterator begin, vector<Ptr<Fileinfo>>::iterator end) {
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
        vector<Ptr<Fileinfo>>({lf}),
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

static bool startsWith(const string_view& str, const string_view& prefix) {
    return str.size() >= prefix.size() && 0 == str.compare(0, prefix.size(), prefix);
}

void Rdutil::buildPathClusters(const char* path, const char* excludePath, Dirlist& dirlist, Cache& cache) {
  Ptr<ImgHashBase> aHashPtr = AverageHash::create();
  Ptr<ImgHashBase> pHashPtr = PHash::create();
  vector<Ptr<Fileinfo>> files;
  string excludePathString(excludePath);

  dirlist.setcallbackfcn([this, &excludePathString, &aHashPtr, &pHashPtr, &cache, &files](const string& path, const string& name, int depth) {
    if (excludePathString.length() > 0 && startsWith(path, excludePathString)) {
      return 0;
    }
  
    string expandedname = path.empty() ? name : (path + "/" + name);
    Ptr<Fileinfo> f = make_shared<Fileinfo>(expandedname, 0, depth, &cache);
    if (f.get()->isImage()) {
      files.push_back(f);
      
      auto entry = pathClusters.find(path);
      if (entry == pathClusters.end()) {
        pathClusters.emplace(
          path,
          Cluster(
            path,
            vector<Ptr<Fileinfo>>({f}),
            aHashPtr,
            pHashPtr,
            0.0
          )
        );
      } else {
        entry->second.add(f);
      }
    }
    
    return 0;
  });

  dirlist.walk(string(path));
  calcHashes(files);
}

//const int WIDTH_SIZE = 50;
//const int HEIGHT_SIZE = 50;

struct Color {
  uchar r, g, b;
  
  bool operator() (const Color& lhs, const Color& rhs) const {
       return (lhs.r < rhs.r) || (lhs.r < rhs.r && lhs.g < rhs.g) || (lhs.r < rhs.r && lhs.g < rhs.g && lhs.b < rhs.b);
   }
};

// Color colors[10*10*10];

bool loadMLImage(const string& imagePath, Mat& outputImage) {
  // load image in grayscale
  Mat image = imread(imagePath, IMREAD_COLOR);
  Mat temp;
  map<Color, int, Color> colors;
  
  int colorCount = image.rows * image.cols;
  Color sharedColor = {0, 0, 0};
  
  for (int r = 0; r < image.rows; ++r) {
    for (int c = 0; c < image.cols; ++c) {
      Vec3b& colorVec = image.at<Vec3b>();
      float divider = 100.0f/2550.0f;
      sharedColor.r = colorVec[0] * divider;
      sharedColor.g = colorVec[1] * divider;
      sharedColor.b = colorVec[2] * divider;
      
      auto colorIterator = colors.find(sharedColor);
      if (colorIterator == colors.end()) {
        colors[sharedColor] = 1;
      } else {
        colorIterator->second += 1;
      }
    }
  }
  
  //memset(colors, 0, sizeof(colors));
//  cout << imagePath << endl;
  
  ///
  //cv::Matsource_img=cv::imread("detect.bmp",1); // Read image from path

  // Serialize, float
  outputImage.create(0, 1, CV_32F);
  outputImage.reserve(1000);
  
  for (uchar r = 0; r <= 10; ++r) {
    for (uchar g = 0; g <= 10; ++g) {
      for (uchar b = 0; b <= 10; ++b) {
        sharedColor = {r, g, b};
        auto colorIterator = colors.find(sharedColor);
        if (colorIterator != colors.end()) {
          int count = colorIterator->second;
          outputImage.push_back((float)count);
        } else {
          outputImage.push_back(0.0f);
        }
      }
    }
  }
  
  
//  data.convertTo(data, CV_32F);

  // Perform k-Means
//  int k = 3;
//  vector<int> labels;
//  Mat3f centers;
//  kmeans(data, k, labels, cv::TermCriteria(
//        TermCriteria::Type::MAX_ITER //| TermCriteria::Type::EPS,
//        ,100 //(int) INT_MAX
//        ,1.0),
//        1, cv::KMEANS_PP_CENTERS, centers);

  // Make a poster! (Clustering)
//  for (int i = 0; i < (int)labels.size(); ++i) {
//      data.at<float>(i, 0) = centers.at<float>(labels[i], 0);
//      data.at<float>(i, 1) = centers.at<float>(labels[i], 1);
//      data.at<float>(i, 2) = centers.at<float>(labels[i], 2);
//     // qDebug()<<labels.size();
//  }

//  int labelCount[k];
//  memset(labelCount, 0, sizeof(labelCount));
//  for (int i = 0; i < (int)labels.size(); ++i) {
//    labelCount[labels[i]]++;
//  }

  // Un-Serialize, un-float
//  cv::Mat destination = data.reshape(3, image.rows);
//  destination.convertTo(image, CV_8UC3);
  
//  for (int i = 0; i < k; ++i) {
//    cout << labelCount[i] << " ";
//  }
//  cout << endl;
//  cout << centers << endl;
//  for (int i = 0; i < centers.rows; ++i) {
//    cout << (int)centers.at<float>(i, 0) << " ";
//    cout << (int)centers.at<float>(i, 1) << " ";
//    cout << (int)centers.at<float>(i, 2) << " ";
//    cout << endl;
//  }
//
//  vector<pair<int, Vec3f>> coutToColor;
//  for (int i = 0; i < k; ++i) {
//    auto v = Vec3f((int)centers.at<float>(i, 0)/10, (int)centers.at<float>(i, 1)/10, (int)centers.at<float>(i, 2)/10);
//    coutToColor.push_back(
//      pair<int, Vec3f>(
//        labelCount[i],
//        v / 25.0f
//      )
//    );
//  }
  
//  std::sort(coutToColor.begin(), coutToColor.end(), [](const pair<int, Vec3f>& a, const pair<int, Vec3f>& b) {
//      return a.first > b.first;
//    }
//  );

  //cout << coutToColor << endl;

//  // Paint!
//  //QLabel *palette[6] = {ui->b_1, ui->b_2, ui->b_3, ui->b_4, ui->b_5, ui->b_6};
//  const int p = 0; // Offset
//  //centers.convertTo(outputImage, CV_32FC3);
//  outputImage.create(0, 1, CV_32FC3);
//  std::for_each(coutToColor.begin(), coutToColor.end(), [&outputImage](const pair<int, Vec3f>& a) {
//    outputImage.push_back(a.second);
//    //outputImage.push_back(a.second[1]);
//    //outputImage.push_back(a.second[2]);
//  });
  
//  cout << imagePath << endl;
//  cout << outputImage << endl;
  
//  for(int i = 0; i < 3; ++i) {
//      //std::stringstream back_clr;
//      //back_clr << centers.at<float>(i + p, 2) << "," << centers.at<float>(i + p, 1) << "," << centers.at<float>(i + p, 0);
//      //palette[i]->setStyleSheet(QString::fromStdString("background-color:rgb(" + back_clr.str() + ");"));
//      //qInfo()<< QString::fromStdString(back_clr.str() );
//      //cout << back_clr.str() << endl;
//
//      //outputImage.push_back(centers.at<float>(i + p, 2));
//  }
  
  ///

//  // check for invalid input
//  if (image.empty()) {
//      cout << "Could not open or find the image: " << imagePath << std::endl;
//      return false;
//  }
//
//  // resize the image
//  Size size(WIDTH_SIZE, HEIGHT_SIZE);
//  resize(image, temp, size, 0, 0, InterpolationFlags::INTER_AREA);
//
//  temp /= 10;
//  // convert to float 1-channel
//   //cout << temp.row(0) << endl;//.at<uchar>(0) << " " << temp.row(0).at<uchar>(0) << endl;
//  temp.convertTo(outputImage, CV_32FC3, 1.0/25.0);
//   //cout << outputImage.row(0) << endl;//.at<float>(0) << " " << outputImage.row(0).at<float>(0) << endl;
//  //cout << "temp.type(): " << temp.type() << " " << CV_8UC3 << " outputImage.type(): " << outputImage.type() << " " << CV_32FC3 << endl;
  return true;
}

bool exists(const char *fileName) {
    std::ifstream infile(fileName);
    return infile.good();
}

Mat shuffleRows(const Mat &matrix)
{
  vector <int> seeds;
  for (int cont = 0; cont < matrix.rows; cont++)
    seeds.push_back(cont);

  randShuffle(seeds);

  Mat output;
  for (int cont = 0; cont < matrix.rows; cont++)
    output.push_back(matrix.row(seeds[cont]));

  return output;
}

void Rdutil::buildTrainData(ostream& out) {
  Ptr<ANN_MLP> mlp;
//  const char* mlpPath = "./mlpfile";
//  if (exists(mlpPath)) {
//    mlp = ANN_MLP::load(mlpPath);
//
//  } else {
    // load training data
  
    Mat inputTrainingData;
    Mat outputTrainingData;
    
    int ci = 0;
    out << "Clusters:" << '\n';
    for (auto& cl : pathClusters) { out << ci++ << ": " << cl.first << '\n'; }
    out << '\n';
    
    int i = 0;
    for (auto& cl : pathClusters) {
      cout << "Cluster " << cl.first << endl;
      for (auto& f : cl.second.files) {
        Mat im;
        if (!f.get()->isInvalidImage() && loadMLImage(f.get()->name(), im)) {
          Mat signImageDataInOneRow = im.reshape(0, 1);
          //cout << "signImageDataInOneRow.type(): " << signImageDataInOneRow.type() << endl;
          inputTrainingData.push_back(signImageDataInOneRow);
          
          vector<float> outputTraningVector(pathClusters.size());
          fill(outputTraningVector.begin(), outputTraningVector.end(), -1.0);
          outputTraningVector[i] = 1.0;
          
          Mat outputMat(outputTraningVector, false);
          outputTrainingData.push_back(outputMat.reshape(0, 1));
        }
      }
      
      ++i;
    }
    
    // shuffle
    //auto shuffledInputTrainingData = shuffleRows(inputTrainingData);
    
      vector <int> seeds;
      for (int cont = 0; cont < inputTrainingData.rows; cont++)
        seeds.push_back(cont);

      randShuffle(seeds);

      Mat shuffledInputTrainingData;
      Mat shuffledOutputTrainingData;
      for (int cont = 0; cont < inputTrainingData.rows; cont++) {
        shuffledInputTrainingData.push_back(inputTrainingData.row(seeds[cont]));
        shuffledOutputTrainingData.push_back(outputTrainingData.row(seeds[cont]));
      }

  // shuffle
    
    Ptr<TrainData> trainingData = TrainData::create(
        shuffledInputTrainingData,
        SampleTypes::ROW_SAMPLE,
        shuffledOutputTrainingData
    );
  
    // build
    mlp = ANN_MLP::create();
    Mat layersSize = Mat(3, 1, CV_16U);
    layersSize.row(0) = Scalar(inputTrainingData.cols);
    layersSize.row(1) = Scalar(20*pathClusters.size());
    layersSize.row(2) = Scalar(outputTrainingData.cols);
    
    mlp->setLayerSizes(layersSize);
    mlp->setActivationFunction(ANN_MLP::ActivationFunctions::SIGMOID_SYM, 0.0, 0.0);
    mlp->setTrainMethod(ANN_MLP::TrainingMethods::BACKPROP, 0.1);
    //mlp->setTrainMethod(ANN_MLP::TrainingMethods::RPROP);

    TermCriteria termCrit = TermCriteria(
        TermCriteria::Type::COUNT
        ,10000 //(int) INT_MAX
        ,0.01
    );
    mlp->setTermCriteria(termCrit);
    
    auto start = std::chrono::system_clock::now();
    mlp->train(trainingData,
        0//ANN_MLP::TrainFlags::UPDATE_WEIGHTS
        //, ANN_MLP::TrainFlags::NO_INPUT_SCALE
        //+ ANN_MLP::TrainFlags::NO_OUTPUT_SCALE
    );
    auto duration = duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now() - start);
    cout << "Training time: " << duration.count() << "ms" << endl;
    cout << "Layer sizes " << mlp->getLayerSizes() << endl;
  //  cout << "L0: " << mlp->getWeights(0).size << " " << mlp->getWeights(0) << endl;
  //  cout << "L1: " << mlp->getWeights(1).size << " " << mlp->getWeights(1) << endl;
  //  cout << "L2: " << mlp->getWeights(2).size << " " << mlp->getWeights(2) << endl;

    mlp->save("./mlpfile");
  //}

  for (auto& f : m_list) {
    Mat img;
    Mat result;
    if (loadMLImage(f.get()->name(), img)) {
      cout << "Predict for " << f.get()->name() << " is " << endl;
      out << f.get()->name() << '\n';
      mlp->predict(img.reshape(1, 1), result);
      //out << result << endl;
      for (int c=0; c<result.cols; ++c) {
        cout << result.col(c) << endl;
        out << c << ": " << result.col(c) << '\n';
      }
    }
  }
}

struct ClusterDistance {
  double minDistance;
  double maxDistance;
};

struct ClusterSuggestions {
  vector<pair<Cluster*, ClusterDistance>> clusters;
  
  void add(Cluster* cluster, double minDistance, double maxDistance) {
    clusters.push_back(pair<Cluster*, ClusterDistance>(cluster, {minDistance, maxDistance}));
  }
  
  vector<pair<Cluster*, ClusterDistance>>& keepTop(int count) {
    sort(clusters.begin(), clusters.end(), [](const pair<Cluster*, ClusterDistance>& a, const pair<Cluster*, ClusterDistance>& b) {
      return (a.second.minDistance < b.second.minDistance) ||
        ((a.second.minDistance == b.second.minDistance) && a.second.maxDistance < b.second.maxDistance);
    });

    if (clusters.size() > count) {
      clusters.resize(count);
    }
    return clusters;
  }
};

void Rdutil::calcClusterSortSuggestions(ostream& out) {
  for (auto& c : clusters) {
    out << "Sorting cluster(size:" << c.size() << ", distance:" << c.distance << " with:" << "\n";
    for (auto& f : c.files) { out << "  " << f.get()->name() << endl; }
    out << "to" << endl;
    
    ClusterSuggestions suggestions;
  
    for (auto& pathC : pathClusters) {
      double minDistance = numeric_limits<double>::max();
      double maxDistance = 0;
      for (auto& f : c.files) {
        
        if (f.get()->isInvalidImage()) {
          continue;
        }

        for (auto& cf : pathC.second.files) {
          if (cf.get()->isInvalidImage()) {
            continue;
          }
        
          //auto aDistance = c.getAHashPtr()->compare(f.get()->getAHash(), cf.get()->getAHash());
          auto pDistance = c.getPHashPtr()->compare(f.get()->getPHash(), cf.get()->getPHash());
        
          double d = pDistance; //fmax(aDistance, pDistance);
          minDistance = fmin(minDistance, d);
          maxDistance = fmax(maxDistance, d);
        }
        
        suggestions.add(&pathC.second, minDistance, maxDistance);
      }
    }
    
    suggestions.keepTop(4);
    for (auto& s : suggestions.clusters) {
      out << " " << s.first->name << " min:" << s.second.minDistance << " max:" << s.second.maxDistance << "\n";
    }
    
    out << "\n";
  }
}
