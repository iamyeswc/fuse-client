#ifndef TIMERCOUNTER_H
#define TIMERCOUNTER_H

#include <chrono>
#include <vector>
#include <cassert>
#include <mutex>
#include <numeric>
#include <cstring>

using SlideWindowUnit = std::chrono::seconds;

class TimerCounter final {
public:
    explicit TimerCounter(unsigned int interval = 60, unsigned int count = 60) :
        m_interval(interval),
        m_data(count, 0)
    {
        assert(interval > 0);
        assert(count > 0);
    }


    TimerCounter(const TimerCounter&) = delete;
    TimerCounter(const TimerCounter&&) = delete;
    TimerCounter& operator=(const TimerCounter&) = delete;
    TimerCounter& operator=(const TimerCounter&&) = delete;

    void reset()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        resetWithoutLock();
    }

    void add_count(unsigned int c)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        refresh_data();
        m_data[m_current] += c;
    }

    unsigned int get_sum_of_last_slices(unsigned int count)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        refresh_data();

        unsigned int sum = 0;
        if (count >= m_data.size())
        {
            sum = std::accumulate(m_data.begin(), m_data.end(), sum);
        }
        else
        {
            std::vector<unsigned int>::size_type pos = m_current;
            for (int i = 0; i < count; ++i)
            {
                sum += m_data[pos];
                pos = (pos - 1 + m_data.size()) % m_data.size();
            }
        }

        return sum;
    }

private:
    void resetWithoutLock()
    {
        memset(&m_data[0], 0, sizeof(m_data[0]) * m_data.size());
    }

    void refresh_data()
    {
        const SlideWindowUnit::rep now = std::chrono::duration_cast<SlideWindowUnit>(std::chrono::steady_clock::now().time_since_epoch()).count() / m_interval;
        const int delta = now - m_last;
        if (delta >= m_data.size())
        {
            resetWithoutLock();
        }
        else
        {
            for (int i = 0; i < delta; ++i)
            {
                m_current = (m_current + 1) % m_data.size();
                m_data[m_current] = 0;
            }
        }
        m_last = now;
    }


private:
    const unsigned int m_interval;

    std::vector<unsigned int> m_data;
    std::vector<unsigned int>::size_type m_current = 0;
    SlideWindowUnit::rep m_last = 0;

    std::mutex m_mutex;
};

#endif // TIMERCOUNTER_H

