/*
 * test_output.cpp
 *
 * Copyright (C) 2023 charlie-foxtrot
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "test_base_class.h"

#include "helper_functions.h"

using namespace std;

class HelperFunctionsTest : public TestBaseClass {
protected:

	void SetUp(void) {
		TestBaseClass::SetUp();
	}

	void create_file(const string &filepath) {
		fclose(fopen(filepath.c_str(), "wb"));
		EXPECT_TRUE(file_exists(filepath));
	}

};

TEST_F(HelperFunctionsTest, rename_file_if_exists_exists)
{
	string starting_path = temp_dir + "/starting_path";
	string ending_path = temp_dir + "/ending_path";
	create_file(starting_path);
	EXPECT_EQ(rename_file_if_exists(starting_path, ending_path), 0);
	EXPECT_FALSE(file_exists(starting_path));
	EXPECT_TRUE(file_exists(ending_path));
}

TEST_F(HelperFunctionsTest, rename_file_if_exists_doesnt_exist)
{
	string starting_path = temp_dir + "/starting_path";
	string ending_path = temp_dir + "/ending_path";
	EXPECT_EQ(rename_file_if_exists(starting_path, ending_path), 0);
	EXPECT_FALSE(file_exists(starting_path));
	EXPECT_FALSE(file_exists(ending_path));
}

TEST_F(HelperFunctionsTest, dir_exists_true)
{
	EXPECT_TRUE(dir_exists(temp_dir));
}

TEST_F(HelperFunctionsTest, dir_exists_false)
{
	EXPECT_FALSE(dir_exists("/not/a/real/dir"));
}

TEST_F(HelperFunctionsTest, dir_exists_not_dir)
{
	string file_in_dir = temp_dir + "/some_file";
	create_file(file_in_dir);
	EXPECT_FALSE(dir_exists(file_in_dir));
}

TEST_F(HelperFunctionsTest, file_exists_true)
{
	string file_in_dir = temp_dir + "/some_file";
	create_file(file_in_dir);
	EXPECT_TRUE(file_exists(file_in_dir));
}

TEST_F(HelperFunctionsTest, file_exists_false)
{
	EXPECT_FALSE(file_exists(temp_dir + "/nothing"));
}

TEST_F(HelperFunctionsTest, file_exists_not_file)
{
	EXPECT_FALSE(file_exists(temp_dir));
	EXPECT_TRUE(dir_exists(temp_dir));
}

TEST_F(HelperFunctionsTest, make_subdirs_exists)
{
	EXPECT_TRUE(make_subdirs(temp_dir, ""));
	EXPECT_TRUE(dir_exists(temp_dir));
}

TEST_F(HelperFunctionsTest, make_subdirs_one_subdir)
{
	EXPECT_TRUE(make_subdirs(temp_dir, "bob"));
	EXPECT_TRUE(dir_exists(temp_dir + "/bob"));
}

TEST_F(HelperFunctionsTest, make_subdirs_multiple_subdir)
{
	EXPECT_TRUE(make_subdirs(temp_dir, "bob/joe/sam"));
	EXPECT_TRUE(dir_exists(temp_dir + "/bob/joe/sam"));
}

TEST_F(HelperFunctionsTest, make_subdirs_file_in_the_way)
{
	string file_in_dir = temp_dir + "/some_file";
	create_file(file_in_dir);
	EXPECT_FALSE(make_subdirs(temp_dir, "some_file/some_dir"));
	EXPECT_FALSE(dir_exists(file_in_dir));
	EXPECT_TRUE(file_exists(file_in_dir));
}

TEST_F(HelperFunctionsTest, make_dated_subdirs_normal)
{
	struct tm time_struct;

	strptime("2010-3-7", "%Y-%m-%d", &time_struct);

	EXPECT_EQ(make_dated_subdirs(temp_dir, &time_struct), temp_dir + "/2010/03/07");
}

TEST_F(HelperFunctionsTest, make_dated_subdirs_fail)
{
	struct tm time_struct;

	strptime("2010-3-7", "%Y-%m-%d", &time_struct);

	EXPECT_EQ(make_dated_subdirs("/invalid/base/dir", &time_struct), "");
}
