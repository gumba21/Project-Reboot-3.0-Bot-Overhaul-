#pragma once

#include "UObjectArray.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
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
	std::vector<FPlayerBotRegistryEntry> GetBots();
	size_t Num();

	bool MarkDead(AController* Controller, APawn* Pawn);
	bool MarkAliveTrackingAdded(uint64 BotId);
	bool MarkAliveTrackingRemoved(AController* Controller);
	bool MarkPendingCleanup(uint64 BotId);
	bool RemoveEntry(uint64 BotId);
	std::vector<uint64> CollectInvalidAliveBotIds();
	void InvalidateAndClear(const char* Reason, bool bLog = true);

private:
	std::vector<FPlayerBotRegistryEntry>::iterator FindIterator(uint64 BotId);
	std::vector<FPlayerBotRegistryEntry>::iterator FindControllerIterator(AController* Controller);
	std::vector<FPlayerBotRegistryEntry>::iterator FindPawnIterator(APawn* Pawn);

	std::vector<FPlayerBotRegistryEntry> Entries;
	uint64 NextBotId = 1;
};

namespace Bots
{
	FPlayerBotRegistry& GetRegistry();

	const char* BotTypeToString(EPlayerBotType Type);
	const char* BotStateToString(EPlayerBotLifecycleState State);
	bool TryParseBotType(const std::string& Value, EPlayerBotType& OutType);

	bool AddBotToAlivePlayerTracking(uint64 BotId);
	bool ShouldRemoveFromAlivePlayersOnDeath(AController* Controller);
	void NotifyAlivePlayerTrackingRemoved(AController* Controller);

	bool CleanupRegisteredBot(uint64 BotId, const char* Reason, bool bDestroyActors = true);
}
