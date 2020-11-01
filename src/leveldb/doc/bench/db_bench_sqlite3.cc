// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>
#include "util/histogram.h"
#include "util/random.h"
#include "util/testutil.h"

// Comma-separated list of operations to run in the specified order
//   Actual benchmarks:
//
//   fillseq       -- write N values in sequential key order in async mode
//   fillseqsync   -- write N/100 values in sequential key order in sync mode
//   fillseqbatch  -- batch write N values in sequential key order in async mode
//   fillrandom    -- write N values in random key order in async mode
//   fillrandsync  -- write N/100 values in random key order in sync mode
//   fillrandbatch -- batch write N values in sequential key order in async mode
//   overwrite     -- overwrite N values in random key order in async mode
//   fillrand100K  -- write N/1000 100K values in random order in async mode
//   fillseq100K   -- write N/1000 100K values in sequential order in async mode
//   readseq       -- read N times sequentially
//   readrandom    -- read N times in random order
//   readrand100K  -- read N/1000 100K values in sequential order in async mode
static const char* FLAGS_benchmarks =
    "fillseq,"
    "fillseqsync,"
    "fillseqbatch,"
    "fillrandom,"
    "fillrandsync,"
    "fillrandbatch,"
    "overwrite,"
    "overwritebatch,"
    "readrandom,"
    "readseq,"
    "fillrand100K,"
    "fillseq100K,"
    "readseq,"
    "readrand100K,"
    ;

// Number of key/values to place in database
static int FLAGS_num = 1000000;

// Number of read operations to do.  If negative, do FLAGS_num reads.
static int FLAGS_reads = -1;

// Size of each value
static int FLAGS_value_size = 100;

// Print histogram of operation timings
static bool FLAGS_histogram = false;

// Arrange to generate values that shrink to this fraction of
// their original size after compression
static double FLAGS_compression_ratio = 0.5;

// Page size. Default 1 KB.
static int FLAGS_page_size = 1024;

// Number of pages.
// Default cache size = FLAGS_page_size * FLAGS_num_pages = 4 MB.
static int FLAGS_num_pages = 4096;

// If true, do not destroy the existing database.  If you set this
// flag and also specify a benchmark that wants a fresh database, that
// benchmark will fail.
static bool FLAGS_use_existing_db = false;

// If true, we allow batch writes to occur
static bool FLAGS_transaction = true;

// If true, we enable Write-Ahead Logging
static bool FLAGS_WAL_enabled = true;

// Use the db with the following name.
static const char* FLAGS_db = NULL;

