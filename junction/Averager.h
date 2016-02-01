/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_AVERAGER_H
#define JUNCTION_AVERAGER_H

#include <junction/Core.h>
#include <vector>
#include <algorithm>

namespace junction {

class Averager {
private:
    std::vector<double> m_values;
    bool m_finalized;

public:
    Averager() : m_finalized(false) {
    }

    void add(double value) {
        TURF_ASSERT(!m_finalized);
        m_values.push_back(value);
    }

    ureg getNumValues() const {
        return m_values.size();
    }

    void finalize(ureg bestValueCount = 0) {
        if (!m_finalized) {
            std::sort(m_values.begin(), m_values.end());
            if (bestValueCount)
                m_values.resize(bestValueCount);
            m_finalized = true;
        }
    }

    double getAverage() {
        finalize();
        double sum = 0;
        for (ureg i = 0; i < m_values.size(); i++) {
            sum += m_values[i];
        }
        return sum / m_values.size();
    }

    double getStdDev();
};

} // namespace junction

#endif // JUNCTION_AVERAGER_H
