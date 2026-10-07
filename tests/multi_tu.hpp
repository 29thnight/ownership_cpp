#pragma once
#include <own/ownership.hpp>

own::unique_owner<int> make_unique_in_first_translation_unit();
own::unique_owner<const int> transfer_unique_in_second_translation_unit(own::unique_owner<int> value);

own::allocated_unique_owner<int> make_allocated_unique_in_first_translation_unit();
own::allocated_unique_owner<const int> transfer_allocated_unique_in_second_translation_unit(own::allocated_unique_owner<int> value);

own::shared_owner<int> make_in_first_translation_unit();
own::weak_owner<int> observe_in_second_translation_unit(const own::shared_owner<int>& value);

int read_borrow_in_second_translation_unit(own::local_view<const int> value);

struct multi_tu_self : own::enable_owner_from_this<multi_tu_self> { int value = 91; };
own::shared_owner<multi_tu_self> make_self_in_first_translation_unit();
own::shared_owner<multi_tu_self> retain_self_in_second_translation_unit(own::local_view<multi_tu_self> value);
