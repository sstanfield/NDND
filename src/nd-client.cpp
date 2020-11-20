#include "ahclient.h"

#include <csignal>
#include <iostream>
#include <thread>
#include <unistd.h>

using namespace ndn;
using namespace ahnd;
using namespace std;

const Name BROADCAST_PREFIX("/ahnd");
constexpr int KEEPALIVE_SECONDS = 300;
constexpr int DEFAULT_PORT = 6363;
constexpr int LOOP_MS = 500;
constexpr int SHUTDOWN_DELAY_MS = 5000;

namespace {
// In the GNUC Library, sig_atomic_t is a typedef for int,
// which is atomic on all systems that are supported by the
// GNUC Library
volatile sig_atomic_t do_shutdown = 0;
// Use a lock free atomic if the above is not true.
} // namespace

void termHandler(int /*signum*/) { do_shutdown = 1; }

class Program {
  public:
	explicit Program(const ndn::Name &prefix) {
		// Init client
		m_client =
		    make_unique<AHClient>(prefix, BROADCAST_PREFIX, DEFAULT_PORT);

		m_scheduler = make_unique<Scheduler>(m_client->face().getIoService());
	}

	// TODO: remove face on SIGINT, SIGTERM

	void loop() {
		{
			// Would prefer to use sigwait and a thread but something in a lib
			// seems to start a thread before main is called and so the signals
			// can not be properly masked- pretty rude of some lib.
			struct sigaction action {};
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
			action.sa_handler = termHandler;
			sigemptyset(&action.sa_mask);
			action.sa_flags = 0;
			sigaction(SIGINT, &action, nullptr);
			sigaction(SIGTERM, &action, nullptr);
		}

		m_client->registerPrefixes();
		m_scheduler->schedule(time::seconds(KEEPALIVE_SECONDS),
		                      [this] { keepaliveLoop(); });
		while (do_shutdown == 0) {
			m_client->processEvents(LOOP_MS);
		}
		m_client->shutdown();
		m_client->processEvents(SHUTDOWN_DELAY_MS);
	}

	void keepaliveLoop() {
		m_client->sendKeepAliveInterest();
		m_scheduler->schedule(time::seconds(KEEPALIVE_SECONDS),
		                      [this] { keepaliveLoop(); });
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
