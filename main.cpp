#include "function.h"
#include <iostream>

int main() {
   ice::function<int()> task{ [j=205]() { std::cout << "Hello task: " << j << "\n"; return 5; } };

   auto cp = std::move(task);

   std::cout << cp() << std::endl;
}
