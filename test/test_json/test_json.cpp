#include "json.hpp"
using json = nlohmann::json;

#include <iostream>
#include <vector>
#include <map>
using namespace std;


void func1() {
    json js;
    js["to"] = "me";
    cout << js << endl;
}

int main() {
    func1();
}