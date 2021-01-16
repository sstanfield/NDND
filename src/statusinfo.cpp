//
// Created by sstanf on 12/11/20.
//

#include "statusinfo.h"

#include <iostream>

using namespace std;
using namespace ndn;

namespace ahnd {

const uint64_t DISCOVERY_ROUTE_COST(0);
const time::milliseconds DISCOVERY_ROUTE_EXPIRATION = 30_s;

StatusInfo::StatusInfo(std::shared_ptr<nfd::Controller> controller)
    : m_controller(std::move(controller)) {}

void StatusInfo::getStatus(const StatusCallback &callback,
                           const StatusErrorCallback &errorCallback) {
	m_faces.clear();
	m_ribs.clear();
	nfd::FaceQueryFilter filter;
	// filter.setFaceScope(ndn::nfd::FACE_SCOPE_NON_LOCAL);

	m_controller->fetch<nfd::FaceQueryDataset>(
	    filter,
	    [this, callback, errorCallback](auto &&dataset) {
		    faceResults(callback, errorCallback, dataset);
	    },
	    [errorCallback](uint32_t code, const std::string &reason) {
		    errorCallback("Failed to query faces, reason: " + reason);
	    });
}

void StatusInfo::faceResults(const StatusCallback &callback,
                             const StatusErrorCallback &errorCallback,
                             const std::vector<nfd::FaceStatus> &dataset) {
	if (dataset.empty()) {
		errorCallback("No faces available.");
		return;
	}

	for (const auto &face_status : dataset) {
		m_faces.push_back(face_status);
	}
	m_controller->fetch<nfd::RibDataset>(
	    [this, callback](auto &&dataset) { ribResults(callback, dataset); },
	    [errorCallback](uint32_t code, const std::string &reason) {
		    errorCallback("Failed to query ribs, reason: " + reason);
	    });
}

void StatusInfo::ribResults(const StatusCallback &callback,
                            const std::vector<nfd::RibEntry> &dataset) {
	for (const auto &rib : dataset) {
		for (const auto &route : rib.getRoutes()) {
			const auto rib_list = m_ribs.find(route.getFaceId());
			if (rib_list == m_ribs.end()) {
				std::vector<nfd::RibEntry> v;
				v.push_back(rib);
				m_ribs[route.getFaceId()] = v;
			} else {
				rib_list->second.push_back(rib);
			}
		}
	}

	stringstream statusstr;
	statusstr << "[" << endl;
	int fi = 0;
	for (const auto &face_status : m_faces) {
		if (face_status.getFaceScope() !=
		    nfd::FaceScope::FACE_SCOPE_NON_LOCAL) {
			continue;
		}
		if (fi > 0) {
			statusstr << "," << endl;
		} else {
			statusstr << endl;
		}
		fi++;
		statusstr << "  {" << endl
		          << "    \"id\":" << face_status.getFaceId() << "," << endl
		          << R"(    "remote_uri":")" << face_status.getRemoteUri()
		          << "\"," << endl
		          << R"(    "local_uri":")" << face_status.getLocalUri()
		          << "\"," << endl
		          << R"(    "link_type":")" << face_status.getLinkType()
		          << "\"," << endl
		          << R"(    "face_scope":")" << face_status.getFaceScope()
		          << "\"," << endl
		          << R"(    "face_persistency":")"
		          << face_status.getFacePersistency() << R"(",)" << endl
		          << R"(    "flags":)" << face_status.getFlags() << "," << endl
		          << R"(    "in_interests":)" << face_status.getNInInterests()
		          << "," << endl
		          << R"(    "out_interests":)" << face_status.getNOutInterests()
		          << "," << endl
		          << R"(    "in_bytes":)" << face_status.getNInBytes() << ","
		          << endl
		          << R"(    "out_bytes":)" << face_status.getNOutBytes() << ","
		          << endl
		          << R"(    "in_data":)" << face_status.getNInData() << ","
		          << endl
		          << R"(    "out_data":)" << face_status.getNOutData() << ","
		          << endl
		          << R"(    "in_nacks":)" << face_status.getNInNacks() << ","
		          << endl
		          << R"(    "out_nacks":)" << face_status.getNOutNacks() << ","
		          << endl;
		if (face_status.hasMtu()) {
			statusstr << R"(    "mtu":)" << face_status.getMtu() << "," << endl;
		}
		if (face_status.hasDefaultCongestionThreshold()) {
			statusstr << R"(    "default_congestion_threshold":)"
			          << face_status.getDefaultCongestionThreshold() << ","
			          << endl;
		}
		if (face_status.hasBaseCongestionMarkingInterval()) {
			statusstr << R"(    "default_base_congestion_marking_interval_ns":)"
			          << face_status.getBaseCongestionMarkingInterval().count()
			          << "," << endl;
		}
		if (face_status.hasExpirationPeriod()) {
			statusstr << R"(    "expiration_period_ms":)"
			          << face_status.getExpirationPeriod().count() << ","
			          << endl;
		}
		const auto rib_list = m_ribs.find(face_status.getFaceId());
		if (rib_list != m_ribs.end()) {
			statusstr << R"(    "routes":[)";
			int ri = 0;
			for (const auto &rib : rib_list->second) {
				if (ri > 0) {
					statusstr << "," << endl;
				} else {
					statusstr << endl;
				}
				ri++;
				statusstr << R"(      {"name":")" << rib.getName() << R"(",)";
				for (const auto &route : rib.getRoutes()) {
					if (route.getFaceId() == face_status.getFaceId()) {
						statusstr << R"("origin":")" << route.getOrigin()
						          << R"(",)";
						statusstr << R"("cost":)" << route.getCost() << ",";
						if (route.hasExpirationPeriod()) {
							statusstr << R"("expiration_period_ms":)"
							          << route.getExpirationPeriod().count()
							          << ",";
						}
						statusstr << R"("flags":)" << route.getFlags() << "}";
					}
				}
			}
			statusstr << endl << "    ]" << endl;
		} else {
			statusstr << R"(    "routes":[])" << endl;
		}
		statusstr << "  }";
	}
	statusstr << endl << "]"; // << endl;
	callback(statusstr.str());
}
} // namespace ahnd
