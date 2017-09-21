#include <memory>
#include <string>

#include <doctest.h>

#include "./base_object.h"
#include "./splash.h"

using namespace std;
using namespace Splash;

/*************/
class BaseObjectMock : public BaseObject
{
  public:
    BaseObjectMock(RootObject* root)
        : BaseObject(root)
    {
        registerAttributes();
    }

  private:
    int _integer{0};
    float _float{0.f};
    string _string{""};

    void registerAttributes()
    {
        addAttribute("integer",
            [&](const Values& args) {
                _integer = args[0].as<int>();
                return true;
            },
            [&]() -> Values { return {_integer}; },
            {'n'});

        addAttribute("float",
            [&](const Values& args) {
                _float = args[0].as<float>();
                return true;
            },
            [&]() -> Values { return {_float}; },
            {'n'});

        addAttribute("string",
            [&](const Values& args) {
                _string = args[0].as<string>();
                return true;
            },
            [&]() -> Values { return {_string}; },
            {'s'});
    }
};

/*************/
TEST_CASE("Testing BaseObject class")
{
    auto object = make_unique<BaseObjectMock>(nullptr);

    int integer_value = 42;
    float float_value = 3.1415;
    string string_value = "T'es sûr qu'on dit pas ouiche ?";
    Values array_value{1, 4.2, "sheraf"};

    object->setAttribute("integer", {integer_value});
    object->setAttribute("float", {float_value});
    object->setAttribute("string", {string_value});
    object->setAttribute("newAttribute", array_value);

    Values value;
    CHECK(object->getAttribute("integer", value) == true);
    CHECK(!value.empty());
    CHECK(value[0].as<int>() == 42);

    CHECK(object->getAttribute("float", value) == true);
    CHECK(!value.empty());
    CHECK(value[0].as<float>() == float_value);

    CHECK(object->getAttribute("string", value) == true);
    CHECK(!value.empty());
    CHECK(value[0].as<string>() == string_value);

    CHECK(object->getAttribute("newAttribute", value) == false);
    CHECK(!value.empty());
    CHECK(value == array_value);

    CHECK(object->getAttribute("inexistingAttribute", value) == false);
    CHECK(value.empty());
}