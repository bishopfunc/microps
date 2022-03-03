#ifndef DUMMY_H
//重複定義を避ける if not defined
#define DUMMY_H
#include "net.h"

extern struct net_device*
dummy_init(void);
#endif