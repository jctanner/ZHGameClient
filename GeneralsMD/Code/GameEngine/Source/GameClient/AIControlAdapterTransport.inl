		static std::string truncateForLog(const std::string& text, std::size_t maxLen = 1200u)
		{
			if (text.size() <= maxLen)
			{
				return text;
			}
			return text.substr(0, maxLen) + "...(truncated)";
		}

		void ensureAdapterLogOpen()
		{
			if (m_adapterLog != nullptr)
			{
				return;
			}

			const std::size_t slash = m_adapterLogPath.find_last_of("\\/");
			if (slash != std::string::npos)
			{
				const std::string dir = m_adapterLogPath.substr(0, slash);
				if (!dir.empty())
				{
					::CreateDirectoryA(dir.c_str(), nullptr);
				}
			}
			m_adapterLog = _fsopen(m_adapterLogPath.c_str(), "a", _SH_DENYNO);
			if (m_adapterLog != nullptr)
			{
				SYSTEMTIME st;
				::GetLocalTime(&st);
				fprintf(
					m_adapterLog,
					"[%02u:%02u:%02u.%03u] adapter_log_opened pid=%lu session=%s path=%s\n",
					static_cast<unsigned int>(st.wHour),
					static_cast<unsigned int>(st.wMinute),
					static_cast<unsigned int>(st.wSecond),
					static_cast<unsigned int>(st.wMilliseconds),
					static_cast<unsigned long>(::GetCurrentProcessId()),
					m_sessionId.c_str(),
					m_adapterLogPath.c_str());
				fflush(m_adapterLog);
			}
		}

		void closeAdapterLog()
		{
			if (m_adapterLog == nullptr)
			{
				return;
			}
			fflush(m_adapterLog);
			fclose(m_adapterLog);
			m_adapterLog = nullptr;
		}

		void adapterLog(const char* format, ...)
		{
			ensureAdapterLogOpen();
			if (m_adapterLog == nullptr)
			{
				return;
			}

			SYSTEMTIME st;
			::GetLocalTime(&st);
			fprintf(
				m_adapterLog,
				"[%02u:%02u:%02u.%03u] ",
				static_cast<unsigned int>(st.wHour),
				static_cast<unsigned int>(st.wMinute),
				static_cast<unsigned int>(st.wSecond),
				static_cast<unsigned int>(st.wMilliseconds));

			va_list args;
			va_start(args, format);
			vfprintf(m_adapterLog, format, args);
			va_end(args);
			fputc('\n', m_adapterLog);
			fflush(m_adapterLog);
		}

		bool configureAdapterLogPath(const std::string& path, bool truncateExisting)
		{
			if (path.empty())
			{
				return false;
			}

			closeAdapterLog();
			m_adapterLogPath = path;

			const std::size_t slash = m_adapterLogPath.find_last_of("\\/");
			if (slash != std::string::npos)
			{
				const std::string dir = m_adapterLogPath.substr(0, slash);
				if (!dir.empty())
				{
					::CreateDirectoryA(dir.c_str(), nullptr);
				}
			}

			if (truncateExisting)
			{
				FILE* truncateFile = _fsopen(m_adapterLogPath.c_str(), "w", _SH_DENYNO);
				if (truncateFile == nullptr)
				{
					return false;
				}
				fclose(truncateFile);
			}

			ensureAdapterLogOpen();
			return m_adapterLog != nullptr;
		}

		bool resetAdapterLogFile(bool truncateExisting)
		{
			const std::string currentPath = m_adapterLogPath;
			return configureAdapterLogPath(currentPath, truncateExisting);
		}

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
				adapterLog("pipe_create_failed winerr=%lu", static_cast<unsigned long>(::GetLastError()));
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
				adapterLog("client_disconnected");
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
				adapterLog("client_connected");
				return;
			}

			DWORD error = ::GetLastError();
			if (error == ERROR_PIPE_CONNECTED)
			{
				m_hasClient = true;
				adapterLog("client_connected_already");
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
					adapterLog("peek_pipe_failed winerr=%lu", static_cast<unsigned long>(::GetLastError()));
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
					adapterLog("read_pipe_failed winerr=%lu bytes_read=%lu", static_cast<unsigned long>(::GetLastError()), static_cast<unsigned long>(bytesRead));
					resetClientConnection();
					return;
				}

				m_lineBuffer.append(buffer, bytesRead);
				processBufferedLines();
			}
		}

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

				adapterLog("recv_raw %s", truncateForLog(line).c_str());
				handleMessage(line);
			}
		}
