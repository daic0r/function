#include "function.h"
#include <iostream>

int main() {
   ice::function<int()> task{ [j=205]() { std::cout << "Hello task: " << j << "\n"; return 5; } };

   auto cp = std::move(task);

   ice::function<void()> bigtask{ [ar=std::array<std::byte, 1024>{}]() { std::cout << "Big task!\n"; } };

   std::cout << cp() << std::endl;
   bigtask();
}
