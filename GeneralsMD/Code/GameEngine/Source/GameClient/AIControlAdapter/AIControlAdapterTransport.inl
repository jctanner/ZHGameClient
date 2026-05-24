	class AdapterTransport
	{
	public:
		typedef void (*MessageHandler)(void* context, const std::string& line);

		AdapterTransport(AdapterLog* log) :
			m_log(log),
			m_pipe(INVALID_HANDLE_VALUE),
			m_listenSocket(INVALID_SOCKET),
			m_winsockStarted(false),
			m_hasClient(false),
			m_messageHandler(nullptr),
			m_messageHandlerContext(nullptr)
		{
		}

		~AdapterTransport()
		{
			closePipe();
			closeTcp();
		}

		void setMessageHandler(MessageHandler handler, void* context)
		{
			m_messageHandler = handler;
			m_messageHandlerContext = context;
		}

		bool hasClient() const { return m_hasClient || !m_tcpClients.empty(); }
		bool hasListener() const { return m_pipe != INVALID_HANDLE_VALUE || m_listenSocket != INVALID_SOCKET; }
		HANDLE pipe() const { return m_pipe; }

		void ensurePipeCreated()
		{
			if (m_pipe != INVALID_HANDLE_VALUE)
			{
				return;
			}

			m_pipe = ::CreateNamedPipeA(
				"\\\\.\\pipe\\zh_ai_control",
				PIPE_ACCESS_DUPLEX,
				PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT,
				1,
				PIPE_BUFFER_SIZE,
				PIPE_BUFFER_SIZE,
				0,
				nullptr);
			if (m_pipe == INVALID_HANDLE_VALUE)
			{
				m_log->log("pipe_create_failed winerr=%lu", static_cast<unsigned long>(::GetLastError()));
			}
		}

		void closePipe()
		{
			if (m_pipe == INVALID_HANDLE_VALUE)
			{
				return;
			}

			resetPipeClientConnection();
			::CloseHandle(m_pipe);
			m_pipe = INVALID_HANDLE_VALUE;
		}

		void ensureTcpCreated()
		{
			if (m_listenSocket != INVALID_SOCKET)
			{
				return;
			}

			if (!m_winsockStarted)
			{
				WSADATA data;
				const int startupResult = ::WSAStartup(MAKEWORD(2, 2), &data);
				if (startupResult != 0)
				{
					m_log->log("tcp_winsock_start_failed wsaerr=%d", startupResult);
					return;
				}
				m_winsockStarted = true;
			}

			SOCKET listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (listenSocket == INVALID_SOCKET)
			{
				m_log->log("tcp_socket_failed wsaerr=%d", ::WSAGetLastError());
				return;
			}

			u_long nonBlocking = 1;
			if (::ioctlsocket(listenSocket, FIONBIO, &nonBlocking) == SOCKET_ERROR)
			{
				m_log->log("tcp_nonblocking_failed wsaerr=%d", ::WSAGetLastError());
				::closesocket(listenSocket);
				return;
			}

			const char reuse = 1;
			::setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

			sockaddr_in address;
			memset(&address, 0, sizeof(address));
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			address.sin_port = htons(TCP_PORT);

			if (::bind(listenSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
			{
				m_log->log("tcp_bind_failed host=127.0.0.1 port=%u wsaerr=%d", TCP_PORT, ::WSAGetLastError());
				::closesocket(listenSocket);
				return;
			}

			if (::listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
			{
				m_log->log("tcp_listen_failed host=127.0.0.1 port=%u wsaerr=%d", TCP_PORT, ::WSAGetLastError());
				::closesocket(listenSocket);
				return;
			}

			m_listenSocket = listenSocket;
			m_log->log("tcp_listening host=127.0.0.1 port=%u", TCP_PORT);
		}

		void closeTcp()
		{
			resetTcpClients();

			if (m_listenSocket != INVALID_SOCKET)
			{
				::closesocket(m_listenSocket);
				m_listenSocket = INVALID_SOCKET;
			}

			if (m_winsockStarted)
			{
				::WSACleanup();
				m_winsockStarted = false;
			}
		}

		void resetClientConnection()
		{
			if (m_pipe != INVALID_HANDLE_VALUE)
			{
				::DisconnectNamedPipe(m_pipe);
			}
			if (m_hasClient)
			{
				m_log->log("client_disconnected");
			}
			m_hasClient = false;
			resetTcpClients();
		}

		void acceptClientIfAvailable()
		{
			acceptPipeClientIfAvailable();
			acceptTcpClientsIfAvailable();
		}

		void acceptPipeClientIfAvailable()
		{
			if (m_hasClient || m_pipe == INVALID_HANDLE_VALUE)
			{
				return;
			}

			if (::ConnectNamedPipe(m_pipe, nullptr))
			{
				m_hasClient = true;
				m_log->log("client_connected");
				return;
			}

			DWORD error = ::GetLastError();
			if (error == ERROR_PIPE_CONNECTED)
			{
				m_hasClient = true;
				m_log->log("client_connected_already");
				return;
			}

			if (error == ERROR_NO_DATA)
			{
				::DisconnectNamedPipe(m_pipe);
			}
		}

		void readIncomingData()
		{
			readIncomingPipeData();
			readIncomingTcpData();
		}

		void readIncomingPipeData()
		{
			while (m_hasClient)
			{
				DWORD bytesAvailable = 0;
				if (!::PeekNamedPipe(m_pipe, nullptr, 0, nullptr, &bytesAvailable, nullptr))
				{
					m_log->log("peek_pipe_failed winerr=%lu", static_cast<unsigned long>(::GetLastError()));
					resetPipeClientConnection();
					return;
				}

				if (bytesAvailable == 0)
				{
					return;
				}

				char buffer[2048];
				DWORD requested = bytesAvailable;
				if (requested > sizeof(buffer))
				{
					requested = sizeof(buffer);
				}

				DWORD bytesRead = 0;
				if (!::ReadFile(m_pipe, buffer, requested, &bytesRead, nullptr) || bytesRead == 0)
				{
					m_log->log("read_pipe_failed winerr=%lu bytes_read=%lu", static_cast<unsigned long>(::GetLastError()), static_cast<unsigned long>(bytesRead));
					resetPipeClientConnection();
					return;
				}

				m_lineBuffer.append(buffer, bytesRead);
				processBufferedLines(m_lineBuffer);
			}
		}

		void sendJsonLine(const nlohmann::json& payload)
		{
			if (!m_hasClient && m_tcpClients.empty())
			{
				return;
			}

			std::string line = payload.dump();
			m_log->log("send_raw %s", AdapterLog::truncateForLog(line).c_str());
			line.push_back('\n');

			if (m_hasClient)
			{
				DWORD bytesWritten = 0;
				if (!::WriteFile(m_pipe, line.data(), static_cast<DWORD>(line.size()), &bytesWritten, nullptr))
				{
					m_log->log("write_pipe_failed winerr=%lu", static_cast<unsigned long>(::GetLastError()));
					resetPipeClientConnection();
				}
			}

			for (std::size_t i = 0; i < m_tcpClients.size();)
			{
				m_tcpClients[i].writeBuffer.append(line);
				if (!flushTcpClient(m_tcpClients[i]))
				{
					closeTcpClientAt(i, "tcp_write_failed");
				}
				else
				{
					++i;
				}
			}
		}

		void sendProtocolError(const std::string& requestId, const char* code, const char* reason)
		{
			DEBUG_LOG(("[AICTRL] protocol_error request_id=%s code=%s reason=%s",
				requestId.c_str(),
				code != nullptr ? code : "",
				reason != nullptr ? reason : ""));

			nlohmann::json reply = {
				{"type", "Error"},
				{"request_id", requestId},
				{"code", code},
				{"reason", reason}
			};
			sendJsonLine(reply);
		}

		void sendActionAck(const std::string& requestId, bool ok, const char* code = nullptr, const char* reason = nullptr)
		{
			DEBUG_LOG(("[AICTRL] action_ack request_id=%s ok=%d code=%s reason=%s",
				requestId.c_str(),
				ok ? 1 : 0,
				code != nullptr ? code : "",
				reason != nullptr ? reason : ""));

			nlohmann::json reply = {
				{"type", "ActionAck"},
				{"request_id", requestId},
				{"ok", ok}
			};

			if (!ok)
			{
				reply["code"] = code != nullptr ? code : "internal_error";
				reply["reason"] = reason != nullptr ? reason : "request_failed";
			}

			sendJsonLine(reply);
		}

		void sendQueryResult(const std::string& requestId, const nlohmann::json& result)
		{
			DEBUG_LOG(("[AICTRL] query_result request_id=%s ok=1", requestId.c_str()));

			nlohmann::json reply = {
				{"type", "QueryResult"},
				{"request_id", requestId},
				{"ok", true},
				{"result", result}
			};
			sendJsonLine(reply);
		}

		void sendQueryError(const std::string& requestId, const char* code, const char* reason)
		{
			DEBUG_LOG(("[AICTRL] query_result request_id=%s ok=0 code=%s reason=%s",
				requestId.c_str(),
				code != nullptr ? code : "",
				reason != nullptr ? reason : ""));

			nlohmann::json reply = {
				{"type", "QueryResult"},
				{"request_id", requestId},
				{"ok", false},
				{"code", code != nullptr ? code : "invalid_state"},
				{"reason", reason != nullptr ? reason : "query_failed"}
			};
			sendJsonLine(reply);
		}

		void clearLineBuffer()
		{
			m_lineBuffer.clear();
			for (std::size_t i = 0; i < m_tcpClients.size(); ++i)
			{
				m_tcpClients[i].lineBuffer.clear();
				m_tcpClients[i].writeBuffer.clear();
			}
		}

	private:
		static const DWORD PIPE_BUFFER_SIZE = 8192;
		static const unsigned short TCP_PORT = 51111;
		static const std::size_t MAX_TCP_CLIENTS = 4;

		struct TcpClient
		{
			SOCKET socket;
			std::string lineBuffer;
			std::string writeBuffer;
		};

		AdapterLog* m_log;
		HANDLE m_pipe;
		SOCKET m_listenSocket;
		bool m_winsockStarted;
		bool m_hasClient;
		std::string m_lineBuffer;
		std::vector<TcpClient> m_tcpClients;
		MessageHandler m_messageHandler;
		void* m_messageHandlerContext;

		void resetPipeClientConnection()
		{
			if (m_pipe != INVALID_HANDLE_VALUE)
			{
				::DisconnectNamedPipe(m_pipe);
			}
			if (m_hasClient)
			{
				m_log->log("client_disconnected");
			}
			m_hasClient = false;
		}

		void resetTcpClients()
		{
			for (std::size_t i = 0; i < m_tcpClients.size(); ++i)
			{
				::closesocket(m_tcpClients[i].socket);
			}
			if (!m_tcpClients.empty())
			{
				m_log->log("tcp_clients_disconnected count=%lu", static_cast<unsigned long>(m_tcpClients.size()));
			}
			m_tcpClients.clear();
		}

		void acceptTcpClientsIfAvailable()
		{
			if (m_listenSocket == INVALID_SOCKET)
			{
				return;
			}

			for (;;)
			{
				sockaddr_in clientAddress;
				int clientAddressSize = sizeof(clientAddress);
				SOCKET clientSocket = ::accept(m_listenSocket, reinterpret_cast<sockaddr*>(&clientAddress), &clientAddressSize);
				if (clientSocket == INVALID_SOCKET)
				{
					const int error = ::WSAGetLastError();
					if (error != WSAEWOULDBLOCK)
					{
						m_log->log("tcp_accept_failed wsaerr=%d", error);
					}
					return;
				}

				if (m_tcpClients.size() >= MAX_TCP_CLIENTS)
				{
					m_log->log("tcp_client_rejected reason=max_clients");
					::closesocket(clientSocket);
					continue;
				}

				u_long nonBlocking = 1;
				if (::ioctlsocket(clientSocket, FIONBIO, &nonBlocking) == SOCKET_ERROR)
				{
					m_log->log("tcp_client_nonblocking_failed wsaerr=%d", ::WSAGetLastError());
					::closesocket(clientSocket);
					continue;
				}

				TcpClient client;
				client.socket = clientSocket;
				m_tcpClients.push_back(client);
				m_log->log("tcp_client_connected count=%lu", static_cast<unsigned long>(m_tcpClients.size()));
			}
		}

		void readIncomingTcpData()
		{
			for (std::size_t i = 0; i < m_tcpClients.size();)
			{
				if (!flushTcpClient(m_tcpClients[i]))
				{
					closeTcpClientAt(i, "tcp_write_failed");
					continue;
				}

				bool keepClient = true;
				for (;;)
				{
					char buffer[2048];
					const int bytesRead = ::recv(m_tcpClients[i].socket, buffer, sizeof(buffer), 0);
					if (bytesRead > 0)
					{
						m_tcpClients[i].lineBuffer.append(buffer, bytesRead);
						processBufferedLines(m_tcpClients[i].lineBuffer);
						continue;
					}

					if (bytesRead == 0)
					{
						keepClient = false;
						closeTcpClientAt(i, "tcp_client_disconnected");
						break;
					}

					const int error = ::WSAGetLastError();
					if (error == WSAEWOULDBLOCK)
					{
						break;
					}

					m_log->log("tcp_read_failed wsaerr=%d", error);
					keepClient = false;
					closeTcpClientAt(i, "tcp_read_failed");
					break;
				}

				if (keepClient)
				{
					++i;
				}
			}
		}

		bool flushTcpClient(TcpClient& client)
		{
			while (!client.writeBuffer.empty())
			{
				const int bytesSent = ::send(client.socket, client.writeBuffer.data(), static_cast<int>(client.writeBuffer.size()), 0);
				if (bytesSent > 0)
				{
					client.writeBuffer.erase(0, static_cast<std::size_t>(bytesSent));
					continue;
				}

				const int error = ::WSAGetLastError();
				if (error == WSAEWOULDBLOCK)
				{
					return true;
				}

				m_log->log("tcp_send_failed wsaerr=%d", error);
				return false;
			}

			return true;
		}

		void closeTcpClientAt(std::size_t index, const char* reason)
		{
			if (index >= m_tcpClients.size())
			{
				return;
			}

			::closesocket(m_tcpClients[index].socket);
			m_tcpClients.erase(m_tcpClients.begin() + index);
			m_log->log("%s count=%lu", reason, static_cast<unsigned long>(m_tcpClients.size()));
		}

		void processBufferedLines(std::string& lineBuffer)
		{
			for (;;)
			{
				const std::size_t newline = lineBuffer.find('\n');
				if (newline == std::string::npos)
				{
					return;
				}

				std::string line = lineBuffer.substr(0, newline);
				lineBuffer.erase(0, newline + 1);

				if (!line.empty() && line[line.size() - 1] == '\r')
				{
					line.erase(line.size() - 1);
				}

				if (line.empty())
				{
					continue;
				}

				m_log->log("recv_raw %s", AdapterLog::truncateForLog(line).c_str());
				if (m_messageHandler != nullptr)
				{
					m_messageHandler(m_messageHandlerContext, line);
				}
			}
		}
	};
