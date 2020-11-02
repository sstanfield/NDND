#include "ahclient.hpp"

#include <iostream>

using namespace ndn;
using namespace ahnd;
using namespace std;

const Name SERVER_PREFIX("/ahnd");
//const Name SERVER_DISCOVERY_PREFIX("/ndn/nd/arrival");
//const uint64_t SERVER_DISCOVERY_ROUTE_COST(0);
//const time::milliseconds SERVER_DISCOVERY_ROUTE_EXPIRATION = 30_s;
//const time::milliseconds SERVER_DISCOVERY_INTEREST_LIFETIME = 4_s;

class Options
{
public:
    Options()
            : m_prefix("/test/01/02")
            , server_prefix("/ndn/nd")
    {
    }
public:
    ndn::Name m_prefix;
    ndn::Name server_prefix;
};


class Program
{
public:
    explicit Program(const Options& options)
            : m_options(options)
    {
        // Init client
        m_client = make_unique<AHClient>(m_options.m_prefix, SERVER_PREFIX);

        m_scheduler = make_unique<Scheduler>(m_client->face().getIoService());
        m_client->registerPrefixes();
        loop();
        m_client->processEvents();
    }

// TODO: remove face on SIGINT, SIGTERM

    void loop() {
        m_client->sendKeepAliveInterest();
        m_scheduler->schedule(time::seconds(300), [this] {
            loop();
        });
    }

private:
    std::unique_ptr<AHClient> m_client;
    const Options m_options;
    std::unique_ptr<Scheduler> m_scheduler;
};


int
main(int argc, char** argv)
{
    if (argc < 2) {
        cout << "usage: " << argv[0] << "/prefix" << endl;
        cout <<"    /prefix: the ndn name for this client" << endl;
        return 1;
    }
    Options opt;
    opt.m_prefix = argv[1];
    Program program(opt);
    program.loop();
}
