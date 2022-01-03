#pragma once
// this test framework provides test helpers without googletest
// because valgrind complains about some googletest issues
//
#include <string>
#include <list>
#include <functional>
#include <map>

struct TestResult {
  bool passed;
  std::string desc;
  explicit TestResult(bool res) : passed{res} {}
  explicit TestResult(const std::string &res) : passed{false}, desc{res} {}
  explicit TestResult(const char *res) : passed{false}, desc{res} {}
  bool ok() { return passed; }
};

class TestCase {
public:
  TestCase(std::function<TestResult()> test_case) : result(false), fn(test_case) {}
  TestResult operator()() {
    result = fn();
    return result;
  }
  TestResult result;
  std::function<TestResult()> fn;
};

class TestRunner {
public:
  std::list<std::string> errors;

  void add(TestCase && test_case) {
     tests.push_back({++test_num, test_case});
  }

  void add(std::list<TestCase> test_cases) {
    for (auto test_case : test_cases) {
      tests.push_back({++test_num, test_case});
    }
  }

  int count() const { return tests.size(); }

  double run_all() {
    int passed = 0;
    int total = 0;
    for (auto & test_case : tests) {
      ++total;
      if (test_case.second().ok()) {
        ++passed;
      }
      else {
        std::cout << "Test " << test_case.first << " failed: " << test_case.second.result.desc << "\n";
      }
    }
    if (total == 0) return 0;
    return (double)passed / (double)total;
  }
  private:
  int test_num = 0;
  std::list<std::pair<int,TestCase>> tests;
};

#define EXPECT_BOOL(res) if ((res).kind != Value::t_bool) { return TestResult(std::string(__FUNCTION__) + ": expected bool result"); }
#define EXPECT_TRUE(res) if ((res) != true) { return TestResult(__FUNCTION__); }
#define EXPECT_FALSE(res) if ((res) != false) { return TestResult(__FUNCTION__); }
#define PASS return TestResult(true)

