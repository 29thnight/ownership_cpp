#pragma once
#include <own/ownership.hpp>

own::shared_owner<int> make_in_first_translation_unit();
own::weak_owner<int> observe_in_second_translation_unit(const own::shared_owner<int>& value);
