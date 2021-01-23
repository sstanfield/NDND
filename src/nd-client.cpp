#include "ahclient.h"

#include <csignal>
#include <iostream>
#include <thread>
#include <unistd.h>

#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>

using namespace ndn;
using namespace ahnd;
using namespace std;

const Name BROADCAST_PREFIX("/ahnd");
constexpr int KEEPALIVE_SECONDS = 300;
constexpr int DEFAULT_PORT = 6363;
constexpr int LOOP_MS = 500;
constexpr int SHUTDOWN_DELAY_MS = 5000;
constexpr int CLIENT_BUF_LEN = 100;
constexpr int CLIENT_SELECT_USEC = 100;
constexpr int CLIENT_LISTEN_QUEUE = 5;
constexpr int MAX_CLIENTS = 5;

namespace {
// In the GNUC Library, sig_atomic_t is a typedef for int,
// which is atomic on all systems that are supported by the
// GNUC Library
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile sig_atomic_t do_shutdown = 0;
// Use a lock free atomic if the above is not true.
} // namespace

void termHandler(int /*signum*/) { do_shutdown = 1; }

auto clientListen(const string &socket_path) -> int {
	struct sockaddr_un addr {};
	int fd = -1;

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		perror("socket error");
		exit(-1);
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
	strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
	unlink(socket_path.c_str());

	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		perror("bind error");
		exit(-1);
	}

	if (listen(fd, CLIENT_LISTEN_QUEUE) == -1) {
		perror("listen error");
		exit(-1);
	}
	return fd;
}

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
		std::string socket_path = "/tmp/ah";
		int client_fd = clientListen(socket_path);

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
			// Ignore sigpipe so the writes will return an error...
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
			signal(SIGPIPE, SIG_IGN);
		}

		m_client->registerPrefixes();
		m_scheduler->schedule(time::seconds(KEEPALIVE_SECONDS),
		                      [this] { keepaliveLoop(); });
		std::array<int, MAX_CLIENTS> client_fds{};
		for (int i = 0; i < MAX_CLIENTS; i++) {
			client_fds.at(i) = -1;
		}
		cout << "AH Client: Listening for agent clients on " << socket_path
		     << endl;
		while (do_shutdown == 0) {
			m_client->processEvents(LOOP_MS);

			fd_set rfds;
			struct timeval tv {};
			int retval = 0;

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
			FD_ZERO(&rfds);
			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
			FD_SET(client_fd, &rfds);
			int max_fd = client_fd;
			for (int i = 0; i < MAX_CLIENTS; i++) {
				if (client_fds.at(i) != -1) {
					// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
					FD_SET(client_fds.at(i), &rfds);
					if (client_fds.at(i) > max_fd) {
						max_fd = client_fds.at(i);
					}
				}
			}

			tv.tv_sec = 0;
			tv.tv_usec = CLIENT_SELECT_USEC;
			retval = select(max_fd + 1, &rfds, nullptr, nullptr, &tv);
			/* Don't rely on the value of tv now! */

			if (retval == -1) {
				perror("select()");
			} else if (retval != 0) {
				std::array<char, CLIENT_BUF_LEN> buf{};
				for (int i = 0; i < MAX_CLIENTS; i++) {
					if (client_fds.at(i) != -1 &&
					    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
					    FD_ISSET(client_fds.at(i), &rfds)) {
						int cl = client_fds.at(i);
						int rc = read(cl, buf.data(), CLIENT_BUF_LEN);
						if (rc > 0) {
							string line(buf.data());
							std::istringstream iss(line);
							std::vector<std::string> results(
							    std::istream_iterator<std::string>{iss},
							    std::istream_iterator<std::string>());
							string command = results[0];
							size_t elen =
							    command.find_last_not_of(" \n\r\t") + 1;
							if (command.length() > elen) {
								command.erase(elen);
							}
							if (command == "status") {
								m_client->getStatus(
								    [cl](const string &json) {
									    if (write(cl, json.c_str(),
									              json.length() + 1) == -1) {
										    // XXX Close fd
										    perror("AH Client: ERROR writing "
										           "to client");
									    }
								    },
								    [cl](const string &error) {
									    string message = "ERROR getting status";
									    if (write(cl, message.c_str(),
									              message.length() + 1) == -1) {
										    // XXX Close fd
										    perror("AH Client: ERROR writing "
										           "to client");
									    }
									    cout << "AH Client: Got error checking "
									            "status, "
									         << error << endl;
								    });
							} else if (command == "pier-status") {
								if (results.size() != 2) {
									cout << "AH Client: pier-status requires a "
									        "pier id"
									     << endl;
									string message =
									    "ERROR pier-status requires pier id";
									if (write(cl, message.c_str(),
									          message.length() + 1) == -1) {
										// XXX Close fd
										perror("AH Client: ERROR writing "
										       "to client");
									}
									continue;
								}
								int pier = std::stoi(results[1], nullptr);
								m_client->getPierStatus(
								    pier,
								    [cl](const string &json) {
									    if (write(cl, json.c_str(),
									              json.length() + 1) == -1) {
										    // XXX Close fd
										    perror("AH Client: ERROR writing "
										           "to client");
									    }
								    },
								    [cl](const string &error) {
									    string message = "ERROR getting status";
									    if (write(cl, message.c_str(),
									              message.length() + 1) == -1) {
										    // XXX Close fd
										    perror("AH Client: ERROR writing "
										           "to client");
									    }
									    cout << "AH Client: Got error checking "
									            "status, "
									         << error << endl;
								    });
							} else if (command == "piers") {
								stringstream pierstr;
								bool first = true;
								pierstr << "[";
								m_client->visitPiers([&first, &pierstr](
								                         const DBEntry &pier) {
									if (first) {
										pierstr << endl << "    ";
										first = false;
									} else {
										pierstr << "," << endl << "    ";
									}
									std::string ip_str(inet_ntoa(pier.ip));
									pierstr << R"({"id":)" << pier.id
									        << R"(,"faceId":)" << pier.faceId
									        << R"(,"prefix":")" << pier.prefix
									        << R"(","ip":")" << ip_str
									        << R"(","port":)" << pier.port
									        << "}";
								});
								if (!first) {
									pierstr << endl;
								}
								pierstr << "]" << endl;
								if (write(cl, pierstr.str().c_str(),
								          pierstr.str().length() + 1) == -1) {
									perror(
									    "AH Client: ERROR writing to client");
									client_fds.at(i) = -1;
									close(cl);
								}
							} else if (command == "exit") {
								cout << "AH Client: closed client at client "
								        "request"
								     << endl;
								string message = "GOODBYE!";
								if (write(cl, message.c_str(),
								          message.length() + 1) == -1) {
									perror(
									    "AH Client: ERROR writing to client");
								}
								client_fds.at(i) = -1;
								close(cl);
							} else {
								string message = "ERROR: Invalid command";
								if (write(cl, message.c_str(),
								          message.length() + 1) == -1) {
									perror(
									    "AH Client: ERROR writing to client");
									client_fds.at(i) = -1;
									close(cl);
								}
								cout << "AH Client: unknown command " << command
								     << endl;
							}
						} else if (rc == -1) {
							perror("read");
							cout << "AH Client: closed client, read error"
							     << endl;
							client_fds.at(i) = -1;
							close(cl);
						} else if (rc == 0) {
							cout << "AH Client: closed client, EOF" << endl;
							client_fds.at(i) = -1;
							close(cl);
						}
					}
				}
				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
				if (FD_ISSET(client_fd, &rfds)) {
					int cl = 0;
					if ((cl = accept(client_fd, nullptr, nullptr)) == -1) {
						perror("accept error");
					} else {
						bool found_spot = false;
						for (int i = 0; i < MAX_CLIENTS; i++) {
							if (client_fds.at(i) == -1) {
								client_fds.at(i) = cl;
								found_spot = true;
								cout << "AH Client: Agent got a client "
								        "connection"
								     << endl;
								break;
							}
						}
						if (!found_spot) {
							cout << "AH Client: Agent rejected a client "
							        "connection, to many clients"
							     << endl;
							string message = "CONNECT REJECTED";
							write(cl, message.c_str(), message.length() + 1);
							close(cl);
						}
					}
				}
			}
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
