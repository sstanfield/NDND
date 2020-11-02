#ifndef AHND_AHCLIENT_H
#define AHND_AHCLIENT_H

#include <netinet/in.h>

#include "multicast.h"

namespace ahnd {

struct DBEntry {
	uint8_t ip[16];
	uint16_t port;
	ndn::Name prefix;
	int faceId;
};

class AHClient {
  public:
	AHClient(const ndn::Name &m_prefix, const ndn::Name &broadcast_prefix);
	void registerPrefixes() { registerClientPrefix(); }
	void processEvents() { m_face.processEvents(); }
	void sendKeepAliveInterest();
	ndn::Face &face() { return m_face; }

  private:
	bool hasEntry(const ndn::Name &name);
	void registerClientPrefix();
	void registerKeepAlivePrefix();
	void registerArrivePrefix();
	void sendArrivalInterest();
	// Handle direct or multicast interests that contain a single remotes face
	// and route information.
	void onArriveInterest(const ndn::Interest &request, const bool send_back);
	void registerRoute(const ndn::Name &route_name, int face_id, int cost,
	                   const bool send_data);
	void onSubInterest(const ndn::Interest &subInterest);
	void onNack(const ndn::Interest &interest, const ndn::lp::Nack &nack);
	void onTimeout(const ndn::Interest &interest);
	void onRegisterRouteDataReply(const ndn::Interest &interest,
	                              const ndn::Data &data,
	                              const ndn::Name &route_name, int face_id,
	                              int cost, const bool send_data);
	void onAddFaceDataReply(const ndn::Interest &interest,
	                        const ndn::Data &data, const std::string &uri,
	                        const ndn::Name prefix, DBEntry entry,
	                        const bool send_data);
	void onDestroyFaceDataReply(const ndn::Interest &interest,
	                            const ndn::Data &data);
	void addFaceAndPrefix(const std::string &uri, const ndn::Name prefix,
	                      DBEntry entry, const bool send_data);
	void destroyFace(int face_id);
	void onSetStrategyDataReply(const ndn::Interest &interest,
	                            const ndn::Data &data);
	void setStrategy(const std::string &uri, const std::string &strategy);
	void setIP();

	ndn::Face m_face;
	ndn::KeyChain m_keyChain;
	std::shared_ptr<ndn::nfd::Controller> m_controller;
	ndn::Name m_prefix;
	ndn::Name m_broadcast_prefix;
	in_addr m_IP;
	in_addr m_submask;
	std::unique_ptr<ndn::Scheduler> m_scheduler;
	uint16_t m_port;
	uint8_t m_buffer[4096];
	size_t m_len;
	ndn::RegisteredPrefixHandle m_arrivePrefixId;
	std::unique_ptr<ahnd::MulticastInterest> m_multicast;
	std::list<DBEntry> m_db;
};

} // namespace ahnd

#endif