// Included inside AIControlAdapterState. Operator game speed for local matches.
// The engine steps fixed 1/30 s logic frames at 30 * multiplier per wall second
// (more steps per render frame when frames slow), so the simulation clock and every
// adapter timer derived from it stay exact. Achieved speed is bounded by CPU time.
        static constexpr int kMaxGameSpeedMultiplier = 8;
        int m_gameSpeedMultiplier = 1;
        DWORD m_gameSpeedWindowWall = 0u;
        UnsignedInt m_gameSpeedWindowFrame = 0u;
        int m_gameSpeedWindowRenderFrames = 0;
        double m_gameSpeedAchieved = 0.0;
        double m_gameSpeedRenderFps = 0.0;

        bool gameSpeedApplies() const
        {
            return m_gameSpeedMultiplier > 1 && TheNetwork == nullptr && TheGameLogic != nullptr
                && TheGameLogic->isInGame() && !TheGameLogic->isGamePaused();
        }

        int activeSpeedMultiplier() const
        {
            return gameSpeedApplies() ? m_gameSpeedMultiplier : 1;
        }

        void resetGameSpeed()
        {
            m_gameSpeedMultiplier = 1;
            m_gameSpeedWindowWall = 0u;
            m_gameSpeedWindowFrame = 0u;
            m_gameSpeedWindowRenderFrames = 0;
            m_gameSpeedAchieved = 0.0;
            m_gameSpeedRenderFps = 0.0;
        }

        // Called once per render frame; measures simulated over wall time every ~2 s.
        void sampleGameSpeed()
        {
            const DWORD wall = ::GetTickCount();
            const UnsignedInt frame = TheGameLogic != nullptr ? TheGameLogic->getFrame() : 0u;
            ++m_gameSpeedWindowRenderFrames;
            if (m_gameSpeedWindowWall == 0u || frame < m_gameSpeedWindowFrame)
            {
                m_gameSpeedWindowWall = wall;
                m_gameSpeedWindowFrame = frame;
                m_gameSpeedWindowRenderFrames = 0;
                return;
            }
            const DWORD elapsed = wall - m_gameSpeedWindowWall;
            if (elapsed < 2000u) return;
            const double simulatedMs = static_cast<double>(frame - m_gameSpeedWindowFrame) * 1000.0 / LOGICFRAMES_PER_SECOND;
            m_gameSpeedAchieved = simulatedMs / static_cast<double>(elapsed);
            m_gameSpeedRenderFps = m_gameSpeedWindowRenderFrames * 1000.0 / static_cast<double>(elapsed);
            m_gameSpeedWindowWall = wall;
            m_gameSpeedWindowFrame = frame;
            m_gameSpeedWindowRenderFrames = 0;
        }

        nlohmann::json buildGameSpeedState() const
        {
            const char* state = m_gameSpeedMultiplier <= 1 ? "normal"
                : TheNetwork != nullptr ? "network_game_ignored"
                : TheGameLogic == nullptr || !TheGameLogic->isInGame() ? "waiting_for_match"
                : TheGameLogic->isGamePaused() ? "paused" : "accelerated";
            return {
                {"requested_multiplier", m_gameSpeedMultiplier},
                {"active_multiplier", activeSpeedMultiplier()},
                {"max_multiplier", kMaxGameSpeedMultiplier},
                {"achieved_multiplier", std::round(m_gameSpeedAchieved * 100.0) / 100.0},
                {"render_fps", std::round(m_gameSpeedRenderFps * 10.0) / 10.0},
                {"state", state}
            };
        }

        bool executeGameSetSpeed(const nlohmann::json& message, const std::string& requestId, std::string& reason)
        {
            const nlohmann::json args = message.value("args", nlohmann::json::object());
            const auto it = args.find("multiplier");
            if (it == args.end() || !it->is_number_integer())
            {
                reason = "invalid_multiplier";
                return false;
            }
            const auto value = it->get<long long>();
            if (value < 1 || value > kMaxGameSpeedMultiplier)
            {
                reason = "multiplier_out_of_range";
                return false;
            }
            if (TheNetwork != nullptr)
            {
                reason = "network_game";
                return false;
            }
            const int previous = m_gameSpeedMultiplier;
            m_gameSpeedMultiplier = static_cast<int>(value);
            if (previous != m_gameSpeedMultiplier)
            {
                // Restart the measurement window so achieved speed reflects the new setting.
                m_gameSpeedWindowWall = 0u;
                m_gameSpeedAchieved = 0.0;
                const std::string detail = std::to_string(previous) + "->" + std::to_string(m_gameSpeedMultiplier);
                appendRecordingEvent("speed", "game_speed", detail.c_str(), 0, 0, "operator", requestId.c_str(), "changed");
                adapterLog("game_speed_changed multiplier=%d previous=%d request_id=%s", m_gameSpeedMultiplier, previous, requestId.c_str());
            }
            return true;
        }
