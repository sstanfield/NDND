#include <ndn-cxx/face.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <iostream>
#include <chrono>
#include <ndn-cxx/util/sha256.hpp>
#include <ndn-cxx/encoding/tlv.hpp>
#include <ndn-cxx/util/scheduler.hpp>
#include <ndn-cxx/face.hpp>
#include <ndn-cxx/mgmt/nfd/controller.hpp>
#include <ndn-cxx/mgmt/nfd/face-status.hpp>
#include <ndn-cxx/mgmt/nfd/control-response.hpp>
#include <ndn-cxx/mgmt/nfd/control-parameters.hpp>
#include <ndn-cxx/mgmt/nfd/controller.hpp>
#include <boost/asio.hpp>
#include <sstream>
#include <string.h>

#include "nd-packet-format.h"
#include "nfdc-helpers.h"

using namespace ndn;
using namespace ndn::ndnd;
using namespace std;

const Name SERVER_PREFIX("/ndn/nd");
const Name SERVER_DISCOVERY_PREFIX("/ndn/nd/arrival");
const uint64_t SERVER_DISCOVERY_ROUTE_COST(1);
const time::milliseconds SERVER_DISCOVERY_ROUTE_EXPIRATION = 30_s;
const time::milliseconds SERVER_DISCOVERY_INTEREST_LIFETIME = 4_s;

class Options
{
public:
  Options()
    : m_prefix("/test/01/02")
    , server_prefix("/ndn/nd")
    , server_ip("127.0.0.1")  // XXX not used- remove (multicast now)
  {
  }
public:
  ndn::Name m_prefix;
  ndn::Name server_prefix;
  string server_ip;
};


class NDNDClient{
public:
  NDNDClient(const Name& m_prefix, 
             const Name& server_prefix, 
             const string& server_ip)
    : m_prefix(m_prefix)
    , m_server_prefix(server_prefix)
  {
    m_scheduler = new Scheduler(m_face.getIoService());
    m_controller = new nfd::Controller(m_face, m_keyChain);
    setIP();
    m_port = htons(6363); // default
    inet_aton(server_ip.c_str(), &m_server_IP);
  }

  // TODO: remove face on SIGINT, SIGTERM

  void registerRoute(const Name& route_name, int face_id,
                     int cost = 0) 
  {
    Interest interest = prepareRibRegisterInterest(route_name, face_id, m_keyChain, cost);
    m_face.expressInterest(
      interest,
      bind(&NDNDClient::onRegisterRouteDataReply, this, _1, _2),
      bind(&NDNDClient::onNack, this, _1, _2),
      bind(&NDNDClient::onTimeout, this, _1));
  }

  void onSubInterest(const Interest& subInterest)
  {
    // reply data with IP confirmation
    Buffer contentBuf;
    for (unsigned int i = 0; i < sizeof(m_IP); i++) {
      contentBuf.push_back(*((uint8_t*)&m_IP + i));
    }

    auto data = make_shared<Data>(subInterest.getName());
    if (contentBuf.size() > 0) {
      data->setContent(contentBuf.get<uint8_t>(), contentBuf.size());
    } else {
      return;
    }

    m_keyChain.sign(*data, security::SigningInfo(security::SigningInfo::SIGNER_TYPE_SHA256));
    // security::SigningInfo signInfo(security::SigningInfo::SIGNER_TYPE_ID, m_options.identity);
    // m_keyChain.sign(*m_data, signInfo);
    data->setFreshnessPeriod(time::milliseconds(4000));
    m_face.put(*data);
    cout << "NDND (Client): Publishing Data: " << *data << endl;
  }
  
  void sendArrivalInterest()
  {
    nfd::FaceQueryFilter filter;
    filter.setLinkType(nfd::LINK_TYPE_MULTI_ACCESS);

    m_controller->fetch<nfd::FaceQueryDataset>(
      filter,
      bind(&NDNDClient::registerMultiPrefix, this, _1),
      [this] (uint32_t code, const std::string& reason) {
        std::cout << "NDND (Client): Error " << to_string(code) << " when querying multi-access faces: " << reason << endl;
        exit(1);
      });
  }

