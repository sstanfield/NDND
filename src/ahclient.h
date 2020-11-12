#ifndef AHND_AHCLIENT_H
#define AHND_AHCLIENT_H

#include <netinet/in.h>

#include "multicast.h"

namespace ahnd {

const int IP_BYTES = 16;

struct DBEntry {
	std::array<uint8_t, IP_BYTES> ip{};
	uint16_t port;
	ndn::Name prefix;
	int faceId;

	DBEntry() {
		ip.fill(0);
		port = 0;
		faceId = 0;
	}
};

class AHClient {
  public:
	AHClient(ndn::Name m_prefix, ndn::Name broadcast_prefix, int port);
	void registerPrefixes() { registerClientPrefix(); }
	void processEvents() { m_face.processEvents(); }
	void sendKeepAliveInterest();
	auto face() -> ndn::Face & { return m_face; }

  private:
	void appendIpPort(ndn::Name &name);
	auto hasEntry(const ndn::Name &name) -> bool;
	void registerClientPrefix();
	void registerKeepAlivePrefix();
	void registerArrivePrefix();
	void sendArrivalInterest();
	// Handle direct or multicast interests that contain a single remotes face
	// and route information.
	void onArriveInterest(const ndn::Interest &request, bool send_back);
	void registerRoute(const ndn::Name &route_name, int face_id, int cost,
	                   bool send_data);
	static void onNack(const ndn::Interest &interest,
	                   const ndn::lp::Nack &nack);
	static void onTimeout(const ndn::Interest &interest);
	void onRegisterRouteDataReply(const ndn::Interest &interest,
	                              const ndn::Data &data,
	                              const ndn::Name &route_name, int face_id,
	                              int cost, bool send_data);
	void onAddFaceDataReply(const ndn::Interest &interest,
	                        const ndn::Data &data, const std::string &uri,
	                        const ndn::Name &prefix, DBEntry entry,
	                        bool send_data);
	static void onDestroyFaceDataReply(const ndn::Interest &interest,
	                                   const ndn::Data &data);
	void addFaceAndPrefix(const std::string &uri, ndn::Name const &prefix,
	                      DBEntry const &entry, bool send_data);
	void removeRouteAndFace(const DBEntry &item);
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
	std::list<DBEntry> m_db;
};

} // namespace ahnd

#endif