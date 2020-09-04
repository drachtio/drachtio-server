// Copyright 2019 Dmitry Arkhipov
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)


#include <string>

int main() {
  return QWERTY == std::string("uiop") ? EXIT_SUCCESS : EXIT_FAILURE ;
}
