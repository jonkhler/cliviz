#include <gtest/gtest.h>

#include "term.h"

using namespace cliviz;

TEST(Term, GetSizeReturnsNonZeroInTerminal) {
    // In a CI/test environment, stdout may not be a terminal.
    // We just verify the function doesn't crash and returns a valid struct.
    auto size = term_get_size();
    // If running in a terminal, both should be > 0.
    // If not a terminal, both should be 0.
    EXPECT_EQ(size.cols == 0, size.rows == 0);
}

TEST(Term, InitFailsWhenNotATerminal) {
    // In test runner context, stdout is typically piped, not a terminal.
    // term_init should return false gracefully.
    // Note: if this test is somehow run in a real terminal, it would succeed
    // and mess up the terminal — so we only test the non-terminal path.
    if (!isatty(STDOUT_FILENO)) {
        EXPECT_FALSE(term_init());
        EXPECT_FALSE(term_is_active());
    } else {
        GTEST_SKIP() << "Skipping: stdout is a terminal";
    }
}

TEST(Term, ShutdownSafeToCallWithoutInit) {
    // Should not crash even if never initialized
    term_shutdown();
    EXPECT_FALSE(term_is_active());
}
