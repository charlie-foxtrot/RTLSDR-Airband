#include <gtest/gtest.h>

#include "squelch.h"

class SquelchTest : public ::testing::Test
{
protected:
	void SetUp(void)
	{
	}

	void TearDown(void)
	{
	}
};

TEST_F(SquelchTest, default_object)
{
	Squelch squelch;
	EXPECT_EQ(squelch.open_count(), 0);
}
