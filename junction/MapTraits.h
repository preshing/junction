/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_MAPTRAITS_H
#define JUNCTION_MAPTRAITS_H

#include <junction/Core.h>
#include <turf/Util.h>

namespace junction {

template <class T>
struct DefaultKeyTraits {
    typedef T Key;
    typedef typename turf::util::BestFit<T>::Unsigned Hash;
    static const Key NullKey = Key(0);
    static const Hash NullHash = Hash(0);
    static Hash hash(T key) {
        return turf::util::avalanche(Hash(key));
    }
    static Key dehash(Hash hash) {
        return (T) turf::util::deavalanche(hash);
    }
};

template <class T>
struct DefaultValueTraits {
    typedef T Value;
    typedef typename turf::util::BestFit<T>::Unsigned IntType;
    static const IntType NullValue = 0;
    static const IntType Redirect = 1;
};

} // namespace junction

#endif // JUNCTION_MAPTRAITS_H
