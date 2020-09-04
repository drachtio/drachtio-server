// Copyright 2019 Dmitry Arkhipov
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)


#include <string>
#include <iostream>

int main(int, char const** argv) {
  return QWERTY == std::string("UIOP") ? EXIT_SUCCESS : EXIT_FAILURE;
}
