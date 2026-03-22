		bool executeGameCameraSet(const nlohmann::json& message, std::string& reason)
		{
			if (TheTacticalView == nullptr)
			{
				reason = "camera_not_ready";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			bool touched = false;
			Real angle = TheTacticalView->getAngle();
			Real pitch = TheTacticalView->getPitch();
			Real zoom = TheTacticalView->getZoom();
			Real heightAboveGround = TheTacticalView->getHeightAboveGround();
			bool setHeight = false;

			const auto angleIt = argsIt->find("angle");
			if (angleIt != argsIt->end() && angleIt->is_number())
			{
				angle = angleIt->get<Real>();
				touched = true;
			}

			const auto topDownIt = argsIt->find("top_down");
			if (topDownIt != argsIt->end() && topDownIt->is_boolean() && topDownIt->get<bool>())
			{
				// ~-90 degrees in radians.
				pitch = -1.57079632679f;
				touched = true;
			}

			const auto pitchIt = argsIt->find("pitch");
			if (pitchIt != argsIt->end() && pitchIt->is_number())
			{
				pitch = pitchIt->get<Real>();
				touched = true;
			}

			const auto zoomIt = argsIt->find("zoom");
			if (zoomIt != argsIt->end() && zoomIt->is_number())
			{
				zoom = zoomIt->get<Real>();
				touched = true;
			}

			const auto zoomMulIt = argsIt->find("zoom_multiplier");
			if (zoomMulIt != argsIt->end() && zoomMulIt->is_number())
			{
				const Real mul = zoomMulIt->get<Real>();
				if (mul > 0.0f)
				{
					zoom = TheTacticalView->getZoom() * mul;
					touched = true;
				}
			}

			const auto heightIt = argsIt->find("height");
			if (heightIt != argsIt->end() && heightIt->is_number())
			{
				heightAboveGround = heightIt->get<Real>();
				setHeight = true;
				touched = true;
			}

			const auto heightMulIt = argsIt->find("height_multiplier");
			if (heightMulIt != argsIt->end() && heightMulIt->is_number())
			{
				const Real mul = heightMulIt->get<Real>();
				if (mul > 0.0f)
				{
					heightAboveGround = TheTacticalView->getHeightAboveGround() * mul;
					setHeight = true;
					touched = true;
				}
			}

			if (!touched)
			{
				reason = "no_camera_fields";
				return false;
			}

			TheTacticalView->setAngle(angle);
			TheTacticalView->setPitch(pitch);
			TheTacticalView->setZoom(zoom);
			if (setHeight)
			{
				TheTacticalView->setHeightAboveGround(heightAboveGround);
			}
			return true;
		}

		bool executeGameCameraSetZoomLimited(const nlohmann::json& message, std::string& reason)
		{
			if (TheTacticalView == nullptr)
			{
				reason = "camera_not_ready";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			bool hasValue = false;
			bool enabled = true;

			const auto enabledIt = argsIt->find("enabled");
			if (enabledIt != argsIt->end() && enabledIt->is_boolean())
			{
				enabled = enabledIt->get<bool>();
				hasValue = true;
			}

			const auto zoomLimitedIt = argsIt->find("zoom_limited");
			if (zoomLimitedIt != argsIt->end() && zoomLimitedIt->is_boolean())
			{
				enabled = zoomLimitedIt->get<bool>();
				hasValue = true;
			}

			if (!hasValue)
			{
				reason = "missing_enabled";
				return false;
			}

			TheTacticalView->setZoomLimited(enabled ? TRUE : FALSE);
			return true;
		}

		bool executeGameCameraReset(std::string& reason)
		{
			if (TheTacticalView == nullptr)
			{
				reason = "camera_not_ready";
				return false;
			}

			TheTacticalView->setAngleAndPitchToDefault();
			TheTacticalView->setZoomToDefault();
			return true;
		}

		bool executeGameCameraLookAt(const nlohmann::json& message, std::string& reason)
		{
			if (TheTacticalView == nullptr)
			{
				reason = "camera_not_ready";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			Real x = 0.0f;
			Real y = 0.0f;
			bool hasPos = false;
			const auto xIt = argsIt->find("x");
			const auto yIt = argsIt->find("y");
			if (xIt != argsIt->end() && yIt != argsIt->end() && xIt->is_number() && yIt->is_number())
			{
				x = xIt->get<Real>();
				y = yIt->get<Real>();
				hasPos = true;
			}
			if (!hasPos)
			{
				const auto centerIt = argsIt->find("zone_center");
				if (centerIt != argsIt->end() && centerIt->is_object())
				{
					const auto cxIt = centerIt->find("x");
					const auto cyIt = centerIt->find("y");
					if (cxIt != centerIt->end() && cyIt != centerIt->end() && cxIt->is_number() && cyIt->is_number())
					{
						x = cxIt->get<Real>();
						y = cyIt->get<Real>();
						hasPos = true;
					}
				}
			}
			if (!hasPos)
			{
				reason = "missing_target_position";
				return false;
			}

			Coord3D target;
			target.x = x;
			target.y = y;
			target.z = 0.0f;
			TheTacticalView->lookAt(&target);
			return true;
		}

		bool executeGameCameraGet(const nlohmann::json& /*message*/, nlohmann::json& result, std::string& reason)
		{
			if (TheTacticalView == nullptr)
			{
				reason = "camera_not_ready";
				return false;
			}

			Coord3D pos;
			TheTacticalView->getPosition(&pos);
			result = nlohmann::json::object({
				{"path", "game.camera"},
				{"x", pos.x},
				{"y", pos.y},
				{"z", pos.z},
				{"angle", TheTacticalView->getAngle()},
				{"pitch", TheTacticalView->getPitch()},
				{"zoom", TheTacticalView->getZoom()},
				{"height_above_ground", TheTacticalView->getHeightAboveGround()},
				{"default_height", TheGlobalData != nullptr ? TheGlobalData->m_cameraHeight : 0.0f},
				{"min_height", TheGlobalData != nullptr ? TheGlobalData->m_minCameraHeight : 0.0f},
				{"max_height", TheGlobalData != nullptr ? TheGlobalData->m_maxCameraHeight : 0.0f},
				{"zoom_limited", TheTacticalView->isZoomLimited()}
			});
			return true;
		}

