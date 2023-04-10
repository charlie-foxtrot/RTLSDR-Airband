#include <gtest/gtest.h>

#include <stdlib.h> // mkdtemp()
#include <string>

#include "logging.h"
#include "squelch.h"

using namespace std;

class SquelchTest : public ::testing::Test
{
private:
	string temp_dir;
	string debug_filepath;

protected:
	void SetUp(void)
	{
		// setup debug file
		char temp_path_template[] = "/tmp/fileXXXXXX";
		if (mkdtemp(temp_path_template) == NULL) {
			cerr << "Error making temp dir for test files: " << strerror(errno) << endl;
			ASSERT_TRUE(false);
		}

		temp_dir = string(temp_path_template);

		debug_filepath = temp_dir + "/debug_file.log";
		init_debug(debug_filepath.c_str());
	}

	void TearDown(void)
	{
		close_debug();

		if (unlink(debug_filepath.c_str()) != 0) {
			cerr << "Error removing debug log file: " << strerror(errno) << endl;
		}

		if (rmdir(temp_dir.c_str()) != 0) {
			cerr << "Error removing temp dir for test files: " << strerror(errno) << endl;
		}

	}

};

TEST_F(SquelchTest, default_object)
{
	Squelch squelch;
	EXPECT_EQ(squelch.open_count(), 0);
}
