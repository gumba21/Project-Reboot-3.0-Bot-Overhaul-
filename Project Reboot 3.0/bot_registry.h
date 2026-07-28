#pragma once

#include "UObjectArray.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

class AController;
class APawn;
class AFortInventory;
class AFortPlayerController;
class AFortPlayerPawnAthena;
class AFortPlayerStateAthena;
class UWorld;

enum class EPlayerBotType : uint8
{
	Participant,
	Practice,
};

enum class EPlayerBotLifecycleState : uint8
{
	Alive,
	Dead,
	PendingCleanup,
	Removed,
};

enum class EBotDeathNotificationResult : uint8
{
	NotRegistered,
	Started,
	DuplicateSuppressed,
	CleanupInProgress,
};

enum class EBotCleanupStartResult : uint8
{
	Started,
	AlreadyPending,
	AlreadyRemoved,
	NotFound,
};

enum class EBotStressPhase : uint8
{
	Inactive,
	Spawning,
	Spawned,
	Killing,
	Killed,
	Cleaning,
	Cleaned,
};

enum class EBotLifecycleDiagnosticStage : uint8
{
	None,
	DamageApplication,
	DeathNotificationHook,
	DelayedDeathCallback,
	OriginalDeathHandler,
	PracticeBypass,
	TimerCancellation,
	Unpossess,
	InventoryDestruction,
	PawnDestruction,
	ControllerDestruction,
	PlayerStateHandling,
	RegistryRemoval,
	TombstoneCreation,
	TombstoneRemoval,
	InvalidBotSweep,
};

struct FBotStressFeatureFlags
{
	// Practice bots bypass the original Fortnite handler by default. Turning
	// this on is diagnostic-only and may re-enable the engine's legacy path.
	bool bOriginalFortniteDeathHandler = false;
	bool bUnpossess = true;
	bool bPawnDestruction = true;
	bool bControllerDestruction = true;
	bool bPlayerStateCleanup = true;
	bool bRegistryRemoval = true;
	bool bInvalidBotSweeping = true;
};

struct FBotStressDiagnosticSnapshot
{
	bool bActive = false;
	bool bHeartbeatTicking = false;
	uint64 HeartbeatNumber = 0;
	EBotStressPhase Phase = EBotStressPhase::Inactive;
	EBotLifecycleDiagnosticStage LastEnteredStage = EBotLifecycleDiagnosticStage::None;
	EBotLifecycleDiagnosticStage LastCompletedStage = EBotLifecycleDiagnosticStage::None;
	uint64 LastEnteredBotId = 0;
	uint64 LastCompletedBotId = 0;
	size_t CachedRegistryCount = 0;
	int RequestedCount = 0;
	int SpawnedCount = 0;
	int KillRequests = 0;
	int CleanupRequests = 0;
	double AgeSeconds = 0.0;
};

// A raw UObject address is not enough: Unreal may recycle the same object slot.
// Pairing the address with its object-array index and serial number lets Resolve
// reject stale or recycled objects without dereferencing the old pointer.
struct FSafeBotObjectReference
{
	UObject* Object = nullptr;
	int32 ObjectIndex = -1;
	int32 ObjectSerialNumber = 0;

	static FSafeBotObjectReference Capture(UObject* Object);

	template <typename T = UObject>
	T* Resolve() const
	{
		if (!Object || ObjectIndex < 0)
			return nullptr;

		auto Item = GetItemByIndex(ObjectIndex);

		if (!Item || Item->Object != Object || Item->SerialNumber != ObjectSerialNumber)
			return nullptr;

		return Object->IsValidLowLevel() ? (T*)Object : nullptr;
	}

	bool IsValid() const { return Resolve<UObject>() != nullptr; }
	bool Matches(UObject* Candidate) const;
	void Reset();
};

struct FPlayerBotRegistryEntry
{
	uint64 BotId = 0;
	EPlayerBotType Type = EPlayerBotType::Participant;
	EPlayerBotLifecycleState State = EPlayerBotLifecycleState::Alive;
	FSafeBotObjectReference Controller;
	FSafeBotObjectReference Pawn;
	FSafeBotObjectReference PlayerState;
	FSafeBotObjectReference Inventory;
	std::chrono::steady_clock::time_point SpawnTime;
	std::string DisplayName;
	bool bReferencesValid = true;
	bool bCountedAsAliveParticipant = false;
	bool bInvalidReferenceLogged = false;
	bool bDeathNotificationInProgress = false;
	bool bDeathNotificationHandled = false;

	double GetSpawnAgeSeconds() const;
	bool HasRequiredReferences() const;
};

