/**
   copyright 2006-2017 Paul Dreik (earlier Paul Sundvall)
   Distributed under GPL v 2.0 or later, at your option.
   See LICENSE for further details.
*/

#include "config.h" //header file from autoconf

// std
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

// project
#include "CmdlineParser.hh"
#include "Dirlist.hh"     //to find files
#include "Fileinfo.hh"    //file container
#include "RdfindDebug.hh" //debug macro
#include "Rdutil.hh"      //to do some work

#include <opencv2/opencv.hpp>

#include "Cache.hh"

using namespace std;

// global variables

// this vector holds the information about all files found
vector<Ptr<Fileinfo>> filelist;
struct Options;
const Options* global_options{};

void loadListOfFiles(Rdutil& gswd, Parser& parser, const Options& o);

/**
 * this contains the command line index for the path currently
 * being investigated. it has to be global, because function pointers
 * are being used.
 */
int current_cmdline_index = 0;

static void
usage()
{
  cout
    << "Usage: "
    << "rdfind [options] FILE ...\n"
    << '\n'
    << "Finds duplicate files recursively in the given FILEs (directories),\n"
    << "and takes appropriate action (by default, nothing).\n"
    << "Directories listed first are ranked higher, meaning that if a\n"
    << "file is found on several places, the file found in the directory "
       "first\n"
    << "encountered on the command line is kept, and the others are "
       "considered duplicate.\n"
    << '\n'
    << "options are (default choice within parentheses)\n"
    << '\n'
    << " -ignoreempty      (true)| false  ignore empty files (true implies "
       "-minsize 1,\n"
    << "                                  false implies -minsize 0)\n"
    << " -minsize N        (N=1)          ignores files with size less than N "
       "bytes\n"
    << " -maxsize N        (N=0)          ignores files with size N "
       "bytes and larger (use 0 to disable this check).\n"
    << " -followsymlinks    true |(false) follow symlinks\n"
    << " -removeidentinode (true)| false  ignore files with nonunique "
       "device and inode\n"
    << " -deterministic    (true)| false  makes results independent of order\n"
    << "                                  from listing the filesystem\n"
    << " -outputname  name  sets the results file name to \"name\" "
       "(default results.txt)\n"
    << " -deleteduplicates  true |(false) delete duplicate files\n"
    << " -h|-help|--help                  show this help and exit\n"
    << " -v|--version                     display version number and exit\n"
    << '\n'
    << "If properly installed, a man page should be available as man rdfind.\n"
    << '\n'
    << "rdfind is written by Paul Dreik 2006 onwards. License: GPL v2 or "
       "later (at your option).\n"
    << "version is " << VERSION << '\n';
}

Cache cache;

struct Options {
  // operation mode and default values
  Fileinfo::filesizetype minimumfilesize =
    1; // minimum file size to be noticed (0 - include empty files)
  Fileinfo::filesizetype maximumfilesize =
    0; // if nonzero, files this size or larger are ignored
  bool followsymlinks = false;        // follow symlinks
  bool remove_identical_inode = true; // remove files with identical inodes
  bool deterministic = false; // be independent of filesystem order
  string resultsfile = "rdfind_results.txt"; // results file name.
  string cachefile = ""; // cache file name.
  const char* clusterPath = ""; // path to folder-clusters
};

Options parseOptions(Parser& parser) {
  Options o;
  for (; parser.has_args_left(); parser.advance()) {
    // empty strings are forbidden as input since they can not be file names or
    // options
    if (parser.get_current_arg()[0] == '\0') {
      cerr << "bad argument " << parser.get_current_index() << '\n';
      exit(EXIT_FAILURE);
    }

    // if we reach the end of the argument list - exit the loop and proceed with
    // the file list instead.
    if (parser.get_current_arg()[0] != '-') {
      // end of argument list - exit!
      break;
    }
    if (parser.try_parse_string("-outputname")) {
      o.resultsfile = parser.get_parsed_string();
    } else if (parser.try_parse_string("-cachename")) {
      o.cachefile = parser.get_parsed_string();
    } else if (parser.try_parse_bool("-ignoreempty")) {
      if (parser.get_parsed_bool()) {
        o.minimumfilesize = 1;
      } else {
        o.minimumfilesize = 0;
      }
    } else if (parser.try_parse_string("-minsize")) {
      const long long minsize = stoll(parser.get_parsed_string());
      if (minsize < 0) {
        throw runtime_error("negative value of minsize not allowed");
      }
      o.minimumfilesize = minsize;
    } else if (parser.try_parse_string("-maxsize")) {
      const long long maxsize = stoll(parser.get_parsed_string());
      if (maxsize < 0) {
        throw runtime_error("negative value of maxsize not allowed");
      }
      o.maximumfilesize = maxsize;
    } else if (parser.try_parse_bool("-followsymlinks")) {
      o.followsymlinks = parser.get_parsed_bool();
    } else if (parser.try_parse_bool("-removeidentinode")) {
      o.remove_identical_inode = parser.get_parsed_bool();
    } else if (parser.try_parse_bool("-deterministic")) {
      o.deterministic = parser.get_parsed_bool();
    } else if (parser.try_parse_string("-clusterpath")) {
      o.clusterPath = parser.get_parsed_string();
    } else if (parser.current_arg_is("-help") || parser.current_arg_is("-h") ||
               parser.current_arg_is("--help")) {
      usage();
      exit(EXIT_SUCCESS);
    } else if (parser.current_arg_is("-version") ||
               parser.current_arg_is("--version") ||
               parser.current_arg_is("-v")) {
      cout << "This is rdfind version " << VERSION << '\n';
      exit(EXIT_SUCCESS);
    } else {
      cerr << "did not understand option " << parser.get_current_index()
                << ":\"" << parser.get_current_arg() << "\"\n";
      exit(EXIT_FAILURE);
    }
  }

  // fix default values
  if (o.maximumfilesize == 0) {
    o.maximumfilesize = numeric_limits<decltype(o.maximumfilesize)>::max();
  }

  // verify conflicting arguments
  if (!(o.minimumfilesize < o.maximumfilesize)) {
    cerr << "maximum filesize " << o.maximumfilesize
              << " must be larger than minimum filesize " << o.minimumfilesize
              << "\n";
    exit(EXIT_FAILURE);
  }

  // done with parsing of options. remaining arguments are files and dirs.
  return o;
}

