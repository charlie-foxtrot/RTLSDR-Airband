#include "test_base_class.h"

#include "filters.h"

using namespace std;

class FiltersTest : public TestBaseClass
{
protected:
	void SetUp(void)
	{
		TestBaseClass::SetUp();
	}

	void TearDown(void)
	{
		TestBaseClass::TearDown();
	}
};

TEST_F(FiltersTest, default_notch)
{
	NotchFilter notch;
	EXPECT_FALSE(notch.enabled());
}

TEST_F(FiltersTest, default_lowpass)
{
	LowpassFilter lowpass;
	EXPECT_FALSE(lowpass.enabled());
}
