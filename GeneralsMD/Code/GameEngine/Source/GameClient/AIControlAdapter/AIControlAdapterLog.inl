	class AdapterLog
	{
	public:
		AdapterLog() :
			m_adapterLog(nullptr),
			m_adapterLogPath("D:\\logs\\adapter.log")
		{
			char buffer[32];
			sprintf_s(buffer, "%08X%08X", static_cast<unsigned int>(::GetCurrentProcessId()), static_cast<unsigned int>(::GetTickCount()));
			m_sessionId = buffer;
		}

		~AdapterLog()
		{
			close();
		}

		const std::string& sessionId() const { return m_sessionId; }

		static std::string truncateForLog(const std::string& text, std::size_t maxLen = 1200u)
		{
			if (text.size() <= maxLen)
			{
				return text;
			}
			return text.substr(0, maxLen) + "...(truncated)";
		}

		void ensureOpen()
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

		void close()
		{
			if (m_adapterLog == nullptr)
			{
				return;
			}
			fflush(m_adapterLog);
			fclose(m_adapterLog);
			m_adapterLog = nullptr;
		}

		void log(const char* format, ...)
		{
			ensureOpen();
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

		bool configurePath(const std::string& path, bool truncateExisting)
		{
			if (path.empty())
			{
				return false;
			}

			close();
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

			ensureOpen();
			return m_adapterLog != nullptr;
		}

		bool resetFile(bool truncateExisting)
		{
			const std::string currentPath = m_adapterLogPath;
			return configurePath(currentPath, truncateExisting);
		}

	private:
		FILE* m_adapterLog;
		std::string m_adapterLogPath;
		std::string m_sessionId;
	};
