#include <winsock2.h>
#include <Ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32")

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
using namespace std;


SOCKET udp_socket = INVALID_SOCKET;
enum program_mode { send_mode, receive_mode };


void print_usage(void)
{
	cout << "  USAGE:" << endl;
	cout << "   Receive mode:" << endl;
	cout << "    udpspeed PORT_NUMBER" << endl;
	cout << endl;
	cout << "   Send mode:" << endl;
	cout << "    udpspeed TARGET_HOST PORT_NUMBER" << endl;
	cout << endl;
	cout << "   ie:" << endl;
	cout << "    Receive mode: udpspeed 1920" << endl;
	cout << "    Send mode:    udpspeed www 342" << endl;
	cout << "    Send mode:    udpspeed 127.0.0.1 950" << endl;
	cout << endl;
}

bool verify_port(const string& port_string, unsigned long int& port_number)
{
	for (size_t i = 0; i < port_string.length(); i++)
	{
		if (!isdigit(port_string[i]))
		{
			cout << "  Invalid port: " << port_string << endl;
			cout << "  Ports are specified by numerals only." << endl;
			return false;
		}
	}

	istringstream iss(port_string);
	iss >> port_number;

	if (port_string.length() > 5 || port_number > 65535 || port_number == 0)
	{
		cout << "  Invalid port: " << port_string << endl;
		cout << "  Port must be in the range of 1-65535" << endl;
		return false;
	}

	return true;
}

bool init_winsock(void)
{
	WSADATA wsa_data;
	WORD ver_requested = MAKEWORD(2, 2);

	if (WSAStartup(ver_requested, &wsa_data))
	{
		cout << "Could not initialize Winsock 2.2.";
		return false;
	}

	if (LOBYTE(wsa_data.wVersion) != 2 || HIBYTE(wsa_data.wVersion) != 2)
	{
		cout << "Required version of Winsock (2.2) not available.";
		return false;
	}

	return true;
}

bool init_options(const int& argc, char** argv, enum program_mode& mode, string& target_host_string, long unsigned int& port_number)
{
	if (!init_winsock())
		return false;

	string port_string = "";

	if (2 == argc)
	{
		mode = receive_mode;
		port_string = argv[1];
	}
	else if (3 == argc)
	{
		mode = send_mode;
		target_host_string = argv[1];
		port_string = argv[2];
	}
	else
	{
		print_usage();
		return false;
	}

	cout.setf(ios::fixed, ios::floatfield);
	cout.precision(2);

	return verify_port(port_string, port_number);
}

void cleanup(void)
{
	// if the socket is still open, close it
	if (INVALID_SOCKET != udp_socket)
		closesocket(udp_socket);

	// shut down winsock
	WSACleanup();
}


class packet
{
public:

	vector<char> packet_buf;
	std::chrono::high_resolution_clock::time_point time_stamp;
};


class stats
{
public:

	long long unsigned int total_elapsed_ticks = 0;
	long long unsigned int total_bytes_received = 0;
	long long unsigned int last_reported_at_ticks = 0;
	long long unsigned int last_reported_total_bytes_received = 0;

	double bytes_per_second = 0;

	double record_bps = 0;
};


void thread_func(atomic_bool& stop, atomic_bool& thread_done, vector<packet>& vc, mutex& m, stats &s, string &ip_addr)
{
	thread_done = false;

	// One million microseconds per second
	static const unsigned long long int ticks_per_second = 1000000;

	while (!stop)
	{
		m.lock();

		if (vc.size() > 0)
		{
			for (size_t i = 0; i < vc.size(); i++)
			{
			/*	cout << "packet " << vc[i].ip_addr << endl;*/

				s.total_bytes_received += vc[i].packet_buf.size();
			}

			const std::chrono::high_resolution_clock::time_point end_time = std::chrono::high_resolution_clock::now();
			const std::chrono::duration<float, std::micro> elapsed = end_time - vc[0].time_stamp;

			s.total_elapsed_ticks += static_cast<unsigned long long int>(elapsed.count());

			if (s.total_elapsed_ticks >= s.last_reported_at_ticks + ticks_per_second)
			{
				const long long unsigned int actual_ticks = s.total_elapsed_ticks - s.last_reported_at_ticks;
				const long long unsigned int bytes_sent_received_between_reports = s.total_bytes_received - s.last_reported_total_bytes_received;

				s.bytes_per_second = static_cast<double>(bytes_sent_received_between_reports) / (static_cast<double>(actual_ticks) / static_cast<double>(ticks_per_second));

				if (s.bytes_per_second > s.record_bps)
					s.record_bps = s.bytes_per_second;

				s.last_reported_at_ticks = s.total_elapsed_ticks;
				s.last_reported_total_bytes_received = s.total_bytes_received;

				if (0.0 == s.bytes_per_second)
				{
					cout << "  " << ip_addr << " -- time out." << endl;

					stop = true;
					thread_done = true;
					m.unlock();
					return;
				}
				else
				{
					static const double mbits_factor = 8.0 / (1024.0 * 1024.0);

					cout << "  " << ip_addr << " -- " << s.bytes_per_second * mbits_factor << " Mbit/s, Record: " << s.record_bps * mbits_factor << " Mbit/s" << endl;
				}
			}

			vc.clear();
		}



		m.unlock();
	}

	thread_done = true;
}


