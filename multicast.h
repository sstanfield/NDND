//
// Created by sstanf on 10/29/20.
//

#ifndef AHND_MULTICAST_H
#define AHND_MULTICAST_H

#include <ndn-cxx/mgmt/nfd/controller.hpp>

namespace ahnd {

class MulticastInterest {
  private:
	ndn::Face &m_face;
	std::shared_ptr<ndn::nfd::Controller> m_controller;
	ndn::Name m_prefix;
	bool m_ready;
	bool m_error;

	void requestReady();
	void setStrategy();
	void afterReg(int n_reg_success);
	void registerMultiPrefix(const std::vector<ndn::nfd::FaceStatus> &dataset);

  public:
	MulticastInterest(ndn::Face &face,
	                  std::shared_ptr<ndn::nfd::Controller> controller,
	                  ndn::Name prefix);
	auto isReady() const { return m_ready; }
	auto isError() const { return m_error; }
	void expressInterest(const ndn::Interest &interest,
	                     const ndn::DataCallback &afterSatisfied,
	                     const ndn::NackCallback &afterNacked,
	                     const ndn::TimeoutCallback &afterTimeout);
};
} // namespace ahnd

#endif // NDND_MULTICAST_H
