	class AdapterTransport
	{
	public:
		typedef void (*MessageHandler)(void* context, const std::string& line);

		AdapterTransport(AdapterLog* log) :
			m_log(log),
			m_pipe(INVALID_HANDLE_VALUE),
			m_hasClient(false),
			m_messageHandler(nullptr),
			m_messageHandlerContext(nullptr)
		{
		}

		~AdapterTransport()
		{
			closePipe();
		}

		void setMessageHandler(MessageHandler handler, void* context)
		{
			m_messageHandler = handler;
			m_messageHandlerContext = context;
		}

		bool hasClient() const { return m_hasClient; }
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

			resetClientConnection();
			::CloseHandle(m_pipe);
			m_pipe = INVALID_HANDLE_VALUE;
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
		}

		void acceptClientIfAvailable()
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
			while (m_hasClient)
			{
				DWORD bytesAvailable = 0;
				if (!::PeekNamedPipe(m_pipe, nullptr, 0, nullptr, &bytesAvailable, nullptr))
				{
					m_log->log("peek_pipe_failed winerr=%lu", static_cast<unsigned long>(::GetLastError()));
					resetClientConnection();
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
					resetClientConnection();
					return;
				}

				m_lineBuffer.append(buffer, bytesRead);
				processBufferedLines();
			}
		}

		void sendJsonLine(const nlohmann::json& payload)
		{
			if (!m_hasClient)
			{
				return;
			}

			std::string line = payload.dump();
			m_log->log("send_raw %s", AdapterLog::truncateForLog(line).c_str());
			line.push_back('\n');

			DWORD bytesWritten = 0;
			if (!::WriteFile(m_pipe, line.data(), static_cast<DWORD>(line.size()), &bytesWritten, nullptr))
			{
				m_log->log("write_pipe_failed winerr=%lu", static_cast<unsigned long>(::GetLastError()));
				resetClientConnection();
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
		}

	private:
		static const DWORD PIPE_BUFFER_SIZE = 8192;

		AdapterLog* m_log;
		HANDLE m_pipe;
		bool m_hasClient;
		std::string m_lineBuffer;
		MessageHandler m_messageHandler;
		void* m_messageHandlerContext;

		void processBufferedLines()
		{
			for (;;)
			{
				const std::size_t newline = m_lineBuffer.find('\n');
				if (newline == std::string::npos)
				{
					return;
				}

				std::string line = m_lineBuffer.substr(0, newline);
				m_lineBuffer.erase(0, newline + 1);

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
