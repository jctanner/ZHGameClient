/**
 * AIControlAdapter.h
 *
 * Public interface for the AI Control Adapter system.
 *
 * The AI Control Adapter provides external programmatic control of Command & Conquer: Generals
 * Zero Hour via a named pipe interface. It enables AI agents to play the game by:
 * - Sending commands (build units, move armies, attack, etc.)
 * - Querying game state (resources, units, enemy positions, etc.)
 * - Navigating menus and configuring skirmish matches
 * - Receiving game events (attacks, unit deaths, construction complete, etc.)
 *
 * Architecture:
 * - Runs in-process within the game (injected via DLL or compiled-in)
 * - Communicates via Windows named pipe (default: \\.\pipe\zh_ai_control)
 * - Uses JSON protocol for all messages
 * - Updates every frame via AIControlAdapterUpdate() hook
 * - Does NOT modify core game logic - only sends player inputs
 *
 * The adapter is designed for:
 * - Reinforcement learning (RL) training environments
 * - AI agent research and development
 * - Automated testing and scripting
 * - Bot development
 *
 * Usage:
 * 1. Game calls AIControlAdapterUpdate() every frame (hooked in GameLogic::update())
 * 2. Adapter checks named pipe for incoming messages
 * 3. External client sends JSON commands (e.g., {"type":"SessionCommand","cmd":"Game.BuildWorker"})
 * 4. Adapter executes commands by simulating player inputs
 * 5. Adapter sends JSON responses and events back to client
 *
 * Thread safety:
 * - AIControlAdapterUpdate() runs on game's main thread
 * - Named pipe I/O is non-blocking
 * - No locks needed (single-threaded execution model)
 *
 * See also:
 * - AIControlAdapterPolicy.h for policy/strategy decision functions
 * - scripts/configure-death-valley-final.ps1 for example PowerShell client
 * - ADAPTER_CODE_REVIEW.md for architecture documentation
 */

#pragma once

/**
 * Update the AI Control Adapter for the current frame.
 *
 * This function is called every frame by the game's main update loop (typically hooked
 * in GameLogic::update()). It performs the following operations:
 *
 * 1. Named pipe communication:
 *    - Checks for incoming messages from external clients (non-blocking)
 *    - Processes JSON commands (SessionCommand, AutonomyCommand, etc.)
 *    - Sends responses and events back to clients
 *
 * 2. Command execution:
 *    - Executes queued commands by simulating player inputs
 *    - Handles menu navigation (Menu.Click, Menu.SetText, etc.)
 *    - Handles game commands (Game.BuildWorker, Game.MoveArmy, etc.)
 *    - Handles skirmish setup (Skirmish.Start, etc.)
 *
 * 3. Autonomy system:
 *    - Updates autonomous economy (worker management, building construction)
 *    - Updates autonomous combat (unit production, attack coordination)
 *    - Executes science and upgrade plans
 *
 * 4. Event emission:
 *    - Detects game events (unit deaths, attacks, construction, etc.)
 *    - Sends event notifications to connected clients
 *    - Maintains event history for recent attack tracking
 *
 * Performance:
 * - Designed to run every frame with minimal overhead
 * - Named pipe reads are non-blocking
 * - Object cache updates incrementally (bounded per-frame work)
 * - Typical frame time impact: <0.1ms
 *
 * Thread safety:
 * - Must be called from game's main thread only
 * - No internal locking (relies on single-threaded game architecture)
 *
 * Call site:
 * - Hooked in GameLogic::update() (or equivalent main loop)
 * - Called after game simulation but before rendering
 */
void AIControlAdapterUpdate();

/**
 * Reset the AI Control Adapter to initial state.
 *
 * This function resets all adapter state when starting a new game or returning to the
 * main menu. It clears:
 *
 * - Object cache (units, buildings, resources)
 * - Autonomy state (worker assignments, production queues)
 * - Event history (recent attacks, deaths)
 * - Build reservations and cooldown timers
 * - Cached game state snapshots
 *
 * The named pipe connection is NOT closed - clients remain connected across game sessions.
 * This allows the same client to:
 * 1. Configure a skirmish match via menu commands
 * 2. Start the game
 * 3. Play the game via game commands
 * 4. Return to menu and repeat
 *
 * Call sites:
 * - When starting a new skirmish game
 * - When returning to main menu from a game
 * - When loading a saved game (to rebuild state)
 *
 * Thread safety:
 * - Must be called from game's main thread only
 */
void AIControlAdapterReset();
