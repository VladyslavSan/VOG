#pragma once

#include <memory>

#define VOG_DECLARE_PTR(TYPE)                                                                      \
    class TYPE;                                                                                    \
    using TYPE##Ptr = std::shared_ptr<TYPE>;