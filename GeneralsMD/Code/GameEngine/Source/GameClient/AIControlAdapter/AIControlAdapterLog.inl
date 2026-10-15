// Diagnostic text writes belong to one worker. Producers never wait for its
// lock or disk; queue saturation and writer failures are exposed in telemetry.
class AdapterLog
{
    struct Line { char text[4096] = {}; std::size_t length = 0; };
    std::array<Line, 512> m_lines{};
    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::thread m_writer;
    std::atomic<bool> m_stopping{false};
    std::atomic<std::uint64_t> m_dropped{0}, m_errors{0}, m_written{0}, m_flushes{0};
    std::size_t m_start = 0, m_count = 0;
    char m_path[1024] = "D:\\logs\\adapter.log";
    bool m_configurePending = true, m_truncatePending = false;
    std::string m_sessionId;

    void writerMain()
    {
        FILE* file = nullptr;
        DWORD lastFlush = ::GetTickCount();
        std::array<Line, 64> batch{};
        for (;;)
        {
            std::size_t count = 0;
            bool configure = false, truncate = false;
            char path[1024] = {};
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_ready.wait_for(lock, std::chrono::milliseconds(250), [&] { return m_count > 0 || m_configurePending || m_stopping.load(); });
                configure = m_configurePending; truncate = m_truncatePending;
                if (configure) { std::memcpy(path, m_path, sizeof(path)); m_configurePending = m_truncatePending = false; }
                count = std::min<std::size_t>(batch.size(), m_count);
                for (std::size_t i = 0; i < count; ++i) { batch[i] = m_lines[m_start]; m_start = (m_start + 1) % m_lines.size(); }
                m_count -= count;
            }
            if (configure)
            {
                if (file != nullptr) { if (fflush(file) != 0) ++m_errors; fclose(file); }
                const std::string filePath(path);
                const auto slash = filePath.find_last_of("\\/");
                if (slash != std::string::npos) ::CreateDirectoryA(filePath.substr(0, slash).c_str(), nullptr);
                file = _fsopen(path, truncate ? "w" : "a", _SH_DENYNO);
                if (file == nullptr) ++m_errors;
            }
            if (file != nullptr)
            {
                for (std::size_t i = 0; i < count; ++i)
                {
                    if (fwrite(batch[i].text, 1, batch[i].length, file) != batch[i].length || fputc('\n', file) == EOF) ++m_errors;
                    else ++m_written;
                }
                if (::GetTickCount() - lastFlush >= 1000u || m_stopping.load())
                {
                    if (fflush(file) != 0) ++m_errors;
                    ++m_flushes; lastFlush = ::GetTickCount();
                }
            }
            else m_dropped += count;
            if (m_stopping.load())
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_count == 0) break;
            }
        }
        if (file != nullptr) { if (fflush(file) != 0) ++m_errors; fclose(file); }
    }

public:
    AdapterLog()
    {
        char buffer[32];
        sprintf_s(buffer, "%08X%08X", static_cast<unsigned int>(::GetCurrentProcessId()), static_cast<unsigned int>(::GetTickCount()));
        m_sessionId = buffer;
        m_writer = std::thread([this] { writerMain(); });
    }
    ~AdapterLog() { close(); }
    const std::string& sessionId() const { return m_sessionId; }
    std::uint64_t dropped() const { return m_dropped.load(); }
    std::uint64_t errors() const { return m_errors.load(); }
    std::uint64_t written() const { return m_written.load(); }
    std::uint64_t flushes() const { return m_flushes.load(); }
    static std::string truncateForLog(const std::string& text, std::size_t maxLen = 1200u)
    {
        return text.size() <= maxLen ? text : text.substr(0, maxLen) + "...(truncated)";
    }
    void ensureOpen() { m_ready.notify_one(); }
    void close()
    {
        m_stopping = true; m_ready.notify_one();
        if (m_writer.joinable()) m_writer.join();
    }
    void log(const char* format, ...)
    {
        Line line;
        SYSTEMTIME st; ::GetLocalTime(&st);
        const int prefix = snprintf(line.text, sizeof(line.text), "[%02u:%02u:%02u.%03u] ",
            static_cast<unsigned int>(st.wHour), static_cast<unsigned int>(st.wMinute),
            static_cast<unsigned int>(st.wSecond), static_cast<unsigned int>(st.wMilliseconds));
        va_list args; va_start(args, format);
        vsnprintf(line.text + prefix, sizeof(line.text) - prefix, format, args); va_end(args);
        line.length = std::strlen(line.text);
        std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
        if (!lock.owns_lock() || m_count == m_lines.size() || m_stopping.load()) { ++m_dropped; return; }
        m_lines[(m_start + m_count++) % m_lines.size()] = line;
        lock.unlock(); m_ready.notify_one();
    }
    // Acknowledges enqueueing the change. The worker opens/truncates the file;
    // eventual IO failures are visible through errors().
    bool configurePath(const std::string& path, bool truncateExisting)
    {
        if (path.empty() || path.size() >= sizeof(m_path)) return false;
        std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        std::memcpy(m_path, path.c_str(), path.size() + 1);
        m_configurePending = true; m_truncatePending = truncateExisting;
        lock.unlock(); m_ready.notify_one(); return true;
    }
    bool resetFile(bool truncateExisting)
    {
        std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        m_configurePending = true; m_truncatePending = truncateExisting;
        lock.unlock(); m_ready.notify_one(); return true;
    }
};
