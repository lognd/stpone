#pragma once
#define ATOMIC_RESTORESTATE 0
#define ATOMIC_BLOCK(x) for (int once_ = 1; once_; once_ = 0)
