//
// Created by sstanf on 10/29/20.
//

#include "multicast.h"

#include <iostream>

using namespace std;
using namespace ndn;

namespace ahnd {

const uint64_t DISCOVERY_ROUTE_COST(0);
const time::milliseconds DISCOVERY_ROUTE_EXPIRATION = 30_s;

MulticastInterest::MulticastInterest(
    Face &face, std::shared_ptr<nfd::Controller> controller, Name prefix)
    : m_face(face), m_controller(std::move(controller)),
      m_prefix(std::move(prefix)) {
	m_ready = false;
	m_error = false;
}

void MulticastInterest::reset() {
	m_ready = false;
	m_error = false;
	nfd::FaceQueryFilter filter;
	filter.setLinkType(nfd::LINK_TYPE_MULTI_ACCESS);

	m_controller->fetch<nfd::FaceQueryDataset>(
	    filter, [this](auto &&dataset) { registerMultiPrefix(dataset); },
	    [this](uint32_t code, const std::string &reason) {
		    cout << "AHND (Multicast): Error " << to_string(code)
		         << " when querying multi-access faces: " << reason << endl;
		    m_error = true;
	    });
}

void MulticastInterest::expressInterest(const Interest &interest,
                                        const DataCallback &afterSatisfied,
                                        const NackCallback &afterNacked,
                                        const TimeoutCallback &afterTimeout) {
	if (m_ready) {
		m_face.expressInterest(interest, afterSatisfied, afterNacked,
		                       afterTimeout);
	} else {
		cout << "AHND (Multicast): Error, expressed interest before ready"
		     << endl;
		m_error = true;
	}
}

void MulticastInterest::requestReady() {
	m_ready = true;
	m_error = false;
}

void MulticastInterest::setStrategy() {
	nfd::ControlParameters parameters;
	parameters.setName(m_prefix).setStrategy(
	    "/localhost/nfd/strategy/multicast"),

	    m_controller->start<nfd::StrategyChoiceSetCommand>(
	        parameters,
	        [this](const ndn::nfd::ControlParameters &_) { requestReady(); },
	        [this](const nfd::ControlResponse &resp) {
		        cout << "AHND (Multicast): Error " << to_string(resp.getCode())
		             << " when setting multicast strategy: " << resp.getText()
		             << endl;
		        m_error = true;
	        });
}

void MulticastInterest::afterReg(int n_reg_success) {
	if (n_reg_success > 0) {
		setStrategy();
	} else {
		cout << "AHND (Multicast): Cannot register hub discovery prefix for "
		        "any face"
		     << endl;
		m_error = true;
	}
}

void MulticastInterest::registerMultiPrefix(
    const std::vector<nfd::FaceStatus> &dataset) {
	if (dataset.empty()) {
		cout << "AHND (Multicast): No multi-access faces available" << endl;
		m_error = true;
	}

	int n_regs = dataset.size();
	std::shared_ptr<int> n_reg_success = std::make_shared<int>(0);
	std::shared_ptr<int> n_reg_failure = std::make_shared<int>(0);

	for (const auto &face_status : dataset) {
		nfd::ControlParameters parameters;
		parameters.setName(m_prefix)
		    .setFaceId(face_status.getFaceId())
		    .setCost(DISCOVERY_ROUTE_COST)
		    .setExpirationPeriod(DISCOVERY_ROUTE_EXPIRATION);

		m_controller->start<nfd::RibRegisterCommand>(
		    parameters,
		    [this, n_reg_success, n_reg_failure,
		     n_regs](const nfd::ControlParameters &_) {
			    *n_reg_success += 1;
			    if (*n_reg_success + *n_reg_failure == n_regs) {
				    afterReg(*n_reg_success);
			    }
		    },
		    [this, n_reg_success, n_reg_failure, n_regs,
		     face_status](const nfd::ControlResponse &resp) {
			    std::cerr << "AHND (Multicast): Error " << resp.getCode()
			              << " when registering hub discovery prefix "
			              << "for face " << face_status.getFaceId() << " ("
			              << face_status.getRemoteUri()
			              << "): " << resp.getText() << std::endl;
			    *n_reg_failure += 1;
			    if (*n_reg_success + *n_reg_failure == n_regs) {
				    afterReg(*n_reg_success);
			    }
		    });
	}
}
} // namespace ahnd