inline
static void ExecErrorCheck(int status, char *err_msg) {
  if (status != SQLITE_OK) {
    fprintf(stderr, "SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
    exit(1);
  }
}

inline
static void StepErrorCheck(int status) {
  if (status != SQLITE_DONE) {
    fprintf(stderr, "SQL step error: status = %d\n", status);
    exit(1);
  }
}

inline
static void ErrorCheck(int status) {
  if (status != SQLITE_OK) {
    fprintf(stderr, "sqlite3 error: status = %d\n", status);
    exit(1);
  }
}

inline
static void WalCheckpoint(sqlite3* db_) {
  // Flush all writes to disk
  if (FLAGS_WAL_enabled) {
    sqlite3_wal_checkpoint_v2(db_, NULL, SQLITE_CHECKPOINT_FULL, NULL, NULL);
  }
}

namespace leveldb {

// Helper for quickly generating random data.
namespace {
class RandomGenerator {
 private:
  std::string data_;
  int pos_;

 public:
  RandomGenerator() {
    // We use a limited amount of data over and over again and ensure
    // that it is larger than the compression window (32KB), and also
    // large enough to serve all typical value sizes we want to write.
    Random rnd(301);
    std::string piece;
    while (data_.size() < 1048576) {
      // Add a short fragment that is as compressible as specified
      // by FLAGS_compression_ratio.
      test::CompressibleString(&rnd, FLAGS_compression_ratio, 100, &piece);
      data_.append(piece);
    }
    pos_ = 0;
  }

  Slice Generate(int len) {
    if (pos_ + len > data_.size()) {
      pos_ = 0;
      assert(len < data_.size());
    }
    pos_ += len;
    return Slice(data_.data() + pos_ - len, len);
  }
};

static Slice TrimSpace(Slice s) {
  int start = 0;
  while (start < s.size() && isspace(s[start])) {
    start++;
  }
  int limit = s.size();
  while (limit > start && isspace(s[limit-1])) {
    limit--;
  }
  return Slice(s.data() + start, limit - start);
}

}  // namespace

class Benchmark {
 private:
  sqlite3* db_;
  int db_num_;
  int num_;
  int reads_;
  double start_;
  double last_op_finish_;
  int64_t bytes_;
  std::string message_;
  Histogram hist_;
  RandomGenerator gen_;
  Random rand_;

  // State kept for progress messages
  int done_;
  int next_report_;     // When to report next

  void PrintHeader() {
    const int kKeySize = 16;
    PrintEnvironment();
    fprintf(stdout, "Keys:       %d bytes each\n", kKeySize);
    fprintf(stdout, "Values:     %d bytes each\n", FLAGS_value_size);
    fprintf(stdout, "Entries:    %d\n", num_);
    fprintf(stdout, "RawSize:    %.1f MB (estimated)\n",
            ((static_cast<int64_t>(kKeySize + FLAGS_value_size) * num_)
             / 1048576.0));
    PrintWarnings();
    fprintf(stdout, "------------------------------------------------\n");
  }

  void PrintWarnings() {
#if defined(__GNUC__) && !defined(__OPTIMIZE__)
    fprintf(stdout,
            "WARNING: Optimization is disabled: benchmarks unnecessarily slow\n"
            );
#endif
#ifndef NDEBUG
    fprintf(stdout,
            "WARNING: Assertions are enabled; benchmarks unnecessarily slow\n");
#endif
  }

  void PrintEnvironment() {
    fprintf(stderr, "SQLite:     version %s\n", SQLITE_VERSION);

#if defined(__linux)
    time_t now = time(NULL);
    fprintf(stderr, "Date:       %s", ctime(&now));  // ctime() adds newline

    FILE* cpuinfo = fopen("/proc/cpuinfo", "r");
    if (cpuinfo != NULL) {
      char line[1000];
      int num_cpus = 0;
      std::string cpu_type;
      std::string cache_size;
      while (fgets(line, sizeof(line), cpuinfo) != NULL) {
        const char* sep = strchr(line, ':');
        if (sep == NULL) {
          continue;
        }
        Slice key = TrimSpace(Slice(line, sep - 1 - line));
        Slice val = TrimSpace(Slice(sep + 1));
        if (key == "model name") {
          ++num_cpus;
          cpu_type = val.ToString();
        } else if (key == "cache size") {
          cache_size = val.ToString();
        }
      }
      fclose(cpuinfo);
      fprintf(stderr, "CPU:        %d * %s\n", num_cpus, cpu_type.c_str());
      fprintf(stderr, "CPUCache:   %s\n", cache_size.c_str());
    }
#endif
  }

  void Start() {
    start_ = Env::Default()->NowMicros() * 1e-6;
    bytes_ = 0;
    message_.clear();
    last_op_finish_ = start_;
    hist_.Clear();
    done_ = 0;
    next_report_ = 100;
  }

  void FinishedSingleOp() {
    if (FLAGS_histogram) {
      double now = Env::Default()->NowMicros() * 1e-6;
      double micros = (now - last_op_finish_) * 1e6;
      hist_.Add(micros);
      if (micros > 20000) {
        fprintf(stderr, "long op: %.1f micros%30s\r", micros, "");
        fflush(stderr);
      }
      last_op_finish_ = now;
    }

    done_++;
    if (done_ >= next_report_) {
      if      (next_report_ < 1000)   next_report_ += 100;
      else if (next_report_ < 5000)   next_report_ += 500;
      else if (next_report_ < 10000)  next_report_ += 1000;
      else if (next_report_ < 50000)  next_report_ += 5000;
      else if (next_report_ < 100000) next_report_ += 10000;
      else if (next_report_ < 500000) next_report_ += 50000;
      else                            next_report_ += 100000;
      fprintf(stderr, "... finished %d ops%30s\r", done_, "");
      fflush(stderr);
    }
  }

  void Stop(const Slice& name) {
    double finish = Env::Default()->NowMicros() * 1e-6;

    // Pretend at least one op was done in case we are running a benchmark
    // that does not call FinishedSingleOp().
    if (done_ < 1) done_ = 1;

    if (bytes_ > 0) {
      char rate[100];
      snprintf(rate, sizeof(rate), "%6.1f MB/s",
               (bytes_ / 1048576.0) / (finish - start_));
      if (!message_.empty()) {
        message_  = std::string(rate) + " " + message_;
      } else {
        message_ = rate;
      }
    }

    fprintf(stdout, "%-12s : %11.3f micros/op;%s%s\n",
            name.ToString().c_str(),
            (finish - start_) * 1e6 / done_,
            (message_.empty() ? "" : " "),
            message_.c_str());
    if (FLAGS_histogram) {
      fprintf(stdout, "Microseconds per op:\n%s\n", hist_.ToString().c_str());
    }
    fflush(stdout);
  }

 public:
  enum Order {
    SEQUENTIAL,
    RANDOM
  };
  enum DBState {
    FRESH,
    EXISTING
  };

  Benchmark()
  : db_(NULL),
    db_num_(0),
    num_(FLAGS_num),
    reads_(FLAGS_reads < 0 ? FLAGS_num : FLAGS_reads),
    bytes_(0),
    rand_(301) {
    std::vector<std::string> files;
    std::string test_dir;
    Env::Default()->GetTestDirectory(&test_dir);
    Env::Default()->GetChildren(test_dir, &files);
    if (!FLAGS_use_existing_db) {
      for (int i = 0; i < files.size(); i++) {
        if (Slice(files[i]).starts_with("dbbench_sqlite3")) {
          std::string file_name(test_dir);
          file_name += "/";
          file_name += files[i];
          Env::Default()->DeleteFile(file_name.c_str());
        }
      }
    }
  }

  ~Benchmark() {
    int status = sqlite3_close(db_);
    ErrorCheck(status);
  }

  void Run() {
    PrintHeader();
    Open();

    const char* benchmarks = FLAGS_benchmarks;
    while (benchmarks != NULL) {
      const char* sep = strchr(benchmarks, ',');
      Slice name;
      if (sep == NULL) {
        name = benchmarks;
        benchmarks = NULL;
      } else {
        name = Slice(benchmarks, sep - benchmarks);
        benchmarks = sep + 1;
      }

      bytes_ = 0;
      Start();

      bool known = true;
      bool write_sync = false;
      if (name == Slice("fillseq")) {
        Write(write_sync, SEQUENTIAL, FRESH, num_, FLAGS_value_size, 1);
        WalCheckpoint(db_);
      } else if (name == Slice("fillseqbatch")) {
        Write(write_sync, SEQUENTIAL, FRESH, num_, FLAGS_value_size, 1000);
        WalCheckpoint(db_);
      } else if (name == Slice("fillrandom")) {
        Write(write_sync, RANDOM, FRESH, num_, FLAGS_value_size, 1);
        WalCheckpoint(db_);
      } else if (name == Slice("fillrandbatch")) {
        Write(write_sync, RANDOM, FRESH, num_,