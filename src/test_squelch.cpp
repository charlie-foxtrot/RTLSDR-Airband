/*
 * test_squelch.cpp
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
