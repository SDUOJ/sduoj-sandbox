#include <iostream>
using namespace std;

const int N = 5000005;

long long a[N];

int main() {
    a[0] = a[1] = 1;
    for(int i = 2; i < N; ++i) {
        a[i] = a[i - 1] + a[i - 2];
    }
    for(int i = 0; i < N; ++i) {
        cout << a[i] << endl;
    }
}