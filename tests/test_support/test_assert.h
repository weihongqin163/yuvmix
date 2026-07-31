#ifndef YUVMIX_TEST_SUPPORT_TEST_ASSERT_H_
#define YUVMIX_TEST_SUPPORT_TEST_ASSERT_H_

#include <iostream>

namespace yuvmix_test {

inline int& FailureCount() {
    static int failures = 0;
    return failures;
}

inline void Expect(bool condition, const char* expression,
                   const char* file, int line) {
    if (!condition) {
        ++FailureCount();
        std::cerr << file << ':' << line << ": expectation failed: "
                  << expression << '\n';
    }
}

inline int Finish() {
    return FailureCount() == 0 ? 0 : 1;
}

}  // namespace yuvmix_test

#define EXPECT_TRUE(expression) \
    ::yuvmix_test::Expect((expression), #expression, __FILE__, __LINE__)
#define EXPECT_FALSE(expression) EXPECT_TRUE(!(expression))
#define EXPECT_EQ(actual, expected) EXPECT_TRUE((actual) == (expected))
#define EXPECT_NE(actual, expected) EXPECT_TRUE((actual) != (expected))

#endif  // YUVMIX_TEST_SUPPORT_TEST_ASSERT_H_