  void registerMultiPrefix(const std::vector<nfd::FaceStatus>& dataset) {
    if (dataset.empty()) {
      std::cout << "NDND (Client): No multi-access faces available" << endl;
      exit(1);
    }

    m_nRegs = dataset.size();
    m_nRegSuccess = 0;
    m_nRegFailure = 0;

    for (const auto& faceStatus : dataset) {
      nfd::ControlParameters parameters;
      parameters.setName(SERVER_DISCOVERY_PREFIX)
              .setFaceId(faceStatus.getFaceId())
              .setCost(SERVER_DISCOVERY_ROUTE_COST)
              .setExpirationPeriod(SERVER_DISCOVERY_ROUTE_EXPIRATION);

      m_controller->start<nfd::RibRegisterCommand>(
        parameters,
        [this] (const nfd::ControlParameters&) {
          ++m_nRegSuccess;
          afterReg();
        },
        [this, faceStatus] (const nfd::ControlResponse& resp) {
          std::cerr << "NDND (Client): Error " << resp.getCode() << " when registering hub discovery prefix "
                    << "for face " << faceStatus.getFaceId() << " (" << faceStatus.getRemoteUri()
                    << "): " << resp.getText() << std::endl;
          ++m_nRegFailure;
          afterReg();
        });
    }
  }

  void
  afterReg()
  {
    if (m_nRegSuccess + m_nRegFailure < m_nRegs) {
      return; // continue waiting
    }
    if (m_nRegSuccess > 0) {
      this->setStrategy();
    } else {
      std::cout << "NDND (Client): Cannot register hub discovery prefix for any face" << endl;
      exit(1);
    }
  }

  void
  setStrategy()
  {
    nfd::ControlParameters parameters;
    parameters.setName(SERVER_DISCOVERY_PREFIX)
              .setStrategy("/localhost/nfd/strategy/multicast"),

    m_controller->start<nfd::StrategyChoiceSetCommand>(
    parameters,
    bind(&NDNDClient::requestServerData, this),
    [this] (const nfd::ControlResponse& resp) {
      std::cout << "NDND (Client): Error " << to_string(resp.getCode()) << " when setting multicast strategy: " <<
                 resp.getText() << endl;
    });
  }

  void
  requestServerData()
  {
    Name name("/ndn/nd/arrival");
    name.append((uint8_t*)&m_IP, sizeof(m_IP)).append((uint8_t*)&m_port, sizeof(m_port));
    name.appendNumber(m_prefix.size()).append(m_prefix).appendTimestamp();

    Interest interest(name);
    interest.setInterestLifetime(SERVER_DISCOVERY_INTEREST_LIFETIME);
    interest.setMustBeFresh(true);
    interest.setNonce(4);
    //interest.setCanBePrefix(false);
    interest.setCanBePrefix(true);

    cout << "NDND (Client): Arrival Interest: " << interest << endl;

    m_face.expressInterest(interest,
                           bind(&NDNDClient::onSubData, this, _1, _2),
                           bind(&NDNDClient::onNack, this, _1, _2), //no expectation
                           nullptr); //no expectation
  }

  void registerSubPrefix()
  {
    Name name(m_prefix);
    name.append("nd-info");
    m_face.setInterestFilter(InterestFilter(name), bind(&NDNDClient::onSubInterest, this, _2), nullptr);
    cout << "NDND (Client): Register Prefix: " << name << endl;
  }


  void sendSubInterest()
  {
    if (!m_have_server) {
      return;
    }
    Name name("/ndn/nd");
    name.appendTimestamp();
    Interest interest(name);
    interest.setInterestLifetime(30_s);
    interest.setMustBeFresh(true);
    interest.setNonce(4);
    interest.setCanBePrefix(false);

    m_face.expressInterest(interest,
                           bind(&NDNDClient::onSubData, this, _1, _2),
                           bind(&NDNDClient::onNack, this, _1, _2),
                           bind(&NDNDClient::onTimeout, this, _1));
  }

// private:
  void onSubData(const Interest& interest, const Data& data)
  {
    std::cout << data << std::endl;

    size_t dataSize = data.getContent().value_size();
    auto pResult = reinterpret_cast<const RESULT*>(data.getContent().value());
    int iNo = 1;
    Name name;
    char ipStr[256];

    while((uint8_t*)pResult < data.getContent().value() + dataSize){
      m_len = sizeof(RESULT);
      char *tIp = inet_ntoa(*(in_addr*)(pResult->IpAddr));
      int ipLen = strlen(tIp)+1;
      memcpy(ipStr, tIp, ipLen>255?255:ipLen);
      printf("-----%2d-----\n", iNo);
      printf("IP: %s\n", ipStr);
      std::stringstream ss;
      ss << "udp4://" << ipStr << ':' << ntohs(pResult->Port);
      printf("Port: %hu\n", ntohs(pResult->Port));

      auto result = Block::fromBuffer(pResult->NamePrefix, data.getContent().value() + dataSize - pResult->NamePrefix);
      name.wireDecode(std::get<1>(result));
      printf("Name Prefix: %s\n", name.toUri().c_str());
      m_len += std::get<1>(result).size();

      pResult = reinterpret_cast<const RESULT*>(((uint8_t*)pResult) + m_len);
      iNo ++;

      m_uri_to_prefix[ss.str()] = name.toUri();
      cout << "URI: " << ss.str() << endl;
      // Do not register route to myself
      if (strcmp(ipStr, inet_ntoa(m_IP)) == 0) {
        cout << "My IP address returned" << endl;
        continue;
      }
      if (SERVER_PREFIX.isPrefixOf(name)) {
        m_have_server = true;
      }

      addFace(ss.str());

      setStrategy(name.toUri(), BEST_ROUTE);
    }
  }

