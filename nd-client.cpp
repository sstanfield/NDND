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
#include "server-daemon.hpp"
#include "multicast.h"

using namespace ndn;
using namespace ndn::ndnd;
using namespace std;

const Name SERVER_PREFIX("/ndn/nd");
const Name SERVER_DISCOVERY_PREFIX("/ndn/nd/arrival");
const uint64_t SERVER_DISCOVERY_ROUTE_COST(0);
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
    m_controller = std::make_shared<nfd::Controller>(m_face, m_keyChain);
    setIP();
    m_port = htons(6363); // default
    inet_aton(server_ip.c_str(), &m_server_IP);
    m_multicast = std::make_unique<MulticastInterest>(m_face, m_controller);
  }

  // TODO: remove face on SIGINT, SIGTERM

  void
  registerArrivePrefix()
  {
    if (m_arrive_listen) {
      return;
    }
    Name prefix("/ndn/nd");
    std::cout << "NDND (Client): Registering arrive prefix " << prefix.toUri() << std::endl;
    m_arrivePrefixId = m_face.setInterestFilter(InterestFilter(prefix),
                                              bind(&NDNDClient::onArriveInterest, this, _2, true),
                                              bind(&NDNDClient::onRegArriveSuccess, this, _1),
                                              bind(&NDNDClient::onRegArriveFail, this, _1, _2));
    m_arrive_listen = true;
  }

  void onRegArriveSuccess(const Name& name) {
    std::cout << "NDND (Client): Registered arrive prefix "
              << name.toUri()
              << std::endl;
  }

  void onRegArriveFail(const Name& name, const std::string& error) {
    std::cout << "NDND (Client): Failed to register arrive prefix "
              << name.toUri()
              << " reason: "
              << error
              << std::endl;
  }

  void sendArrivalInterest() {
      if (m_multicast->is_error()) {
          cout << "NDND (Client): Multicast error, exiting" << endl;
          exit(1);
      }
      if (m_multicast->is_ready()) {
          Name name("/ndn/nd/arrival");
          name.append((uint8_t * ) & m_IP, sizeof(m_IP)).append((uint8_t * ) & m_port, sizeof(m_port));
          name.appendNumber(m_prefix.size()).append(m_prefix).appendTimestamp();

          Interest interest(name);
          interest.setInterestLifetime(SERVER_DISCOVERY_INTEREST_LIFETIME);
          interest.setMustBeFresh(true);
          interest.setNonce(4);
          //interest.setCanBePrefix(false);
          interest.setCanBePrefix(true);

          cout << "NDND (Client): Arrival Interest: " << interest << endl;

          m_multicast->expressInterest(interest,
                                bind(&NDNDClient::onArriveData, this, _1, _2),
                                bind(&NDNDClient::onNack, this, _1, _2),
                                bind(&NDNDClient::onArriveTimeout, this, _1));
      } else {
          cout << "NDND (Client): Arrival Interest, multicast not ready will retry" << endl;
          m_scheduler->schedule(time::seconds(3), [this] {
              sendArrivalInterest();
          });
      }
  }

  void onArriveData(const Interest& interest, const Data& data) {
      cout << "Arrive data " << interest.getName() << endl;
  }

  void onArriveTimeout(const Interest& interest) {
      std::cout << "Arrive Timeout (I am all alone?) " << interest << std::endl;
  }

  void onArriveInterest(const Interest& request, const bool send_back)
  {
      cout << "Got arrive interest " << request.getName() << endl;
    // First setup the face and route the pier.
    Name name = request.getName();
    uint8_t ip[16];
    uint16_t port;
    char ipStr[256];
    for (unsigned int i = 0; i < name.size(); i++) {
      Name::Component component = name.get(i);
      bool ret = (component.compare(Name::Component("arrival")) == 0) ||
                 (component.compare(Name::Component("nd-info")) == 0);
      if (ret) {
          cout << "Working on arrival interest" << endl;
        Name::Component comp;
        // getIP
        comp = name.get(i + 1);
        memcpy(ip, comp.value(), sizeof(ip));
        char *tIp = inet_ntoa(*(in_addr*)(ip));
        int ipLen = strlen(tIp)+1;
        memcpy(ipStr, tIp, ipLen>255?255:ipLen);
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
        ss << "udp4://" << ipStr << ':' << ntohs(port);
        auto ssStr = ss.str();
        std::cout << "NDND (Client): Arrival Name is " << prefix.toUri() << " from " << ssStr << std::endl;

        m_uri_to_prefix[ssStr] = prefix.toUri();
        // Do not register route to myself
        if (strcmp(ipStr, inet_ntoa(m_IP)) == 0) {
          cout << "NDND (Client): My IP address returned - send back nothing" << endl;
          auto data = make_shared<Data>(request.getName());

          m_keyChain.sign(*data, security::SigningInfo(security::SigningInfo::SIGNER_TYPE_SHA256));
          data->setFreshnessPeriod(time::milliseconds(4000));
          m_face.put(*data);
          continue;
        }
        addFace(ssStr, send_back);
      }
    }
  }

    void onResponseTimeout(const Interest& interest)
    {
        // XXX removeRoute(findEntry(interest.getName()));
    }

    void onResponseData(const Data& data)
    {
        std::cout << "NDND (Client): Record Updated/Confirmed from " << data.getName() << std::endl;
    }

  void registerRoute(const Name& route_name, int face_id,
                     int cost, const bool send_back)
  {
    Interest interest = prepareRibRegisterInterest(route_name, face_id, m_keyChain, cost);
    m_face.expressInterest(
      interest,
      bind(&NDNDClient::onRegisterRouteDataReply, this, _1, _2, send_back),
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
  
  void registerSubPrefix() {
    Name name(m_prefix);
    name.append("nd-info");
    m_face.setInterestFilter(InterestFilter(name),
                             //bind(&NDNDClient::onSubInterest, this, _2),
                             bind(&NDNDClient::onArriveInterest, this, _2, false),
                             nullptr);
    cout << "NDND (Client): Register Prefix: " << name << endl;
  }


  void sendSubInterest() {
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
  void onSubData(const Interest& interest, const Data& data) {
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

      addFace(ss.str(), false);

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

  void onRegisterRouteDataReply(const Interest& interest, const Data& data, const bool send_data)
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
        if (send_data) {
            // Then send back our info.
            Buffer contentBuf;
            using namespace std::chrono;
            milliseconds ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
            struct RESULT result;
            result.V4 = 1;
            memcpy(result.IpAddr, &m_IP, 16);
            result.Port = m_port;

            for (unsigned int i = 0; i < sizeof(struct RESULT); i++) {
                contentBuf.push_back(*((uint8_t * ) & result + i));
            }
            auto block = m_prefix.wireEncode();
            for (size_t i = 0; i < block.size(); i++) {
                contentBuf.push_back(*(block.wire() + i));
            }

            Name prefix(route_name);
            prefix.append("nd-info");
            prefix.append((uint8_t*)&m_IP, sizeof(m_IP)).append((uint8_t*)&m_port, sizeof(m_port));
            prefix.appendNumber(m_prefix.size()).append(m_prefix).appendTimestamp();

            std::cout << "NDND (Client): Subscribe Back to " << prefix << std::endl;
            Interest interest(prefix);
            interest.setInterestLifetime(30_s);
            interest.setMustBeFresh(true);
            interest.setNonce(4);
            interest.setCanBePrefix(false);

            m_face.expressInterest(interest,
                                   std::bind(&NDNDClient::onResponseData, this, _2),
                                   std::bind(&NDNDClient::onNack, this, _1, _2),
                                   std::bind(&NDNDClient::onResponseTimeout, this, _1));
        }
    } else {
      std::cout << "\nRegistration of route failed." << std::endl;
      std::cout << "Status text: " << response_text << std::endl;
    }
  }

  void onAddFaceDataReply(const Interest& interest, const Data& data,
                          const string& uri, const bool send_data)
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
        registerRoute(it->second, face_id, 0, send_data);
        //registerRoute(it->second, m_server_faceid, 10, is_server_face);
      } else {
	      std::cerr << "Failed to find prefix for uri " << uri << std::endl;
      }

    } else {
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

  void addFace(const string& uri, const bool send_data)
  {
    printf("NDND (Client): Adding face: %s\n", uri.c_str());
    Interest interest = prepareFaceCreationInterest(uri, m_keyChain);
    m_face.expressInterest(
      interest,
      bind(&NDNDClient::onAddFaceDataReply, this, _1, _2, uri, send_data),
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
  std::shared_ptr<nfd::Controller> m_controller;
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
  bool m_arrive_listen = false;
  RegisteredPrefixHandle m_arrivePrefixId;
  std::unique_ptr<MulticastInterest> m_multicast;
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
    m_client->registerArrivePrefix();
    m_client->registerSubPrefix();
    m_client->sendArrivalInterest();
    //loop();
    while (1) {
        cout << "LOOPING" << endl;
        m_client->m_face.processEvents();
    }
  }

  void loop() {
    m_client->sendSubInterest();
    m_scheduler->schedule(time::seconds(30), [this] {
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
