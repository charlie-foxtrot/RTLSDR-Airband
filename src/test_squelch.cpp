#include "test_base_class.h"

#include "logging.h"
#include "squelch.h"

using namespace std;

class SquelchTest : public TestBaseClass
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

TEST_F(SquelchTest, default_object)
{
	Squelch squelch;
	EXPECT_EQ(squelch.open_count(), 0);
}
