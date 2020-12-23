#ifndef AHND_AHCLIENT_H
#define AHND_AHCLIENT_H

#include <netinet/in.h>

#include "multicast.h"
#include "statusinfo.h"

namespace ahnd {

struct DBEntry {
	const long id;
	struct in_addr ip {
		0
	};
	uint16_t port;
	ndn::Name prefix;
	int faceId;

	DBEntry() : id(count++) {
		port = 0;
		faceId = 0;
	}

  private:
	// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
	static long count;
};

using VisitPiersCallback = std::function<void(const DBEntry &pier)>;

class AHClient {
  public:
	AHClient(ndn::Name m_prefix, ndn::Name broadcast_prefix, int port);
	void registerPrefixes() { registerClientPrefix(); }
	void processEvents(long timeout_ms);
	void sendKeepAliveInterest();
	auto face() -> ndn::Face & { return m_face; }
	void shutdown();
	void getStatus(const StatusCallback &statusCallback,
	               const StatusErrorCallback &errorCallback) {
		m_statusinfo->getStatus(statusCallback, errorCallback);
	}
	void visitPiers(const VisitPiersCallback &callback);

  private:
	void appendIpPort(ndn::Name &name);
	auto newItem() -> DBEntry &;
	auto hasEntry(const ndn::Name &name) -> bool;
	void removeItem(const ndn::Name &name);
	void removeItem(const DBEntry &item);
	void registerClientPrefix();
	void registerKeepAlivePrefix();
	void registerPingPrefix();
	void registerArrivePrefix();
	void sendArrivalInterestInternal();
	void sendArrivalInterest();
	void sendDepartureInterestInternal();
	void sendDepartureInterest();
	// Handle direct or multicast interests that contain a single remotes face
	// and route information.
	void onArriveInterest(const ndn::Interest &request, bool send_back);
	void registerRoute(const ndn::Name &route_name, int face_id, int cost,
	                   bool send_data);
	static void onNack(const ndn::Interest &interest,
	                   const ndn::lp::Nack &nack);
	static void onTimeout(const ndn::Interest &interest);
	void sendData(const ndn::Name &route_name, int face_id, int count = 1);
	void onRegisterRouteDataReply(const ndn::Interest &interest,
	                              const ndn::Data &data,
	                              const ndn::Name &route_name, int face_id,
	                              int cost, bool send_data);
	void onAddFaceDataReply(const ndn::Interest &interest,
	                        const ndn::Data &data, const std::string &uri,
	                        const ndn::Name &prefix, DBEntry &entry,
	                        bool send_data);
	static void onDestroyFaceDataReply(const ndn::Interest &interest,
	                                   const ndn::Data &data, int face_id);
	void addFaceAndPrefix(const std::string &uri, ndn::Name const &prefix,
	                      DBEntry &entry, bool send_data);
	void removeRouteAndFace(const ndn::Name &prefix, int faceId);
	void destroyFace(int face_id);
	void setIP();

	ndn::Face m_face;
	ndn::KeyChain m_keyChain;
	std::shared_ptr<ndn::nfd::Controller> m_controller;
	ndn::Name m_prefix;
	ndn::Name m_broadcast_prefix;
	in_addr m_IP{0};
	std::unique_ptr<ndn::Scheduler> m_scheduler;
	uint16_t m_port;
	ndn::RegisteredPrefixHandle m_arrivePrefixId;
	std::unique_ptr<ahnd::MulticastInterest> m_multicast;
	std::unique_ptr<ahnd::StatusInfo> m_statusinfo;
	std::vector<DBEntry> m_db;
	std::vector<long> m_db_free;
};

} // namespace ahnd

#endif