#include <iostream>
#include <QList>
#include <QString>
#include <cassert>
using namespace std;

int main ()
{
    QList<QString> list;
    list << "sun" << "cloud" << "sun" << "rain";
    list.removeOne("sun");
    assert(list.front() == "cloud");
    // list: ["cloud", ,"sun", "rain"]
  return 0;
}
