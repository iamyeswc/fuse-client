#include "FuseClient.h"
#include <chrono>
#include <thread>
#include <iostream>

const unsigned int FuseHttpClient::max_fuse_slide_window = 600;

FuseClient::FuseClient(const std::string &host, unsigned int port) :
    m_host(host),
    m_port(port),
    m_in_fuse_mode(false),
    m_inplace_retry_times(0),
    m_timeout(0),
    m_latency_timeout(std::numeric_limits<unsigned int>::max())
{
}

FuseClient::~FuseClient()
{
    if (m_recovery_thread.joinable())
    {
        m_in_fuse_mode = false;
        m_recovery_thread.join();
    }
}

void FuseClient::set_fuse(unsigned int slide_window,
                          unsigned int threshold,
                          unsigned int recovery_interval,
                          unsigned int recovery_threshold)
{
    if (silde_window == 0)
    {
        m_timer_counter.reset();
        LOGi1("Disable fuse mode for FuseHttpClient %s since the slide window is zero", destination().c_str());
        return;
    }

    if (silde_window > max_fuse_slide_window)
    {
        silde_window = max_fuse_slide_window;
        LOGi1("Max fuse slide window in second is %u", max_fuse_slide_window);
    }

    m_timer_counter.reset(new TimerCounter(1, silde_window));

    m_fuse_slide_window = silde_window;
    m_fuse_threshold = threshold;
    m_fuse_recovery_interval = recovery_interval;
    m_fuse_recovery_threshold = recovery_threshold;

    LOGd4("Fuse mode: slide_window[%u], threshold[%u], recovery_interval[%u], recovery_threshold[%u]",
          m_fuse_slide_window, m_fuse_threshold, m_fuse_recovery_interval, m_fuse_recovery_threshold);
}

void FuseClient::recovery_func()
{
    std::chrono::steady_clock::time_point next = std::chrono::steady_clock::now() + std::chrono::seconds(m_fuse_recovery_interval);

    unsigned int recovery_count = 0;
    while (m_in_fuse_mode)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (std::chrono::steady_clock::now() < next)
        {
            continue;
        }

        LOGd1("%s in fuse mode, try a test", destination().c_str());
        if (test())
        {
            ++recovery_count;
            LOGi2("%s %u times successful test", destination().c_str(), recovery_count);
            if (recovery_count >= m_fuse_recovery_threshold)
            {
                LOGi2("%s equal the threshold %u, leave fuse mode", destination().c_str(), m_fuse_recovery_threshold);
                if (m_timer_counter)
                {
                    m_timer_counter->reset();
                }
                m_in_fuse_mode = false;
            }
        }
        else
        {
            LOGx1("%s test failed", destination().c_str());
            recovery_count = 0;
        }

        next = std::chrono::steady_clock::now() + std::chrono::seconds(m_fuse_recovery_interval);
    }

    m_recovery_triggered->store(false);
}
