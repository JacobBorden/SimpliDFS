#include "utilities/json.hpp"
#include <gtest/gtest.h>

TEST(JsonParser, ParseArray) {
  std::string src = "[\"one\", \"two\"]";
  JsonValue val = JsonParser::Parse(src);
  ASSERT_EQ(val.type, JsonValueType::Array);
  ASSERT_EQ(val.array_value.size(), 2u);
  EXPECT_EQ(val.array_value[0].string_value, "one");
  EXPECT_EQ(val.array_value[1].string_value, "two");
  EXPECT_EQ(val.ToString(), "[\"one\", \"two\"]");
}
