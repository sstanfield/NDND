#include "ahclient.h"

#include <iostream>

using namespace ndn;
using namespace ahnd;
using namespace std;

const Name BROADCAST_PREFIX("/ahnd");
constexpr int KEEPALIVE_SECONDS = 300;
constexpr int DEFAULT_PORT = 6363;

class Program {
  public:
	explicit Program(const ndn::Name &prefix) {
		// Init client
		m_client =
		    make_unique<AHClient>(prefix, BROADCAST_PREFIX, DEFAULT_PORT);

		m_scheduler = make_unique<Scheduler>(m_client->face().getIoService());
		m_client->registerPrefixes();
		loop();
		m_client->processEvents();
	}

	// TODO: remove face on SIGINT, SIGTERM

	void loop() {
		m_client->sendKeepAliveInterest();
		m_scheduler->schedule(time::seconds(KEEPALIVE_SECONDS),
		                      [this] { loop(); });
	}

  private:
	std::unique_ptr<AHClient> m_client;
	std::unique_ptr<Scheduler> m_scheduler;
};

auto main(int argc, char *argv[]) -> int {
	// Suppress the pointer arithmetic lint on two lines, this is just how you
	// deal with arguments...
	if (argc < 2) {
		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
		cout << "usage: " << argv[0] << "/prefix" << endl;
		cout << "    /prefix: the ndn name for this client" << endl;
		return 1;
	}
	// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
	Program program(argv[1]);
	program.loop();
}
