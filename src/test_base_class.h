#ifndef _TEST_BASE_CLASS_H
#define _TEST_BASE_CLASS_H

#include <gtest/gtest.h>

#include <string>

class TestBaseClass : public ::testing::Test
{
protected:
	void SetUp(void);
	void TearDown(void);

private:
	std::string temp_dir;
};

#endif /* _TEST_BASE_CLASS_H */