  void onNack(const Interest& interest, const lp::Nack& nack)
  {
    std::cout << "received Nack with reason " << nack.getReason()
              << " for interest " << interest << std::endl;
    if (SERVER_PREFIX.isPrefixOf(interest.getName())) {
      m_scheduler->schedule(time::seconds(3), [this] {
        sendArrivalInterest();
      });
    }
  }

  void onTimeout(const Interest& interest)
  {
    std::cout << "Timeout " << interest << std::endl;
  }

  void onRegisterRouteDataReply(const Interest& interest, const Data& data)
  {
    Block response_block = data.getContent().blockFromValue();
    response_block.parse();

    std::cout << response_block << std::endl;

    Block status_code_block = response_block.get(STATUS_CODE);
    Block status_text_block = response_block.get(STATUS_TEXT);
    short response_code = readNonNegativeIntegerAs<int>(status_code_block);
    char response_text[1000] = {0};
    memcpy(response_text, status_text_block.value(), status_text_block.value_size());

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
    }
    else {
      std::cout << "\nRegistration of route failed." << std::endl;
      std::cout << "Status text: " << response_text << std::endl;
    }
  }

  void onAddFaceDataReply(const Interest& interest, const Data& data,
                          const string& uri) 
  {
    short response_code;
    char response_text[1000] = {0};
    int face_id;                      // Store faceid for deletion of face
    Block response_block = data.getContent().blockFromValue();
    response_block.parse();

    Block status_code_block = response_block.get(STATUS_CODE);
    Block status_text_block = response_block.get(STATUS_TEXT);
    response_code = readNonNegativeIntegerAs<int>(status_code_block);
    memcpy(response_text, status_text_block.value(), status_text_block.value_size());

    // Get FaceId for future removal of the face
    if (response_code == OK || response_code == FACE_EXISTS) {
      Block status_parameter_block =  response_block.get(CONTROL_PARAMETERS);
      status_parameter_block.parse();
      Block face_id_block = status_parameter_block.get(FACE_ID);
      face_id = readNonNegativeIntegerAs<int>(face_id_block);
      std::cout << response_code << " " << response_text << ": Added Face (FaceId: "
                << face_id << "): " << uri << std::endl;

      auto it = m_uri_to_prefix.find(uri);
      if (it != m_uri_to_prefix.end()) {
        registerRoute(it->second, face_id, 0);
        //registerRoute(it->second, m_server_faceid, 10, is_server_face);
      } else {
	      std::cerr << "Failed to find prefix for uri " << uri << std::endl;
      }

    }
    else {
      std::cout << "\nCreation of face failed." << std::endl;
      std::cout << "Status text: " << response_text << std::endl;
    }
  }

  void onDestroyFaceDataReply(const Interest& interest, const Data& data) 
  {
    short response_code;
    char response_text[1000] = {0};
    char buf[1000]           = {0};   // For parsing
    int face_id;
    Block response_block = data.getContent().blockFromValue();
    response_block.parse();

    Block status_code_block =       response_block.get(STATUS_CODE);
    Block status_text_block =       response_block.get(STATUS_TEXT);
    Block status_parameter_block =  response_block.get(CONTROL_PARAMETERS);
    memcpy(buf, status_code_block.value(), status_code_block.value_size());
    response_code = *(short *)buf;
    memcpy(response_text, status_text_block.value(), status_text_block.value_size());

    status_parameter_block.parse();
    Block face_id_block = status_parameter_block.get(FACE_ID);
    memset(buf, 0, sizeof(buf));
    memcpy(buf, face_id_block.value(), face_id_block.value_size());
    face_id = ntohs(*(int *)buf);

    std::cout << response_code << " " << response_text << ": Destroyed Face (FaceId: "
              << face_id << ")" << std::endl;
  }

  void addFace(const string& uri) 
  {
    printf("NDND (Client): Adding face: %s\n", uri.c_str());
    Interest interest = prepareFaceCreationInterest(uri, m_keyChain);
    m_face.expressInterest(
      interest,
      bind(&NDNDClient::onAddFaceDataReply, this, _1, _2, uri),
      bind(&NDNDClient::onNack, this, _1, _2),
      bind(&NDNDClient::onTimeout, this, _1));
  }

  void destroyFace(int face_id) 
  {
    Interest interest = prepareFaceDestroyInterest(face_id, m_keyChain);
    m_face.expressInterest(
      interest,
      bind(&NDNDClient::onDestroyFaceDataReply, this, _1, _2),
      bind(&NDNDClient::onNack, this, _1, _2),
      bind(&NDNDClient::onTimeout, this, _1));
  }

  void onSetStrategyDataReply(const Interest& interest, const Data& data) 
  {
    Block response_block = data.getContent().blockFromValue();
    response_block.parse();
    int responseCode = readNonNegativeIntegerAs<int>(response_block.get(STATUS_CODE));
    std::string responseTxt = readString(response_block.get(STATUS_TEXT));

    if (responseCode == OK) {
      Block status_parameter_block = response_block.get(CONTROL_PARAMETERS);
      status_parameter_block.parse();
      std::cout << "\nSet strategy succeeded." << std::endl;
    } else {
      std::cout << "\nSet strategy failed." << std::endl;
      std::cout << "Status text: " << responseTxt << std::endl;
    }
  }

  void setStrategy(const string& uri, const string& strategy) 
  {
    Interest interest = prepareStrategySetInterest(uri, strategy, m_keyChain);
    m_face.expressInterest(
      interest,
      bind(&NDNDClient::onSetStrategyDataReply, this, _1, _2),
      bind(&NDNDClient::onNack, this, _1, _2),
      bind(&NDNDClient::onTimeout, this, _1));
  }

  void setIP() 
  {
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

      s=getnameinfo(ifa->ifa_addr,sizeof(struct sockaddr_in),host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
      s=getnameinfo(ifa->ifa_netmask,sizeof(struct sockaddr_in),netmask, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

      if (ifa->ifa_addr->sa_family==AF_INET) {
        if (s != 0) {
          printf("getnameinfo() failed: %s\n", gai_strerror(s));
          exit(EXIT_FAILURE);
        }
        if (ifa->ifa_name[0] == 'l' && ifa->ifa_name[1] == 'o')   // Loopback
          continue;
        printf("\tInterface : <%s>\n", ifa->ifa_name);
        printf("\t  Address : <%s>\n", host);
        inet_aton(host, &m_IP);
        inet_aton(netmask, &m_submask);
        break;
      }
    }
    freeifaddrs(ifaddr);
  }

public:
  Face m_face;
  KeyChain m_keyChain;
  nfd::Controller *m_controller;
  Name m_prefix;
  Name m_server_prefix;
  in_addr m_IP;
  in_addr m_submask;
  Scheduler *m_scheduler;
  in_addr m_server_IP;
  uint16_t m_port;
  uint8_t m_buffer[4096];
  size_t m_len;
  std::map<std::string, std::string> m_uri_to_prefix;
  int m_nRegs = 0;
  int m_nRegSuccess = 0;
  int m_nRegFailure = 0;
  bool m_have_server = false;
};


class Program
{
public:
  explicit Program(const Options& options)
    : m_options(options)
  {
    // Init client
    m_client = new NDNDClient(m_options.m_prefix,
                              m_options.server_prefix, 
                              m_options.server_ip);

    m_scheduler = new Scheduler(m_client->m_face.getIoService());
    m_client->registerSubPrefix();
    m_client->sendArrivalInterest();
    loop();
  }

  void loop() {
    m_client->sendSubInterest();
    m_scheduler->schedule(time::seconds(3), [this] {
      loop();
    });
  }

  ~Program() {
    delete m_client;
    delete m_scheduler;
  }

  NDNDClient *m_client;

private:
  const Options m_options;
  Scheduler *m_scheduler;
  boost::asio::io_service m_io_service;
};


int
main(int argc, char** argv)
{ 
  if (argc < 2) {
      printf("usage: %s /prefix\n", argv[0]);
      printf("    /prefix: the ndn name for this client\n");
      return 1;
  }
  Options opt;
  opt.m_prefix = argv[1];
  Program program(opt);
  program.m_client->m_face.processEvents();
}
