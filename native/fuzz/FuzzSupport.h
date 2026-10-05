#pragma once

#include <cstdlib>
#include <exception>
#include <new>

// KAN-155: a parser may reject any input by throwing; that is the contract.
// Running out of memory is not: every parser bounds its allocations first.
template<class Parse> void fuzzParse(Parse &&parse)
{
    try {
        parse();
    } catch (const std::bad_alloc &) {
        std::abort();
    } catch (const std::exception &) {
    }
}
