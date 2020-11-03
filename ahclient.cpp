#include "ahclient.hpp"
#include "nfd-command-tlv.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <iostream>
#include <netdb.h>

using namespace ndn;
using namespace std;

const ndn::time::milliseconds SERVER_DISCOVERY_INTEREST_LIFETIME = 4_s;

static ndn::Block makeRibInterestParameter(const ndn::Name &route_name,
                                           int face_id) {
	auto block = ndn::makeEmptyBlock(CONTROL_PARAMETERS);
	ndn::Block route_name_block = route_name.wireEncode();
	ndn::Block face_id_block =
	    ndn::makeNonNegativeIntegerBlock(FACE_ID, face_id);
	ndn::Block origin_block = ndn::makeNonNegativeIntegerBlock(ORIGIN, 0xFF);
	ndn::Block cost_block = ndn::makeNonNegativeIntegerBlock(COST, 0);
	ndn::Block flags_block = ndn::makeNonNegativeIntegerBlock(FLAGS, 0x01);

	block.push_back(route_name_block);
	block.push_back(face_id_block);
	block.push_back(origin_block);
	block.push_back(cost_block);
	block.push_back(flags_block);

	std::cerr << "Route name block:" << std::endl;
	std::cerr << route_name_block << std::endl;
	std::cerr << "Face id block:" << std::endl;
	std::cerr << face_id_block << std::endl;
	std::cerr << "Control parameters block:" << std::endl;
	std::cerr << block << std::endl;
	block.encode();
	return block;
}

static ndn::Interest prepareRibRegisterInterest(const ndn::Name &route_name,
                                                int face_id,
                                                ndn::KeyChain &keychain,
                                                int cost = 0) {
	ndn::Name name("/localhost/nfd/rib/register");
	ndn::Block control_params = makeRibInterestParameter(route_name, face_id);
	name.append(control_params);

	ndn::security::CommandInterestSigner signer(keychain);
	ndn::Interest interest = signer.makeCommandInterest(name);
	interest.setMustBeFresh(true);
	interest.setCanBePrefix(false);
	return interest;
}

static ndn::Interest prepareFaceCreationInterest(const std::string &uri,
                                                 ndn::KeyChain &keychain) {
	ndn::Name name("/localhost/nfd/faces/create");
	auto control_block = ndn::makeEmptyBlock(CONTROL_PARAMETERS);
	control_block.push_back(ndn::makeStringBlock(URI, uri));
	control_block.encode();
	name.append(control_block);

	ndn::security::CommandInterestSigner signer(keychain);
	ndn::Interest interest = signer.makeCommandInterest(name);
	interest.setMustBeFresh(true);
	interest.setCanBePrefix(false);
	return interest;
}

static ndn::Interest prepareFaceDestroyInterest(int face_id,
                                                ndn::KeyChain &keychain) {
	ndn::Name name("/localhost/nfd/faces/destroy");
	auto control_block = ndn::makeEmptyBlock(CONTROL_PARAMETERS);
	control_block.push_back(ndn::makeNonNegativeIntegerBlock(FACE_ID, face_id));
	control_block.encode();
	name.append(control_block);

	ndn::security::CommandInterestSigner signer(keychain);
	ndn::Interest interest = signer.makeCommandInterest(name);
	interest.setMustBeFresh(true);
	interest.setCanBePrefix(false);
	return interest;
}

static ndn::Interest prepareStrategySetInterest(const std::string &prefix,
                                                const std::string &strategy,
                                                ndn::KeyChain &keychain) {
	ndn::Name name("/localhost/nfd/strategy-choice/set");

	auto prefix_block = ndn::Name(prefix).wireEncode();
	auto strategy_block = ndn::makeEmptyBlock(STRATEGY);
	strategy_block.push_back(ndn::Name(strategy).wireEncode());

	auto control_block = ndn::makeEmptyBlock(CONTROL_PARAMETERS);
	control_block.push_back(prefix_block);
	control_block.push_back(strategy_block);
	control_block.encode();
	name.append(control_block);

	ndn::security::CommandInterestSigner signer(keychain);
	ndn::Interest interest = signer.makeCommandInterest(name);
	interest.setMustBeFresh(true);
	interest.setCanBePrefix(false);
	return interest;
}