class FPlayerBotRegistry
{
public:
	uint64 RegisterBot(EPlayerBotType Type, AController* Controller, AFortPlayerPawnAthena* Pawn,
		AFortPlayerStateAthena* PlayerState, AFortInventory* Inventory, const std::string& DisplayName);

	std::optional<FPlayerBotRegistryEntry> GetBot(uint64 BotId);
	std::optional<FPlayerBotRegistryEntry> FindByController(AController* Controller);
	std::optional<FPlayerBotRegistryEntry> FindByPawn(APawn* Pawn);
	std::optional<uint64> FindStableBotIdByController(AController* Controller);
	std::vector<FPlayerBotRegistryEntry> GetBots();
	size_t Num();

	EBotDeathNotificationResult BeginDeathNotification(AController* Controller, APawn* Pawn,
		FPlayerBotRegistryEntry* OutEntry = nullptr);
	bool CompleteDeathNotification(uint64 BotId);
	bool MarkAliveTrackingAdded(uint64 BotId);
	bool MarkAliveTrackingRemoved(AController* Controller);
	EBotCleanupStartResult BeginCleanup(uint64 BotId);
	bool RestoreAfterDiagnosticCleanup(uint64 BotId);
	bool WasRemoved(uint64 BotId) const;
	bool RemoveEntry(uint64 BotId);
	void ClearRetiredControllerTombstones(const char* Reason, bool bLog = true);
	std::vector<uint64> CollectInvalidAliveBotIds();
	void InvalidateAndClear(const char* Reason, bool bLog = true);

private:
	std::vector<FPlayerBotRegistryEntry>::iterator FindIterator(uint64 BotId);
	std::vector<FPlayerBotRegistryEntry>::iterator FindControllerIterator(AController* Controller);
	std::vector<FPlayerBotRegistryEntry>::iterator FindPawnIterator(APawn* Pawn);

	std::vector<FPlayerBotRegistryEntry> Entries;
	std::vector<std::pair<uint64, FSafeBotObjectReference>> RetiredControllerIds;
	std::unordered_set<uint64> RemovedBotIds;
	uint64 NextBotId = 1;
};

namespace Bots
{
	FPlayerBotRegistry& GetRegistry();

	const char* BotTypeToString(EPlayerBotType Type);
	const char* BotStateToString(EPlayerBotLifecycleState State);
	const char* BotStressPhaseToString(EBotStressPhase Phase);
	const char* BotLifecycleDiagnosticStageToString(EBotLifecycleDiagnosticStage Stage);
	bool TryParseBotType(const std::string& Value, EPlayerBotType& OutType);

	void BeginBotStressSession(int RequestedCount);
	void RecordBotStressSpawn(uint64 BotId);
	void SetBotStressPhase(EBotStressPhase Phase);
	void RecordBotStressKillRequest();
	void RecordBotStressCleanupRequest();
	std::vector<uint64> GetBotStressIds();
	bool IsRecordedBotStressId(uint64 BotId);
	FBotStressDiagnosticSnapshot GetBotStressDiagnosticSnapshot();
	void ResetBotStressDiagnostics();
	void TickBotStressHeartbeat();
	bool ShouldRunInvalidBotSweep(bool bFromNetworkTick);
	void UpdateStressRegistryCount(size_t Count);
	void EnterLifecycleDiagnosticStage(uint64 BotId, EBotLifecycleDiagnosticStage Stage);
	void CompleteLifecycleDiagnosticStage(uint64 BotId, EBotLifecycleDiagnosticStage Stage);
	FBotStressFeatureFlags GetBotStressFeatureFlags();
	bool SetBotStressFeatureFlag(const std::string& Name, bool bEnabled);

	bool AddBotToAlivePlayerTracking(uint64 BotId);
	bool ShouldRemoveFromAlivePlayersOnDeath(AController* Controller);
	void NotifyAlivePlayerTrackingRemoved(AController* Controller);

	void LogDeathTimerRegistration(AController* Controller, APawn* Pawn, UObject* PlayerState,
		const char* CallbackName, float DelaySeconds);
	bool CancelBotOwnedDeathTimers(uint64 BotId, const char* Reason);
	bool ShouldSuppressDelayedDeathCallback(AController* Controller, const char* CallbackName,
		uint64* OutBotId = nullptr);
	void LogDelayedDeathCallbackExit(uint64 BotId, const char* CallbackName, bool bSuppressed);

	bool CleanupRegisteredBot(uint64 BotId, const char* Reason, bool bDestroyActors = true);
}