// function to add items to the list of all files
static int report(const string& path, const string& name, int depth) {
  RDDEBUG("report(" << path.c_str() << "," << name.c_str() << "," << depth
                    << ")" << endl);

  // expand the name if the path is nonempty
  string expandedname = path.empty() ? name : (path + "/" + name);

  Ptr<Fileinfo> tmp = make_shared<Fileinfo>(expandedname, current_cmdline_index, depth, &cache);
  if (tmp.get()->readfileinfo()) {
    if (tmp.get()->isRegularFile()) {
      const auto size = tmp.get()->size();
      if (size >= global_options->minimumfilesize &&
          size < global_options->maximumfilesize) {
        filelist.emplace_back(tmp);
      }
    }
  } else {
    cerr << "failed to read file info on file \"" << tmp.get()->name() << '\n';
    return -1;
  }
  return 0;
}


int main(int narg, const char* argv[]) {
  if (narg == 1) {
    usage();
    return 0;
  }

  // parse the input arguments
  Parser parser(narg, argv);
  const Options o = parseOptions(parser);

  if (!o.cachefile.empty()) {
    cache.load(o.cachefile);
  }

  // an object to do sorting and duplicate finding
  Rdutil gswd(filelist);

  if (strlen(o.clusterPath) > 0) {
    Dirlist dirlist(o.followsymlinks);
    gswd.buildPathClusters(o.clusterPath, dirlist, cache);
  }
  
  loadListOfFiles(gswd, parser, o);

  if (o.remove_identical_inode) {
    // remove files with identical devices and inodes from the list
    cout << "Excluded "
    << gswd.removeIdenticalInodes()
    << " files due to nonunique device and inode." << endl;
  }

  cout << "Total size is "
  << gswd.totalsizeinbytes()
  << " bytes or ";
  gswd.totalsize(cout) << endl;

  cout << "Excluded "
  << gswd.removeNonImages()
  << " non image files from list. ";
  
  cout << filelist.size()
  << " files left." << endl;
  
  gswd.calcHashes();
  if (!o.cachefile.empty()) {
    cache.save();
  }

  gswd.removeInvalidImages();
  gswd.buildClusters();

  cout << "Builting clusters... " << endl;
  cout << "Built "
  << gswd.getClusters().size()
  << " clusters " << endl;
  
//  cout << "Excluded  "
//  << gswd.removeSingleClusters()
//  << " single clusters " << endl;
  
  cout << gswd.clusterFileCount()
  << " files left" << endl;
  
  gswd.sortClustersBySize();

  cout << "Totally, ";
  gswd.saveablespace(cout)
  << " can be reduced." << endl;

  // traverse the list and make a nice file with the results
  cout << "Now making results file "
  << o.resultsfile << endl;
  gswd.printtofile(o.resultsfile);
  
  //gswd.calcClusterSortSuggestions();

  return 0;
}

void loadListOfFiles(Rdutil& gswd, Parser& parser, const Options& o) {
  // an object to traverse the directory structure
  Dirlist dirlist(o.followsymlinks);

  // this is what function is called when an object is found on
  // the directory traversed by walk. Make sure the pointer to the
  // options is set as well.
  global_options = &o;
  dirlist.setcallbackfcn(&report);

  // now loop over path list and add the files

  // done with arguments. start parsing files and directories!
  for (; parser.has_args_left(); parser.advance()) {
    // get the next arg.
    const string file_or_dir = [&]() {
      string arg(parser.get_current_arg());
      // remove trailing /
      while (arg.back() == '/' && arg.size() > 1) {
        arg.erase(arg.size() - 1);
      }
      return arg;
    }();

    auto lastsize = filelist.size();
    cout << "Now scanning \""
    << file_or_dir << "\"";
    cout.flush();

    current_cmdline_index = parser.get_current_index();
    dirlist.walk(file_or_dir, 0);

    cout << ", found "
    << filelist.size() - lastsize
    << " files." << endl;

    // if we want deterministic output, we will sort the newly added
    // items on depth, then filename.
    if (o.deterministic) {
      gswd.sort_on_depth_and_name(lastsize);
    }
  }

  cout << "Now have "
  << filelist.size()
  << " files in total." << endl;

  // mark files with a number for correct ranking. The only ordering at this
  // point is that files found on early command line index are earlier in the
  // list.
  gswd.markitems();
}