namespace ahnd {

AHClient::AHClient(const Name &prefix, const Name &broadcast_prefix)
    : m_prefix(prefix), m_broadcast_prefix(broadcast_prefix) {
	m_scheduler = make_unique<Scheduler>(m_face.getIoService());
	m_controller = std::make_shared<nfd::Controller>(m_face, m_keyChain);
	setIP();
	m_port = htons(6363); // default
	m_multicast = std::make_unique<MulticastInterest>(m_face, m_controller,
	                                                  m_broadcast_prefix);
}

bool AHClient::hasEntry(const Name &name) {
	for (auto it = m_db.begin(); it != m_db.end();) {
		bool is_prefix = it->prefix.isPrefixOf(name);
		if (is_prefix) {
			return true;
		}
		++it;
	}
	return false;
}

void AHClient::registerClientPrefix() {
	Name name(m_prefix);
	name.append("nd-info");
	cout << "AH Client: Registering Client Prefix: " << name << endl;
	m_face.setInterestFilter(
	    InterestFilter(name),
	    bind(&AHClient::onArriveInterest, this, _2, false),
	    [this](const Name &name) {
		    std::cout << "AH Client: Registered client prefix " << name.toUri()
		              << std::endl;
		    // Now register keep alive prefix.
		    registerKeepAlivePrefix();
	    },
	    [this](const Name &name, const std::string &error) {
		    std::cout << "AH Client: Failed to register client prefix "
		              << name.toUri() << " reason: " << error << std::endl;
		    m_scheduler->schedule(time::seconds(3),
		                          [this] { registerClientPrefix(); });
	    });
}

void AHClient::registerKeepAlivePrefix() {
	Name name(m_prefix);
	name.append("nd-keepalive");
	cout << "AH Client: Registering KeepAlive Prefix: " << name << endl;
	m_face.setInterestFilter(
	    InterestFilter(name),
	    [this](const InterestFilter &filter, const Interest &request) {
		    cout << "AH Client: Got keep alive " << endl;
		    auto data = make_shared<Data>(request.getName());
		    m_keyChain.sign(*data,
		                    security::SigningInfo(
		                        security::SigningInfo::SIGNER_TYPE_SHA256));
		    data->setFreshnessPeriod(time::milliseconds(4000));
		    m_face.put(*data);
	    },
	    [this](const Name &name) {
		    std::cout << "AH Client: Registered client prefix " << name.toUri()
		              << std::endl;
		    // Now register broadcast prefix.
		    registerArrivePrefix();
	    },
	    [this](const Name &name, const std::string &error) {
		    std::cout << "AH Client: Failed to register client prefix "
		              << name.toUri() << " reason: " << error << std::endl;
		    m_scheduler->schedule(time::seconds(3),
		                          [this] { registerKeepAlivePrefix(); });
	    });
}

void AHClient::registerArrivePrefix() {
	std::cout << "AH Client: Registering arrive prefix "
	          << m_broadcast_prefix.toUri() << std::endl;
	m_arrivePrefixId = m_face.setInterestFilter(
	    InterestFilter(m_broadcast_prefix),
	    bind(&AHClient::onArriveInterest, this, _2, true),
	    [this](const Name &name) {
		    std::cout << "AH Client: Registered arrive prefix " << name.toUri()
		              << std::endl;
		    // Send our multicast arrive interest.
		    sendArrivalInterest();
	    },
	    [this](const Name &name, const std::string &error) {
		    std::cout << "AH Client: Failed to register arrive prefix "
		              << name.toUri() << " reason: " << error << std::endl;
		    m_scheduler->schedule(time::seconds(3),
		                          [this] { registerArrivePrefix(); });
	    });
}

void AHClient::sendArrivalInterest() {
	if (m_multicast->isError()) {
		cout << "AH Client: Multicast error, exiting" << endl;
		exit(1);
	}
	if (m_multicast->isReady()) {
		Name name(m_broadcast_prefix);
		name.append("arrival");
		name.append((uint8_t *)&m_IP, sizeof(m_IP))
		    .append((uint8_t *)&m_port, sizeof(m_port));
		name.appendNumber(m_prefix.size()).append(m_prefix).appendTimestamp();

		Interest interest(name);
		interest.setInterestLifetime(SERVER_DISCOVERY_INTEREST_LIFETIME);
		interest.setMustBeFresh(true);
		interest.setNonce(4);
		// interest.setCanBePrefix(false);
		interest.setCanBePrefix(true);

		cout << "AH Client: Arrival Interest: " << interest << endl;

		m_multicast->expressInterest(
		    interest,
		    [](const Interest &interest, const Data &data) {
			    // Since this is multicast and we are
			    // listening, this will almost always be from 'us',
			    // Remotes will send an interest to the client prefix.
			    cout << "AH Client: Arrive data " << interest.getName() << endl;
		    },
		    [this](const Interest &interest, const lp::Nack &nack) {
			    // Humm, log this and retry...
			    std::cout << "AH Client: received Nack with reason "
			              << nack.getReason() << " for interest " << interest
			              << std::endl;
			    m_scheduler->schedule(time::seconds(3),
			                          [this] { sendArrivalInterest(); });
		    },
		    [](const Interest &interest) {
			    // This is odd (we should get a packet from ourselves)...
			    std::cout << "AH Client: Arrive Timeout (I am all alone?) "
			              << interest << std::endl;
		    });
	} else {
		cout << "AH Client: Arrival Interest, multicast not ready will retry"
		     << endl;
		m_scheduler->schedule(time::seconds(3),
		                      [this] { sendArrivalInterest(); });
	}
}

// Handle direct or multicast interests that contain a single remotes face and
// route information.
void AHClient::onArriveInterest(const Interest &request, const bool send_back) {
	// First setup the face and route the pier.
	Name name = request.getName();
	uint8_t ip[16];
	uint16_t port;
	char ip_str[256];
	for (unsigned int i = 0; i < name.size(); i++) {
		Name::Component component = name.get(i);
		bool ret = (component.compare(Name::Component("arrival")) == 0) ||
		           (component.compare(Name::Component("nd-info")) == 0);
		if (ret) {
			Name::Component comp;
			// getIP
			comp = name.get(i + 1);
			memcpy(ip, comp.value(), sizeof(ip));
			char *t_ip = inet_ntoa(*(in_addr *)(ip));
			int ip_len = strlen(t_ip) + 1;
			memcpy(ip_str, t_ip, ip_len > 255 ? 255 : ip_len);
			// getPort
			comp = name.get(i + 2);
			memcpy(&port, comp.value(), sizeof(port));
			// getName
			comp = name.get(i + 3);
			int begin = i + 3;
			Name prefix;
			uint64_t name_size = comp.toNumber();
			for (unsigned int j = 0; j < name_size; j++) {
				prefix.append(name.get(begin + j + 1));
			}

			std::stringstream ss;
			ss << "udp4://" << ip_str << ':' << ntohs(port);
			auto ss_str = ss.str();
			std::cout << "AH Client: Arrival Name is " << prefix.toUri()
			          << " from " << ss_str << std::endl;

			// Send back empty data to confirm I am here...
			auto data = make_shared<Data>(request.getName());
			m_keyChain.sign(*data,
			                security::SigningInfo(
			                    security::SigningInfo::SIGNER_TYPE_SHA256));
			data->setFreshnessPeriod(time::milliseconds(4000));
			m_face.put(*data);
			// Do not register route to myself
			if (strcmp(ip_str, inet_ntoa(m_IP)) == 0) {
				cout << "AH Client: My IP address returned - send back nothing"
				     << endl;
				continue;
			}
			DBEntry entry;
			memcpy(entry.ip, ip, sizeof(ip));
			entry.port = port;
			entry.prefix = prefix;
			addFaceAndPrefix(ss_str, prefix, entry, send_back);
		}
	}
}

void AHClient::registerRoute(const Name &route_name, int face_id, int cost,
                             const bool send_data) {
	Interest interest =
	    prepareRibRegisterInterest(route_name, face_id, m_keyChain, cost);
	m_face.expressInterest(
	    interest,
	    bind(&AHClient::onRegisterRouteDataReply, this, _1, _2, route_name,
	         face_id, cost, send_data),
	    [this, route_name, face_id, cost, send_data](const Interest &interest,
	                                                 const lp::Nack &nack) {
		    std::cout << "AH Client: Received Nack with reason "
		              << nack.getReason() << " for interest " << interest
		              << std::endl;
		    m_scheduler->schedule(
		        time::seconds(3), [this, route_name, face_id, cost, send_data] {
			        registerRoute(route_name, face_id, cost, send_data);
		        });
	    },
	    [this, route_name, face_id, cost, send_data](const Interest &interest) {
		    std::cout << "AH Client: Received timeout for interest " << interest
		              << std::endl;
		    // XXX TODO- this may be wrong, may want to unwind the route and
		    // face on timeout here- also see nack above.
		    // removeRoute(findEntry(interest.getName()));
		    m_scheduler->schedule(
		        time::seconds(3), [this, route_name, face_id, cost, send_data] {
			        registerRoute(route_name, face_id, cost, send_data);
		        });
	    });
}

void AHClient::onSubInterest(const Interest &subInterest) {
	// reply data with IP confirmation
	Buffer content_buf;
	for (unsigned int i = 0; i < sizeof(m_IP); i++) {
		content_buf.push_back(*((uint8_t *)&m_IP + i));
	}

	auto data = make_shared<Data>(subInterest.getName());
	if (content_buf.size() > 0) {
		data->setContent(content_buf.get<uint8_t>(), content_buf.size());
	} else {
		return;
	}

	m_keyChain.sign(*data, security::SigningInfo(
	                           security::SigningInfo::SIGNER_TYPE_SHA256));
	// security::SigningInfo signInfo(security::SigningInfo::SIGNER_TYPE_ID,
	// m_options.identity); m_keyChain.sign(*m_data, signInfo);
	data->setFreshnessPeriod(time::milliseconds(4000));
	m_face.put(*data);
	cout << "AH Client: Publishing Data: " << *data << endl;
}

void AHClient::sendKeepAliveInterest() {
	for (auto it = m_db.begin(); it != m_db.end(); it = m_db.erase(it)) {
		const DBEntry item = *it;
		Name name(item.prefix);
		name.append("nd-keepalive");
		name.appendTimestamp();
		Interest interest(name);
		interest.setInterestLifetime(30_s);
		interest.setMustBeFresh(true);
		interest.setNonce(4);
		interest.setCanBePrefix(false);

		m_face.expressInterest(
		    interest,
		    [this, item](const Interest &interest, const Data &data) {
			    cout << "AH Client: Arrive keep alive data "
			         << interest.getName() << endl;
			    m_db.push_back(item);
		    },
		    [](const Interest &interest, const lp::Nack &nack) {
			    // Humm, log this and retry...
			    std::cout << "AH Client: received keep alive Nack with reason "
			              << nack.getReason() << " for interest " << interest
			              << std::endl;
			    // XXX TODO- shutdown face/route.
		    },
		    [](const Interest &interest) {
			    // This is odd (we should get a packet from ourselves)...
			    std::cout << "AH Client: Keep alive timeout " << interest
			              << std::endl;
			    // XXX TODO- shutdown face/route.
		    });
	}
}

void AHClient::onNack(const Interest &interest, const lp::Nack &nack) {
	std::cout << "AH Client: received Nack with reason " << nack.getReason()
	          << " for interest " << interest << std::endl;
}

void AHClient::onTimeout(const Interest &interest) {
	std::cout << "AH Client: Timeout " << interest << std::endl;
}

void AHClient::onRegisterRouteDataReply(const Interest &interest,
                                        const Data &data,
                                        const Name &route_name, int face_id,
                                        int cost, const bool send_data) {
	Block response_block = data.getContent().blockFromValue();
	response_block.parse();

	std::cout << response_block << std::endl;

	Block status_code_block = response_block.get(STATUS_CODE);
	Block status_text_block = response_block.get(STATUS_TEXT);
	short response_code = readNonNegativeIntegerAs<int>(status_code_block);
	char response_text[1000] = {0};
	memcpy(response_text, status_text_block.value(),
	       status_text_block.value_size());

	if (response_code == OK) {

		Block control_params = response_block.get(CONTROL_PARAMETERS);
		control_params.parse();

		Block name_block = control_params.get(ndn::tlv::Name);
		Name route_name(name_block);
		Block face_id_block = control_params.get(FACE_ID);
		int face_id = readNonNegativeIntegerAs<int>(face_id_block);
		Block origin_block = control_params.get(ORIGIN);
		int origin = readNonNegativeIntegerAs<int>(origin_block);
		Block route_cost_block = control_params.get(COST);
		int route_cost = readNonNegativeIntegerAs<int>(route_cost_block);
		Block flags_block = control_params.get(FLAGS);
		int flags = readNonNegativeIntegerAs<int>(flags_block);

		std::cout << "\nRegistration of route succeeded:" << std::endl;
		std::cout << "Status text: " << response_text << std::endl;

		std::cout << "Route name: " << route_name.toUri() << std::endl;
		std::cout << "Face id: " << face_id << std::endl;
		std::cout << "Origin: " << origin << std::endl;
		std::cout << "Route cost: " << route_cost << std::endl;
		std::cout << "Flags: " << flags << std::endl;
		if (send_data) {
			// Then send back our info.
			Name prefix(route_name);
			prefix.append("nd-info");
			prefix.append((uint8_t *)&m_IP, sizeof(m_IP))
			    .append((uint8_t *)&m_port, sizeof(m_port));
			prefix.appendNumber(m_prefix.size())
			    .append(m_prefix)
			    .appendTimestamp();

			std::cout << "AH Client: Subscribe Back to " << prefix << std::endl;
			Interest interest(prefix);
			interest.setInterestLifetime(30_s);
			interest.setMustBeFresh(true);
			interest.setNonce(4);
			interest.setCanBePrefix(false);

			m_face.expressInterest(
			    interest,
			    [](const Interest &interest, const Data &data) {
				    std::cout << "AH Client: Record Updated/Confirmed from "
				              << data.getName() << std::endl;
			    },
			    [this, route_name, face_id, cost,
			     send_data](const Interest &interest, const lp::Nack &nack) {
				    std::cout << "AH Client: Received Nack with reason "
				              << nack.getReason() << " for interest "
				              << interest << std::endl;
				    m_scheduler->schedule(
				        time::seconds(3),
				        [this, route_name, face_id, cost, send_data] {
					        registerRoute(route_name, face_id, cost, send_data);
				        });
			    },
			    [this, route_name, face_id, cost,
			     send_data](const Interest &interest) {
				    std::cout << "AH Client: Received timeout for interest "
				              << interest << std::endl;
				    // XXX removeRoute(findEntry(interest.getName()));
				    m_scheduler->schedule(
				        time::seconds(3),
				        [this, route_name, face_id, cost, send_data] {
					        registerRoute(route_name, face_id, cost, send_data);
				        });
			    });
		}
	} else {
		std::cout << "\nRegistration of route failed." << std::endl;
		std::cout << "Status text: " << response_text << std::endl;
		m_scheduler->schedule(
		    time::seconds(3), [this, route_name, face_id, cost, send_data] {
			    registerRoute(route_name, face_id, cost, send_data);
		    });
	}
}

void AHClient::onAddFaceDataReply(const Interest &interest, const Data &data,
                                  const string &uri, const Name prefix,
                                  DBEntry entry, const bool send_data) {
	short response_code;
	char response_text[1000] = {0};
	int face_id; // Store faceid for deletion of face
	Block response_block = data.getContent().blockFromValue();
	response_block.parse();

	Block status_code_block = response_block.get(STATUS_CODE);
	Block status_text_block = response_block.get(STATUS_TEXT);
	response_code = readNonNegativeIntegerAs<int>(status_code_block);
	memcpy(response_text, status_text_block.value(),
	       status_text_block.value_size());

	// Get FaceId for future removal of the face
	if (response_code == OK || response_code == FACE_EXISTS) {
		Block status_parameter_block = response_block.get(CONTROL_PARAMETERS);
		status_parameter_block.parse();
		Block face_id_block = status_parameter_block.get(FACE_ID);
		face_id = readNonNegativeIntegerAs<int>(face_id_block);
		std::cout << response_code << " " << response_text
		          << ": Added Face (FaceId: " << face_id << "): " << uri
		          << std::endl;

		entry.faceId = face_id;
		if (!hasEntry(entry.prefix)) {
			m_db.push_back(entry);
		}
		registerRoute(prefix, face_id, 0, send_data);
	} else {
		std::cout << "\nCreation of face failed." << std::endl;
		std::cout << "Status text: " << response_text << std::endl;
		m_scheduler->schedule(
		    time::seconds(3), [this, uri, prefix, entry, send_data] {
			    addFaceAndPrefix(uri, prefix, entry, send_data);
		    });
	}
}

void AHClient::onDestroyFaceDataReply(const Interest &interest,
                                      const Data &data) {
	short response_code;
	char response_text[1000] = {0};
	char buf[1000] = {0}; // For parsing
	int face_id;
	Block response_block = data.getContent().blockFromValue();
	response_block.parse();

	Block status_code_block = response_block.get(STATUS_CODE);
	Block status_text_block = response_block.get(STATUS_TEXT);
	Block status_parameter_block = response_block.get(CONTROL_PARAMETERS);
	memcpy(buf, status_code_block.value(), status_code_block.value_size());
	response_code = *(short *)buf;
	memcpy(response_text, status_text_block.value(),
	       status_text_block.value_size());

	status_parameter_block.parse();
	Block face_id_block = status_parameter_block.get(FACE_ID);
	memset(buf, 0, sizeof(buf));
	memcpy(buf, face_id_block.value(), face_id_block.value_size());
	face_id = ntohs(*(int *)buf);

	std::cout << response_code << " " << response_text
	          << ": Destroyed Face (FaceId: " << face_id << ")" << std::endl;
}

void AHClient::addFaceAndPrefix(const string &uri, const Name prefix,
                                DBEntry entry, const bool send_data) {
	cout << "AH Client: Adding face: " << uri << endl;
	Interest interest = prepareFaceCreationInterest(uri, m_keyChain);
	m_face.expressInterest(
	    interest,
	    bind(&AHClient::onAddFaceDataReply, this, _1, _2, uri, prefix, entry,
	         send_data),
	    [this, uri, prefix, entry, send_data](const Interest &interest,
	                                          const lp::Nack &nack) {
		    std::cout << "AH Client: Received Nack with reason "
		              << nack.getReason() << " for interest " << interest
		              << std::endl;
		    m_scheduler->schedule(
		        time::seconds(3), [this, uri, prefix, entry, send_data] {
			        addFaceAndPrefix(uri, prefix, entry, send_data);
		        });
	    },
	    [this, uri, prefix, entry, send_data](const Interest &interest) {
		    std::cout << "AH Client: Received timeout when adding face "
		              << interest << std::endl;
		    m_scheduler->schedule(
		        time::seconds(3), [this, uri, prefix, entry, send_data] {
			        addFaceAndPrefix(uri, prefix, entry, send_data);
		        });
	    });
}

void AHClient::destroyFace(int face_id) {
	Interest interest = prepareFaceDestroyInterest(face_id, m_keyChain);
	m_face.expressInterest(
	    interest, bind(&AHClient::onDestroyFaceDataReply, this, _1, _2),
	    bind(&AHClient::onNack, this, _1, _2),
	    bind(&AHClient::onTimeout, this, _1));
}

void AHClient::onSetStrategyDataReply(const Interest &interest,
                                      const Data &data) {
	Block response_block = data.getContent().blockFromValue();
	response_block.parse();
	int response_code =
	    readNonNegativeIntegerAs<int>(response_block.get(STATUS_CODE));
	std::string response_txt = readString(response_block.get(STATUS_TEXT));

	if (response_code == OK) {
		Block status_parameter_block = response_block.get(CONTROL_PARAMETERS);
		status_parameter_block.parse();
		std::cout << "\nSet strategy succeeded." << std::endl;
	} else {
		std::cout << "\nSet strategy failed." << std::endl;
		std::cout << "Status text: " << response_txt << std::endl;
	}
}

void AHClient::setStrategy(const string &uri, const string &strategy) {
	Interest interest = prepareStrategySetInterest(uri, strategy, m_keyChain);
	m_face.expressInterest(
	    interest, bind(&AHClient::onSetStrategyDataReply, this, _1, _2),
	    bind(&AHClient::onNack, this, _1, _2),
	    bind(&AHClient::onTimeout, this, _1));
}

void AHClient::setIP() {
	struct ifaddrs *ifaddr, *ifa;
	int s;
	char host[NI_MAXHOST];
	char netmask[NI_MAXHOST];
	if (getifaddrs(&ifaddr) == -1) {
		perror("getifaddrs");
		exit(EXIT_FAILURE);
	}

	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL)
			continue;

		s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host,
		                NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
		s = getnameinfo(ifa->ifa_netmask, sizeof(struct sockaddr_in), netmask,
		                NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

		if (ifa->ifa_addr->sa_family == AF_INET) {
			if (s != 0) {
				cout << "getnameinfo() failed: " << gai_strerror(s) << endl;
				exit(EXIT_FAILURE);
			}
			if (ifa->ifa_name[0] == 'l' && ifa->ifa_name[1] == 'o') // Loopback
				continue;
			cout << "\tInterface : <" << ifa->ifa_name << ">" << endl;
			cout << "\t  Address : <" << host << ">" << endl;
			inet_aton(host, &m_IP);
			inet_aton(netmask, &m_submask);
			break;
		}
	}
	freeifaddrs(ifaddr);
}

} // namespace ahnd