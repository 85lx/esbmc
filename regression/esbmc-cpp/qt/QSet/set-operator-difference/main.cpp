// constructing QSets
#include <iostream>
#include <QSet>
#include <cassert>
using namespace std;

int main ()
{
    QSet<int> first;
    assert(first.size() == 0);
    QSet<int> second;
    second.insert(1);
    assert(first != second);
    return 0;
}
