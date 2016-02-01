/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#include <junction/Core.h>
#include <junction/Averager.h>
#include <math.h>

namespace junction {

double Averager::getStdDev() {
    finalize();
    double avg = getAverage();
    double dev = 0;
    for (ureg i = 0; i < m_values.size(); i++) {
        double diff = m_values[i] - avg;
        dev += diff * diff;
    }
    return sqrt(dev / m_values.size());
}

} // namespace junction
