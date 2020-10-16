// AUTHOR: Zhiyi Zhang
// EMAIL: zhiyi@cs.ucla.edu
// License: LGPL v3.0

#include "server-daemon.hpp"
#include "nfdc-helpers.h"
#include "nd-packet-format.h"
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <chrono>
#include <iostream>
//#include <optional>

namespace ndn {
namespace ndnd {

static std::string
getFaceUri(const DBEntry& entry)
{
  std::string result = "udp4://";
  result += inet_ntoa(*(in_addr*)(entry.ip));
  result += ':';
  result += std::to_string(htons(entry.port));
  return result;
}

//std::optional<std::reference_wrapper<DBEntry&>>
//std::optional<DBEntry&>
DBEntry&
NDServer::findEntry(const Name& name)
{
  for (auto it = m_db.begin(); it != m_db.end();) {
    bool is_Prefix = it->prefix.isPrefixOf(name);
    if (is_Prefix) {
      return *it;
    }
    ++it;
  }
  //return std::nullopt;
std::cout << "WTAF!";
exit(1);
}

bool
NDServer::hasEntry(const Name& name)
{
  for (auto it = m_db.begin(); it != m_db.end();) {
    bool is_Prefix = it->prefix.isPrefixOf(name);
    if (is_Prefix) {
      return true;
    }
    ++it;
  }
  return false;
}

void
NDServer::subscribeBack(const std::string& url)
{
  Name name(url);
  for (auto it = m_db.begin(); it != m_db.end();) {
    bool is_Prefix = it->prefix.isPrefixOf(name);
    if (is_Prefix) {
      std::cout << "NDND (RV): Subscribe Back to " << url << std::endl;
      name.append("nd-info");
      name.appendTimestamp();
      Interest interest(name);
      interest.setInterestLifetime(30_s);
      interest.setMustBeFresh(true);
      interest.setNonce(4);
      interest.setCanBePrefix(false);
      // wait until entry confirmed
      if (!it->confirmed) {
        std::cout << "NDND (RV): Entry not confirmed, try again 1 sec later: " << interest << std::endl;
        m_scheduler->schedule(time::seconds(1), [this, url] {
          subscribeBack(url);
        });
        return;
      }



      m_face.expressInterest(interest,
                            std::bind(&NDServer::onSubData, this, _2),
                            std::bind(&NDServer::onNack, this, _1, _2),
                            std::bind(&NDServer::onSubTimeout, this, _1));
      m_scheduler->schedule(time::seconds(10), [this, url] {
          subscribeBack(url);
      });
    }
    ++it;
  }
}

void
NDServer::onSubTimeout(const Interest& interest)
{
  removeRoute(findEntry(interest.getName()));
}

void
NDServer::onSubData(const Data& data)
{
  DBEntry& entry = findEntry(data.getName());
  std::cout << "NDND (RV): Record Updated/Confirmed from " << entry.prefix << std::endl;
  auto ptr = data.getContent().value();
  memcpy(entry.ip, ptr, sizeof(entry.ip));
}

int
NDServer::parseInterest(const Interest& interest, DBEntry& entry)
{
  // identify a Arrival Interest
  Name name = interest.getName();
  for (unsigned int i = 0; i < name.size(); i++)
  {
    Name::Component component = name.get(i);
    int ret = component.compare(Name::Component("arrival"));
    if (ret == 0)
    {
      Name::Component comp;
      // getIP
      comp = name.get(i + 1);
      memcpy(entry.ip, comp.value(), sizeof(entry.ip));
      // getPort
      comp = name.get(i + 2);
      memcpy(&entry.port, comp.value(), sizeof(entry.port));
      // getName
      comp = name.get(i + 3);
      int begin = i + 3;
      Name prefix;
      uint64_t name_size = comp.toNumber();
      for (unsigned int j = 0; j < name_size; j++) {
        prefix.append(name.get(begin + j + 1));
      }
      entry.prefix = prefix;

      std::cout << "NDND (RV): Arrival Name is " << entry.prefix.toUri() << std::endl;

      // AddRoute and Subscribe Back
      entry.confirmed = false;
      if (!hasEntry(prefix)) {
        // don't add dups, XXX should do an update here.
        m_db.push_back(entry);
      }
      addRoute(getFaceUri(entry), entry);
      subscribeBack(entry.prefix.toUri());
      return 1;
    }
  }
  // then it would be a Subscribe Interest
  return 0;
}

void
NDServer::run()
{
  setIP();
  m_port = htons(6363); // default
  m_ttl = 30 * 1000;
  m_scheduler = new Scheduler(m_face.getIoService());
  m_face.processEvents();
}

void
NDServer::registerPrefix(const Name& prefix)
{
  m_prefix = prefix;
  auto prefixId = m_face.setInterestFilter(InterestFilter(m_prefix),
                                           bind(&NDServer::onInterest, this, _2), nullptr);
}

void 
NDServer::onNack(const Interest& interest, const lp::Nack& nack)
{
  std::cout << "NDND (RV): received Nack with reason " << nack.getReason()
            << " for interest " << interest << std::endl;
  removeRoute(findEntry(interest.getName()));
}

void
NDServer::onInterest(const Interest& request)
{
  DBEntry entry;
  int ret = parseInterest(request, entry);
  if (ret) {
    Buffer contentBuf;
    using namespace std::chrono;
    milliseconds ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
    struct RESULT result;
    result.V4 = 1;
    memcpy(result.IpAddr, &m_IP, 16);
    result.Port = m_port;

    for (unsigned int i = 0; i < sizeof(struct RESULT); i++) {
      contentBuf.push_back(*((uint8_t*)&result + i));
    }
    Name prefix("/ndn/nd");
    auto block = prefix.wireEncode();
    for (size_t i =0; i < block.size(); i++) {
      contentBuf.push_back(*(block.wire() + i));
    }

    auto data = make_shared<Data>(request.getName());
    data->setContent(contentBuf.get<uint8_t>(), contentBuf.size());



    m_keyChain.sign(*data, security::SigningInfo(security::SigningInfo::SIGNER_TYPE_SHA256));
    data->setFreshnessPeriod(time::milliseconds(4000));
    m_face.put(*data);
    std::cout << "NDND (RV): Arrival respond: " << std::endl << *data << std::endl;
    return;
  }
  Buffer contentBuf;
  int counter = 0;
  for (auto it = m_db.begin(); it != m_db.end();) {
std::cout << "XXXXX db entry count: " << counter << std::endl;
    const auto& item = *it;
    using namespace std::chrono;
    milliseconds ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
    //if (item.tp + item.ttl < ms.count()) {
      // if the entry is out-of-date, erase it
    //  std::cout << "NDND (RV): Entry out date: " << it->prefix << std::endl;
      //it = m_db.erase(it);
    //} else {
      // else, add the entry to the reply if it matches
      struct RESULT result;
      result.V4 = item.v4? 1 : 0;
      memcpy(result.IpAddr, item.ip, 16);
      result.Port = item.port;

      for (unsigned int i = 0; i < sizeof(struct RESULT); i++) {
        contentBuf.push_back(*((uint8_t*)&result + i));
      }
      auto block = item.prefix.wireEncode();
      for (size_t i =0; i < block.size(); i++) {
        contentBuf.push_back(*(block.wire() + i));
      }
      std::cout << "NDND (RV): Pushing Back one record " << std::endl;
      counter++;
      ++it;
      if (counter > 10)
        break;
    //}
  }

  auto data = make_shared<Data>(request.getName());
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
  std::cout << "NDND (RV): Putting Data back: " << std::endl << *data << std::endl;
}

void
NDServer::removeRoute(DBEntry& entry)
{
  auto Interest = prepareRibUnregisterInterest(entry.prefix, entry.faceId, m_keyChain);
  m_face.expressInterest(Interest,
                         std::bind(&NDServer::onData, this, _2, entry),
                         nullptr, nullptr);
}

void
NDServer::addRoute(const std::string& url, DBEntry& entry)
{
  auto Interest = prepareFaceCreationInterest(url, m_keyChain);
  m_face.expressInterest(Interest,
                         std::bind(&NDServer::onData, this, _2, entry),
                         nullptr, nullptr);
}

void
NDServer::onData(const Data& data, DBEntry& entry)
{
  Name ribRegisterPrefix("/localhost/nfd/rib/register");
  Name ribUnregisterPrefix("/localhost/nfd/rib/unregister");
  Name faceCreationPrefix("/localhost/nfd/faces/create");
  Name faceDestroyPrefix("/localhost/nfd/faces/destroy");
  if (ribRegisterPrefix.isPrefixOf(data.getName())) {
    Block response_block = data.getContent().blockFromValue();
    response_block.parse();
    int responseCode = readNonNegativeIntegerAs<int>(response_block.get(STATUS_CODE));
    std::string responseTxt = readString(response_block.get(STATUS_TEXT));

    // Get FaceId for future removal of the face
    if (responseCode == OK) {
      Block controlParam = response_block.get(CONTROL_PARAMETERS);
      controlParam.parse();

      Name route_name(controlParam.get(ndn::tlv::Name));
      int face_id = readNonNegativeIntegerAs<int>(controlParam.get(FACE_ID));
      int origin = readNonNegativeIntegerAs<int>(controlParam.get(ORIGIN));
      int route_cost = readNonNegativeIntegerAs<int>(controlParam.get(COST));
      int flags = readNonNegativeIntegerAs<int>(controlParam.get(FLAGS));

      std::cout << "\nRegistration of route succeeded:" << std::endl;
      std::cout << "Status text: " << responseTxt << std::endl;
      std::cout << "Route name: " << route_name.toUri() << std::endl;
      std::cout << "Face id: " << face_id << std::endl;
      std::cout << "Origin: " << origin << std::endl;
      std::cout << "Route cost: " << route_cost << std::endl;
      std::cout << "Flags: " << flags << std::endl;

      DBEntry& entry = findEntry(route_name);
      entry.confirmed = true;
      entry.faceId = face_id;
    }
    else {
      std::cout << "\nRegistration of route failed." << std::endl;
      std::cout << "Status text: " << responseTxt << std::endl;
    }
  }
  else if (ribUnregisterPrefix.isPrefixOf(data.getName())) {
    Block response_block = data.getContent().blockFromValue();
    response_block.parse();
    int responseCode = readNonNegativeIntegerAs<int>(response_block.get(STATUS_CODE));
    std::string responseTxt = readString(response_block.get(STATUS_TEXT));
    if (responseCode == OK) {
      std::cout << "Route removal success" << std::endl;
      // remove the face
      auto Interest = prepareFaceDestroyInterest(entry.faceId, m_keyChain);
      m_face.expressInterest(Interest,
                             std::bind(&NDServer::onData, this, _2, entry),
                             nullptr, nullptr); 
    }
    else {
      std::cout << "Removal of route failed." << std::endl;
      std::cout << "Status text: " << responseTxt << std::endl;
    }
  }
  else if (faceCreationPrefix.isPrefixOf(data.getName())) {
    Block response_block = data.getContent().blockFromValue();
    response_block.parse();
    int responseCode = readNonNegativeIntegerAs<int>(response_block.get(STATUS_CODE));
    std::string responseTxt = readString(response_block.get(STATUS_TEXT));

    // Get FaceId for future removal of the face
    if (responseCode == OK || responseCode == FACE_EXISTS) {
      Block status_parameter_block = response_block.get(CONTROL_PARAMETERS);
      status_parameter_block.parse();
      int face_id = readNonNegativeIntegerAs<int>(status_parameter_block.get(FACE_ID));
      entry.faceId = face_id;
      std::cout << responseCode << " " << responseTxt
                << ": Added Face (FaceId: " << entry.faceId
                << std::endl;

      auto Interest = prepareRibRegisterInterest(entry.prefix, entry.faceId, m_keyChain);
      m_face.expressInterest(Interest,
                             std::bind(&NDServer::onData, this, _2, entry),
                             nullptr, nullptr);
    }
    else {
      std::cout << "\nCreation of face failed." << std::endl;
      std::cout << "Status text: " << responseTxt << std::endl;
    }
  }
  else if (faceDestroyPrefix.isPrefixOf(data.getName())) {
    std::cout << "Face destroyed" << std::endl;
    for (auto it = m_db.begin(); it != m_db.end();) {
      bool is_Prefix = it->prefix.isPrefixOf(entry.prefix);
      if (is_Prefix) {
        std::cout << "NDND (RV): Erasing... " << it->prefix.toUri() << std::endl;
        it = m_db.erase(it);
        break;
      }
      ++it;
    }

  }
}

void
NDServer::setIP() 
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

} // namespace ndnd
} // namespace ndn
