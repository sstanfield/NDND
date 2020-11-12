#include "ahclient.h"
#include "nfd-command-tlv.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <iostream>
#include <netdb.h>

using namespace ndn;
using namespace std;

constexpr ndn::time::milliseconds SERVER_DISCOVERY_INTEREST_LIFETIME = 4_s;
constexpr int BUF_SIZE = 1000;
constexpr int FRESHNESS_MS = 4000;
constexpr auto INTEREST_LIFETIME = 30_s;

static auto makeRibInterestParameter(const ndn::Name &route_name,
                                     const int face_id) -> ndn::Block {
	auto block = ndn::makeEmptyBlock(CONTROL_PARAMETERS);
	const ndn::Block &route_name_block = route_name.wireEncode();
	ndn::Block face_id_block =
	    ndn::makeNonNegativeIntegerBlock(FACE_ID, face_id);
	// NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
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

static auto makeRibUnregeisterInterestParameter(const ndn::Name &route_name,
                                                int face_id) -> ndn::Block {
	auto block = ndn::makeEmptyBlock(CONTROL_PARAMETERS);
	const ndn::Block &route_name_block = route_name.wireEncode();
	ndn::Block face_id_block =
	    ndn::makeNonNegativeIntegerBlock(FACE_ID, face_id);
	// NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
	ndn::Block origin_block = ndn::makeNonNegativeIntegerBlock(ORIGIN, 0xFF);

	block.push_back(route_name_block);
	block.push_back(face_id_block);
	block.push_back(origin_block);

	std::cerr << "Route name block:" << std::endl;
	std::cerr << route_name_block << std::endl;
	std::cerr << "Face id block:" << std::endl;
	std::cerr << face_id_block << std::endl;
	std::cerr << "Control parameters block:" << std::endl;
	std::cerr << block << std::endl;
	block.encode();
	return block;
}

static auto prepareRibRegisterInterest(const ndn::Name &route_name, int face_id,
                                       ndn::KeyChain &keychain)
    -> ndn::Interest {
	ndn::Name name("/localhost/nfd/rib/register");
	ndn::Block control_params = makeRibInterestParameter(route_name, face_id);
	name.append(control_params);

	ndn::security::CommandInterestSigner signer(keychain);
	ndn::Interest interest = signer.makeCommandInterest(name);
	interest.setMustBeFresh(true);
	interest.setCanBePrefix(false);
	return interest;
}

static auto prepareRibUnregisterInterest(const ndn::Name &route_name,
                                         int face_id, ndn::KeyChain &keychain)
    -> ndn::Interest {
	ndn::Name name("/localhost/nfd/rib/unregister");
	ndn::Block control_params =
	    makeRibUnregeisterInterestParameter(route_name, face_id);
	name.append(control_params);

	ndn::security::CommandInterestSigner signer(keychain);
	ndn::Interest interest = signer.makeCommandInterest(name);
	interest.setMustBeFresh(true);
	interest.setCanBePrefix(false);
	return interest;
}

static auto prepareFaceCreationInterest(const std::string &uri,
                                        ndn::KeyChain &keychain)
    -> ndn::Interest {
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

static auto prepareFaceDestroyInterest(int face_id, ndn::KeyChain &keychain)
    -> ndn::Interest {
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

namespace ahnd {

AHClient::AHClient(Name prefix, Name broadcast_prefix, int port)
    : m_prefix(std::move(prefix)),
      m_broadcast_prefix(std::move(broadcast_prefix)) {
	m_scheduler = make_unique<Scheduler>(m_face.getIoService());
	m_controller = std::make_shared<nfd::Controller>(m_face, m_keyChain);
	setIP();
	m_port = htons(port);
	m_multicast = std::make_unique<MulticastInterest>(m_face, m_controller,
	                                                  m_broadcast_prefix);
}

void AHClient::appendIpPort(Name &name) {
	// This does some unsafe C casting.
	// NOLINTNEXTLINE: unsafe C style cast
	name.append((uint8_t *)&m_IP, sizeof(m_IP))
	    // NOLINTNEXTLINE: unsafe C style cast
	    .append((uint8_t *)&m_port, sizeof(m_port));
}

auto AHClient::hasEntry(const Name &name) -> bool {
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
	    [this](auto &&_, auto &&PH2) { onArriveInterest(PH2, false); },
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
		    data->setFreshnessPeriod(time::milliseconds(FRESHNESS_MS));
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
	    [this](auto &&_, auto &&PH2) { onArriveInterest(PH2, true); },
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
		appendIpPort(name);
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
	Name const &name = request.getName();
	std::array<uint8_t, IP_BYTES> ip{};
	ip.fill(0);
	uint16_t port = 0;
	const int ip_str_len = 256;
	std::array<char, ip_str_len> ip_str{};
	ip_str.fill(0);
	for (unsigned int i = 0; i < name.size(); i++) {
		Name::Component const &component = name.get(i);
		bool ret = (component.compare(Name::Component("arrival")) == 0) ||
		           (component.compare(Name::Component("nd-info")) == 0);
		if (ret) {
			Name::Component comp;
			// getIP
			comp = name.get(i + 1);
			memcpy(ip.data(), comp.value(), IP_BYTES);
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
			char *t_ip = inet_ntoa(*(in_addr *)(ip.data()));
			unsigned long ip_len = strlen(t_ip) + 1;
			memcpy(ip_str.data(), t_ip,
			       ip_len >= ip_str_len ? ip_str_len - 1 : ip_len);
			// getPort
			comp = name.get(i + 2);
			memcpy(&port, comp.value(), sizeof(port));
			// getName
			comp = name.get(i + 3);
			unsigned int begin = i + 3;
			Name prefix;
			uint64_t name_size = comp.toNumber();
			for (unsigned int j = 0; j < name_size; j++) {
				prefix.append(name.get(begin + j + 1));
			}

			std::stringstream ss;
			ss << "udp4://" << ip_str.data() << ':' << ntohs(port);
			auto ss_str = ss.str();
			std::cout << "AH Client: Arrival Name is " << prefix.toUri()
			          << " from " << ss_str << std::endl;

			// Send back empty data to confirm I am here...
			auto data = make_shared<Data>(request.getName());
			m_keyChain.sign(*data,
			                security::SigningInfo(
			                    security::SigningInfo::SIGNER_TYPE_SHA256));
			data->setFreshnessPeriod(time::milliseconds(FRESHNESS_MS));
			m_face.put(*data);
			// Do not register route to myself
			// XXX- do better then a strcmp here...
			if (strcmp(ip_str.data(), inet_ntoa(m_IP)) == 0) {
				cout << "AH Client: My IP address returned - send back nothing"
				     << endl;
				continue;
			}
			DBEntry entry;
			entry.ip.swap(ip);
			entry.port = port;
			entry.prefix = prefix;
			addFaceAndPrefix(ss_str, prefix, entry, send_back);
		}
	}
}

void AHClient::registerRoute(const Name &route_name, int face_id, int cost,
                             const bool send_data) {
	Interest interest =
	    prepareRibRegisterInterest(route_name, face_id, m_keyChain);
	m_face.expressInterest(
	    interest,
	    [this, route_name, face_id, cost, send_data](auto &&PH1, auto &&PH2) {
		    onRegisterRouteDataReply(PH1, PH2, route_name, face_id, cost,
		                             send_data);
	    },
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

void AHClient::sendKeepAliveInterest() {
	for (auto it = m_db.begin(); it != m_db.end(); it = m_db.erase(it)) {
		const DBEntry item = *it;
		Name name(item.prefix);
		name.append("nd-keepalive");
		name.appendTimestamp();
		Interest interest(name);
		interest.setInterestLifetime(INTEREST_LIFETIME);
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
		    [item, this](const Interest &interest, const lp::Nack &nack) {
			    // Humm, log this and retry...
			    std::cout << "AH Client: received keep alive Nack with reason "
			              << nack.getReason() << " for interest " << interest
			              << std::endl;
			    removeRouteAndFace(item);
		    },
		    [item, this](const Interest &interest) {
			    // This is odd (we should get a packet from ourselves)...
			    std::cout << "AH Client: Keep alive timeout " << interest
			              << std::endl;
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

	Block const &status_code_block = response_block.get(STATUS_CODE);
	Block const &status_text_block = response_block.get(STATUS_TEXT);
	short response_code = readNonNegativeIntegerAs<int>(status_code_block);
	std::array<char, BUF_SIZE> response_text{0};
	response_text.fill(0);
	memcpy(response_text.data(), status_text_block.value(),
	       status_text_block.value_size());

	if (response_code == OK) {

		Block const &control_params = response_block.get(CONTROL_PARAMETERS);
		control_params.parse();

		Block const &name_block = control_params.get(ndn::tlv::Name);
		Name route_name(name_block);
		Block const &face_id_block = control_params.get(FACE_ID);
		int face_id = readNonNegativeIntegerAs<int>(face_id_block);
		Block const &origin_block = control_params.get(ORIGIN);
		int origin = readNonNegativeIntegerAs<int>(origin_block);
		Block const &route_cost_block = control_params.get(COST);
		int route_cost = readNonNegativeIntegerAs<int>(route_cost_block);
		Block const &flags_block = control_params.get(FLAGS);
		int flags = readNonNegativeIntegerAs<int>(flags_block);

		std::cout << "\nRegistration of route succeeded:" << std::endl;
		std::cout << "Status text: " << response_text.data() << std::endl;

		std::cout << "Route name: " << route_name.toUri() << std::endl;
		std::cout << "Face id: " << face_id << std::endl;
		std::cout << "Origin: " << origin << std::endl;
		std::cout << "Route cost: " << route_cost << std::endl;
		std::cout << "Flags: " << flags << std::endl;
		if (send_data) {
			// Then send back our info.
			Name prefix(route_name);
			prefix.append("nd-info");
			appendIpPort(prefix);
			prefix.appendNumber(m_prefix.size())
			    .append(m_prefix)
			    .appendTimestamp();

			std::cout << "AH Client: Subscribe Back to " << prefix << std::endl;
			Interest interest(prefix);
			interest.setInterestLifetime(INTEREST_LIFETIME);
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
		std::cout << "Status text: " << response_text.data() << std::endl;
		m_scheduler->schedule(
		    time::seconds(3), [this, route_name, face_id, cost, send_data] {
			    registerRoute(route_name, face_id, cost, send_data);
		    });
	}
}

void AHClient::onAddFaceDataReply(const Interest &interest, const Data &data,
                                  const string &uri, const Name &prefix,
                                  DBEntry entry, const bool send_data) {
	short response_code = 0;
	std::array<char, BUF_SIZE> response_text{0};
	response_text.fill(0);
	int face_id = 0; // Store faceid for deletion of face
	Block const &response_block = data.getContent().blockFromValue();
	response_block.parse();

	Block const &status_code_block = response_block.get(STATUS_CODE);
	Block const &status_text_block = response_block.get(STATUS_TEXT);
	response_code = readNonNegativeIntegerAs<int>(status_code_block);
	memcpy(response_text.data(), status_text_block.value(),
	       status_text_block.value_size());

	// Get FaceId for future removal of the face
	if (response_code == OK || response_code == FACE_EXISTS) {
		Block const &status_parameter_block =
		    response_block.get(CONTROL_PARAMETERS);
		status_parameter_block.parse();
		Block const &face_id_block = status_parameter_block.get(FACE_ID);
		face_id = readNonNegativeIntegerAs<int>(face_id_block);
		std::cout << response_code << " " << response_text.data()
		          << ": Added Face (FaceId: " << face_id << "): " << uri
		          << std::endl;

		entry.faceId = face_id;
		if (!hasEntry(entry.prefix)) {
			m_db.push_back(entry);
		}
		registerRoute(prefix, face_id, 0, send_data);
	} else {
		std::cout << "\nCreation of face failed." << std::endl;
		std::cout << "Status text: " << response_text.data() << std::endl;
		m_scheduler->schedule(
		    time::seconds(3), [this, uri, prefix, entry, send_data] {
			    addFaceAndPrefix(uri, prefix, entry, send_data);
		    });
	}
}

void AHClient::onDestroyFaceDataReply(const Interest &interest,
                                      const Data &data) {
	short response_code = 0;
	std::array<char, BUF_SIZE> response_text{0};
	int face_id = 0;
	Block response_block = data.getContent().blockFromValue();
	response_block.parse();

	Block const &status_code_block = response_block.get(STATUS_CODE);
	Block const &status_text_block = response_block.get(STATUS_TEXT);
	Block const &status_parameter_block =
	    response_block.get(CONTROL_PARAMETERS);
	response_code = *(short *)status_code_block.value(); // NOLINT
	response_text.fill(0);
	memcpy(response_text.data(), status_text_block.value(),
	       status_text_block.value_size());

	status_parameter_block.parse();
	Block const &face_id_block = status_parameter_block.get(FACE_ID);
	face_id = ntohs(*(int *)face_id_block.value()); // NOLINT

	std::cout << response_code << " " << response_text.data()
	          << ": Destroyed Face (FaceId: " << face_id << ")" << std::endl;
}

void AHClient::addFaceAndPrefix(const string &uri, Name const &prefix,
                                DBEntry const &entry, const bool send_data) {
	cout << "AH Client: Adding face: " << uri << endl;
	Interest interest = prepareFaceCreationInterest(uri, m_keyChain);
	m_face.expressInterest(
	    interest,
	    [this, uri, prefix, entry, send_data](auto &&PH1, auto &&PH2) {
		    onAddFaceDataReply(PH1, PH2, uri, prefix, entry, send_data);
	    },
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

void AHClient::removeRouteAndFace(const DBEntry &item) {
	// Shutdown route/face.
	auto unreg_interest =
	    prepareRibUnregisterInterest(item.prefix, item.faceId, m_keyChain);
	m_face.expressInterest(
	    unreg_interest,
	    [item, this](const Interest &interest, const Data &data) {
		    destroyFace(item.faceId);
	    },
	    nullptr, nullptr);
}

void AHClient::destroyFace(int face_id) {
	Interest interest = prepareFaceDestroyInterest(face_id, m_keyChain);
	m_face.expressInterest(
	    interest,
	    [](auto &&PH1, auto &&PH2) { onDestroyFaceDataReply(PH1, PH2); },
	    [](auto &&PH1, auto &&PH2) { onNack(PH1, PH2); },
	    [](auto &&PH1) { onTimeout(PH1); });
}

void AHClient::setIP() {
	// This will have NOLINTS because it is using a C api and will be doing
	// unsafe stuff.
	struct ifaddrs *ifaddr = nullptr;
	struct ifaddrs *ifa = nullptr;
	int s = 0;
	bool found = false;
	std::array<char, NI_MAXHOST> host{};
	host.fill(0);
	if (getifaddrs(&ifaddr) == -1) {
		perror("getifaddrs");
		exit(EXIT_FAILURE);
	}

	for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == nullptr) {
			continue;
		}

		s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host.data(),
		                NI_MAXHOST, nullptr, 0, NI_NUMERICHOST);
		if (s != 0) {
			// cout << "getnameinfo() failed: " << gai_strerror(s) << endl;
			continue;
		}

		if (ifa->ifa_addr->sa_family == AF_INET) {
			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
			if (ifa->ifa_name[0] == 'l' &&
			    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
			    ifa->ifa_name[1] == 'o') {
				// Loopback
				continue;
			}
			cout << "\tInterface : <" << ifa->ifa_name << ">" << endl;
			cout << "\t  Address : <" << host.data() << ">" << endl;
			inet_aton(host.data(), &m_IP);
			found = true;
			break;
		}
	}
	freeifaddrs(ifaddr);
	if (!found) {
		cout << "AH Client: Could not find host ip." << endl;
		exit(1);
	}
}

} // namespace ahnd