//
// Created by sstanf on 12/11/20.
//

#ifndef AHNDN_STATUSINFO_H
#define AHNDN_STATUSINFO_H

#include <ndn-cxx/mgmt/nfd/controller.hpp>

namespace ahnd {

using StatusCallback = std::function<void(std::string json)>;
using StatusErrorCallback = std::function<void(std::string reason)>;

class StatusInfo {
  private:
	std::shared_ptr<ndn::nfd::Controller> m_controller;
	std::vector<ndn::nfd::FaceStatus> m_faces;
	std::unordered_map<long, std::vector<ndn::nfd::RibEntry>> m_ribs;

	void faceResults(const StatusCallback &callback,
	                 const StatusErrorCallback &errorCallback,
	                 const std::vector<ndn::nfd::FaceStatus> &dataset);
	void ribResults(const StatusCallback &callback,
	                const std::vector<ndn::nfd::RibEntry> &dataset);

  public:
	explicit StatusInfo(std::shared_ptr<ndn::nfd::Controller> controller);
	void getStatus(const StatusCallback &callback,
	               const StatusErrorCallback &errorCallback);
};
} // namespace ahnd

#endif // AHNDN_STATUSINFO_H
