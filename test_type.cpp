#include <iostream>
#include <typeinfo>
namespace proto { class TupleDictionary {}; }
int main() {
    std::cout << typeid(proto::TupleDictionary).name() << std::endl;
}