class recv_stats
{
public:

	stats s;
	thread t;
	atomic_bool stop = false;
	atomic_bool thread_done = false;
	mutex m;
	string ip_addr;

	vector<packet> packets;

	recv_stats(void)
	{
		t = thread(thread_func, ref(stop), ref(thread_done), ref(packets), ref(m), ref(s), ref(ip_addr));
	}

	~recv_stats(void)
	{
		stop = true;

		while (false == thread_done)
		{
			// cout << "Waiting for thread" << endl;
		}

		t.join();
	}
};


int main(int argc, char** argv)
{
	cout << endl << "udpspeed_2 1.0 - UDP speed tester" << endl << "Copyright 2021, Shawn Halayka" << endl << endl;

	program_mode mode = receive_mode;

	string target_host_string = "";
	long unsigned int port_number = 0;

	const long unsigned int tx_buf_size = 1450;
	vector<char> tx_buf(tx_buf_size, 0);

	const long unsigned int rx_buf_size = 8196;
	vector<char> rx_buf(rx_buf_size, 0);

	if (!init_options(argc, argv, mode, target_host_string, port_number))
	{
		cleanup();
		return 1;
	}

	if (send_mode == mode)
	{
		cout << "  Sending on port " << port_number << " - CTRL+C to exit." << endl;

		struct addrinfo hints;
		struct addrinfo* result;

		memset(&hints, 0, sizeof(struct addrinfo));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_flags = 0;
		hints.ai_protocol = IPPROTO_UDP;

		ostringstream oss;
		oss << port_number;

		if (0 != getaddrinfo(target_host_string.c_str(), oss.str().c_str(), &hints, &result))
		{
			cout << "  getaddrinfo error." << endl;
			freeaddrinfo(result);
			cleanup();
			return 2;
		}

		if (INVALID_SOCKET == (udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)))
		{
			cout << "  Could not allocate a new socket." << endl;
			freeaddrinfo(result);
			cleanup();
			return 3;
		}

		while (1)
		{
			if (SOCKET_ERROR == (sendto(udp_socket, &tx_buf[0], tx_buf_size, 0, result->ai_addr, sizeof(struct sockaddr))))
			{
				cout << "  Socket sendto error." << endl;
				freeaddrinfo(result);
				cleanup();
				return 4;
			}
		}

		freeaddrinfo(result);
	}
	else if (receive_mode == mode)
	{
		cout << "  Receiving on UDP port " << port_number << " - CTRL+C to exit." << endl;

		struct sockaddr_in my_addr;
		struct sockaddr_in their_addr;
		int addr_len = 0;

		my_addr.sin_family = AF_INET;
		my_addr.sin_port = htons(static_cast<unsigned short int>(port_number));
		my_addr.sin_addr.s_addr = INADDR_ANY;
		memset(&(my_addr.sin_zero), '\0', 8);
		addr_len = sizeof(struct sockaddr);

		if (INVALID_SOCKET == (udp_socket = socket(AF_INET, SOCK_DGRAM, 0)))
		{
			cout << "  Could not allocate a new socket." << endl;
			cleanup();
			return 5;
		}

		if (SOCKET_ERROR == bind(udp_socket, reinterpret_cast<struct sockaddr*>(&my_addr), sizeof(struct sockaddr)))
		{
			cout << "  Could not bind socket to port " << port_number << "." << endl;
			cleanup();
			return 6;
		}

		map<string, recv_stats> senders;

		while (1)
		{
			std::chrono::high_resolution_clock::time_point start_loop_time = std::chrono::high_resolution_clock::now();

			timeval timeout;
			timeout.tv_sec = 0;
			timeout.tv_usec = 100000; // one hundred thousand microseconds is one-tenth of a second

			fd_set fds;
			FD_ZERO(&fds);
			FD_SET(udp_socket, &fds);

			int select_ret = select(0, &fds, 0, 0, &timeout);

			if (SOCKET_ERROR == select_ret)
			{
				cout << "  Socket select error." << endl;
				cleanup();
				return 7;
			}
			else if(0 < select_ret)
			{
				int temp_bytes_received = 0;

				if (SOCKET_ERROR == (temp_bytes_received = recvfrom(udp_socket, &rx_buf[0], rx_buf_size, 0, reinterpret_cast<struct sockaddr*>(&their_addr), &addr_len)))
				{
					cout << "  Socket recvfrom error." << endl;
					cleanup();
					return 8;
				}

				ostringstream oss;
				oss << static_cast<int>(their_addr.sin_addr.S_un.S_un_b.s_b1) << ".";
				oss << static_cast<int>(their_addr.sin_addr.S_un.S_un_b.s_b2) << ".";
				oss << static_cast<int>(their_addr.sin_addr.S_un.S_un_b.s_b3) << ".";
				oss << static_cast<int>(their_addr.sin_addr.S_un.S_un_b.s_b4);

				packet p;
				p.packet_buf = rx_buf;
				p.packet_buf.resize(temp_bytes_received);
				p.time_stamp = start_loop_time;

				senders[oss.str()].m.lock();
				senders[oss.str()].ip_addr = oss.str();
				senders[oss.str()].packets.push_back(p);
				senders[oss.str()].m.unlock();
			}
		}
	}

	cleanup();

	return 0;
}
