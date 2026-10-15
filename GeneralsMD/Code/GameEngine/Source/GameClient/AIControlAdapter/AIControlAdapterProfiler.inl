// Included inside AIControlAdapterState. Wall-time profile of adapter update phases
// and incoming commands, to locate frame spikes. Sections accumulate count/total/max
// and slow-frame membership; frames above kSlowFrameUs log a per-section breakdown.
        static constexpr std::uint64_t kSlowFrameUs = 25000u;
        struct ProfileSectionStats
        {
            std::uint64_t count = 0, totalUs = 0, maxUs = 0, slowFrames = 0;
        };
        std::map<std::string, ProfileSectionStats> m_profileSections;
        std::vector<std::pair<std::string, std::uint64_t>> m_profileFrame;
        std::uint64_t m_profileSlowFrames = 0;
        std::string m_profileCurrentCmd;

        static std::uint64_t profileNowUs()
        {
            LARGE_INTEGER counter, frequency;
            ::QueryPerformanceCounter(&counter); ::QueryPerformanceFrequency(&frequency);
            return frequency.QuadPart > 0 ? static_cast<std::uint64_t>(counter.QuadPart) * 1000000u / static_cast<std::uint64_t>(frequency.QuadPart) : 0u;
        }

        void recordProfileSection(const std::string& name, std::uint64_t elapsedUs)
        {
            ProfileSectionStats& stats = m_profileSections[name];
            ++stats.count;
            stats.totalUs += elapsedUs;
            stats.maxUs = std::max(stats.maxUs, elapsedUs);
            m_profileFrame.emplace_back(name, elapsedUs);
        }

        struct ProfileScope
        {
            AIControlAdapterState& owner; const char* name; std::uint64_t started;
            ProfileScope(AIControlAdapterState& state, const char* section) : owner(state), name(section), started(profileNowUs()) {}
            ~ProfileScope() { owner.recordProfileSection(name, profileNowUs() - started); }
        };

        // Called at the end of each update with the whole-frame duration.
        void finishProfileFrame(std::uint64_t frameUs)
        {
            if (frameUs >= kSlowFrameUs)
            {
                ++m_profileSlowFrames;
                std::map<std::string, std::uint64_t> merged;
                for (const auto& entry : m_profileFrame) merged[entry.first] += entry.second;
                std::vector<std::pair<std::string, std::uint64_t>> ordered(merged.begin(), merged.end());
                std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
                std::string detail;
                for (const auto& entry : ordered)
                {
                    m_profileSections[entry.first].slowFrames += 1;
                    if (entry.second < 1000u || detail.size() > 900u) continue;
                    detail += " " + entry.first + "=" + std::to_string(entry.second / 1000u) + "ms";
                }
                adapterLog("adapter_slow_frame total_ms=%llu tick=%u%s",
                    static_cast<unsigned long long>(frameUs / 1000u), adapterGameplayTick(), detail.c_str());
            }
            m_profileFrame.clear();
        }

        nlohmann::json buildAdapterProfile() const
        {
            std::vector<std::pair<std::string, ProfileSectionStats>> ordered(m_profileSections.begin(), m_profileSections.end());
            std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.second.totalUs > b.second.totalUs; });
            nlohmann::json sections = nlohmann::json::array();
            for (const auto& entry : ordered)
            {
                sections.push_back({{"name", entry.first}, {"count", entry.second.count},
                    {"total_ms", entry.second.totalUs / 1000u}, {"max_ms", entry.second.maxUs / 1000u},
                    {"avg_us", entry.second.count > 0 ? entry.second.totalUs / entry.second.count : 0u},
                    {"slow_frames", entry.second.slowFrames}});
            }
            return {{"slow_frame_threshold_ms", kSlowFrameUs / 1000u}, {"slow_frames", m_profileSlowFrames},
                {"update_count", m_recordingUpdateSamples}, {"update_total_ms", m_recordingUpdateUs / 1000u},
                {"update_max_ms", m_recordingUpdateMaxUs / 1000u}, {"sections", sections}};
        }

        void resetAdapterProfile()
        {
            m_profileSections.clear();
            m_profileFrame.clear();
            m_profileSlowFrames = 0;
        }
