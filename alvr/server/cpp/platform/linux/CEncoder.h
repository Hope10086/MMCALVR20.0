#pragma once

#include "alvr_server/IDRScheduler.h"
#include "shared/threadtools.h"
#include <atomic>
#include <memory>
#include <poll.h>
#include <sys/types.h>

class PoseHistory;

class CEncoder : public CThread {
  public:
    CEncoder(std::shared_ptr<PoseHistory> poseHistory);
    ~CEncoder();
    bool Init() override { return true; }
    void Run() override;

    void Stop();
    void OnPacketLoss();
    void InsertIDR();
    bool IsConnected() { return m_connected; }
    void CaptureFrame();
    void QpModeset();
    void RoiSizeset();
    void CentreSizeset();
    void CentreSizereset();
    void COF0set();
    void COF1set();
    void COF0reset();
    void COF1reset();
    void QPDistribution();


  private:
    void GetFds(int client, int (*fds)[6]);
    std::shared_ptr<PoseHistory> m_poseHistory;
    std::atomic_bool m_exiting{false};
    IDRScheduler m_scheduler;
    pollfd m_socket;
    std::string m_socketPath;
    int m_fds[6];
    bool m_connected = false;
    std::atomic_bool m_captureFrame = false;
    std::atomic_bool m_roisize = false;
    std::atomic_bool m_qpmodeset = false;
    std::atomic_bool m_roisize = false;
    std::atomic_bool m_cof0set= false;
    std::atomic_bool m_cof1set= false;
    std::atomic_bool m_cof0reset= false;
    std::atomic_bool m_cof1reset= false;
    std::atomic_bool m_QPDistribution= false;
};
