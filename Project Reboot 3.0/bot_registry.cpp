#include "bot_registry.h"

#include "Controller.h"
#include "FortGameModeAthena.h"
#include "FortGameStateAthena.h"
#include "FortInventory.h"
#include "FortPlayerControllerAthena.h"
#include "FortPlayerPawnAthena.h"
#include "FortPlayerStateAthena.h"
#include "World.h"
#include "reboot.h"

#include <algorithm>
#include <cctype>

FSafeBotObjectReference FSafeBotObjectReference::Capture(UObject* InObject)
{
	FSafeBotObjectReference Result;

	if (!InObject || !InObject->IsValidLowLevel())
		return Result;

	auto Item = GetItemByIndex(InObject->InternalIndex);

	if (!Item || Item->Object != InObject)
		return Result;

	Result.Object = InObject;
	Result.ObjectIndex = InObject->InternalIndex;
	Result.ObjectSerialNumber = Item->SerialNumber;
	return Result;
}

void FSafeBotObjectReference::Reset()
{
	Object = nullptr;
	ObjectIndex = -1;
	ObjectSerialNumber = 0;
}

double FPlayerBotRegistryEntry::GetSpawnAgeSeconds() const
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - SpawnTime).count();
}

bool FPlayerBotRegistryEntry::HasRequiredReferences() const
{
	return Controller.IsValid() && Pawn.IsValid() && PlayerState.IsValid();
}

std::vector<FPlayerBotRegistryEntry>::iterator FPlayerBotRegistry::FindIterator(uint64 BotId)
{
	return std::find_if(Entries.begin(), Entries.end(), [BotId](const FPlayerBotRegistryEntry& Entry) {
		return Entry.BotId == BotId;
	});
}

std::vector<FPlayerBotRegistryEntry>::iterator FPlayerBotRegistry::FindControllerIterator(AController* Controller)
{
	return std::find_if(Entries.begin(), Entries.end(), [Controller](const FPlayerBotRegistryEntry& Entry) {
		return Entry.Controller.Resolve<AController>() == Controller;
	});
}

std::vector<FPlayerBotRegistryEntry>::iterator FPlayerBotRegistry::FindPawnIterator(APawn* Pawn)
{
	return std::find_if(Entries.begin(), Entries.end(), [Pawn](const FPlayerBotRegistryEntry& Entry) {
		return Entry.Pawn.Resolve<APawn>() == Pawn;
	});
}

uint64 FPlayerBotRegistry::RegisterBot(EPlayerBotType Type, AController* Controller,
	AFortPlayerPawnAthena* Pawn, AFortPlayerStateAthena* PlayerState, AFortInventory* Inventory,
	const std::string& DisplayName)
{
	auto ControllerReference = FSafeBotObjectReference::Capture(Controller);
	auto PawnReference = FSafeBotObjectReference::Capture(Pawn);
	auto PlayerStateReference = FSafeBotObjectReference::Capture(PlayerState);

	if (!ControllerReference.IsValid() || !PawnReference.IsValid() || !PlayerStateReference.IsValid())
	{
		LOG_ERROR(LogBots, "[BotRegistry] Refused insertion with invalid required references.");
		return 0;
	}

	if (FindControllerIterator(Controller) != Entries.end() || FindPawnIterator(Pawn) != Entries.end())
	{
		LOG_ERROR(LogBots, "[BotRegistry] Refused duplicate controller or pawn insertion.");
		return 0;
	}

	FPlayerBotRegistryEntry Entry;
	Entry.BotId = NextBotId++;
	Entry.Type = Type;
	Entry.State = EPlayerBotLifecycleState::Alive;
	Entry.Controller = ControllerReference;
	Entry.Pawn = PawnReference;
	Entry.PlayerState = PlayerStateReference;
	Entry.Inventory = FSafeBotObjectReference::Capture(Inventory);
	Entry.SpawnTime = std::chrono::steady_clock::now();
	Entry.DisplayName = DisplayName;
	Entry.bReferencesValid = true;

	const auto BotId = Entry.BotId;
	Entries.push_back(std::move(Entry));

	LOG_INFO(LogBots, "[BotRegistry] Inserted bot {} type={} name={}.",
		BotId, Bots::BotTypeToString(Type), DisplayName);
	return BotId;
}

std::optional<FPlayerBotRegistryEntry> FPlayerBotRegistry::GetBot(uint64 BotId)
{
	auto It = FindIterator(BotId);
	return It == Entries.end() ? std::nullopt : std::optional<FPlayerBotRegistryEntry>(*It);
}

std::optional<FPlayerBotRegistryEntry> FPlayerBotRegistry::FindByController(AController* Controller)
{
	auto It = FindControllerIterator(Controller);
	return It == Entries.end() ? std::nullopt : std::optional<FPlayerBotRegistryEntry>(*It);
}

std::optional<FPlayerBotRegistryEntry> FPlayerBotRegistry::FindByPawn(APawn* Pawn)
{
	auto It = FindPawnIterator(Pawn);
	return It == Entries.end() ? std::nullopt : std::optional<FPlayerBotRegistryEntry>(*It);
}

std::vector<FPlayerBotRegistryEntry> FPlayerBotRegistry::GetBots()
{
	return Entries;
}

size_t FPlayerBotRegistry::Num()
{
	return Entries.size();
}

bool FPlayerBotRegistry::MarkDead(AController* Controller, APawn* Pawn)
{
	auto It = FindControllerIterator(Controller);

	if (It == Entries.end() && Pawn)
		It = FindPawnIterator(Pawn);

	if (It == Entries.end())
		return false;

	if (It->State == EPlayerBotLifecycleState::Dead ||
		It->State == EPlayerBotLifecycleState::PendingCleanup ||
		It->State == EPlayerBotLifecycleState::Removed)
	{
		return true;
	}

	It->State = EPlayerBotLifecycleState::Dead;
	LOG_INFO(LogBots, "[BotLifecycle] Death observed for bot {} type={}.",
		It->BotId, Bots::BotTypeToString(It->Type));
	return true;
}

bool FPlayerBotRegistry::MarkAliveTrackingAdded(uint64 BotId)
{
	auto It = FindIterator(BotId);

	if (It == Entries.end())
		return false;

	It->bCountedAsAliveParticipant = true;
	return true;
}

bool FPlayerBotRegistry::MarkAliveTrackingRemoved(AController* Controller)
{
	auto It = FindControllerIterator(Controller);

	if (It == Entries.end())
		return false;

	It->bCountedAsAliveParticipant = false;
	return true;
}

bool FPlayerBotRegistry::MarkPendingCleanup(uint64 BotId)
{
	auto It = FindIterator(BotId);

	if (It == Entries.end())
		return false;

	if (It->State == EPlayerBotLifecycleState::Removed)
		return true;

	It->State = EPlayerBotLifecycleState::PendingCleanup;
	return true;
}

bool FPlayerBotRegistry::RemoveEntry(uint64 BotId)
{
	auto It = FindIterator(BotId);

	if (It == Entries.end())
		return false;

	It->State = EPlayerBotLifecycleState::Removed;
	It->Controller.Reset();
	It->Pawn.Reset();
	It->PlayerState.Reset();
	It->Inventory.Reset();

	Entries.erase(std::remove_if(Entries.begin(), Entries.end(), [BotId](const FPlayerBotRegistryEntry& Entry) {
		return Entry.BotId == BotId;
	}), Entries.end());
	return true;
}

std::vector<uint64> FPlayerBotRegistry::CollectInvalidAliveBotIds()
{
	std::vector<uint64> InvalidBotIds;

	for (auto& Entry : Entries)
	{
		auto Controller = Entry.Controller.Resolve<AController>();
		auto Pawn = Entry.Pawn.Resolve<AFortPlayerPawnAthena>();
		auto PlayerState = Entry.PlayerState.Resolve<AFortPlayerStateAthena>();
		const bool bRequiredReferencesValid =
			Controller && !Controller->IsActorBeingDestroyed() &&
			Pawn && !Pawn->IsActorBeingDestroyed() &&
			PlayerState && !PlayerState->IsActorBeingDestroyed();
		Entry.bReferencesValid = bRequiredReferencesValid;

		if (bRequiredReferencesValid || Entry.State == EPlayerBotLifecycleState::Removed)
			continue;

		if (!Entry.bInvalidReferenceLogged)
		{
			LOG_WARN(LogBots, "[BotLifecycle] Invalid reference detected for bot {} (controller={} pawn={} playerstate={}).",
				Entry.BotId, Entry.Controller.IsValid(), Entry.Pawn.IsValid(), Entry.PlayerState.IsValid());
			Entry.bInvalidReferenceLogged = true;
		}

		// Dead entries intentionally remain available for diagnostics until an
		// explicit cleanup or reset. Alive entries cannot safely keep ticking.
		if (Entry.State == EPlayerBotLifecycleState::Alive)
		{
			Entry.State = EPlayerBotLifecycleState::PendingCleanup;
			InvalidBotIds.push_back(Entry.BotId);
		}
	}

	return InvalidBotIds;
}

void FPlayerBotRegistry::InvalidateAndClear(const char* Reason, bool bLog)
{
	if (bLog)
		LOG_INFO(LogBots, "[BotLifecycle] Invalidating {} registry entries during {}.", Entries.size(), Reason);

	for (auto& Entry : Entries)
	{
		Entry.State = EPlayerBotLifecycleState::Removed;
		Entry.Controller.Reset();
		Entry.Pawn.Reset();
		Entry.PlayerState.Reset();
		Entry.Inventory.Reset();
	}

	Entries.clear();
}

namespace
{
	bool IsActorSafeToDestroy(AActor* Actor)
	{
		return Actor && Actor->IsValidLowLevel() && !Actor->IsActorBeingDestroyed();
	}

	void DestroyActorIfValid(AActor* Actor)
	{
		if (IsActorSafeToDestroy(Actor))
			Actor->K2_DestroyActor();
	}

	bool RemoveControllerFromAliveArray(AFortGameModeAthena* GameMode, AFortPlayerControllerAthena* Controller)
	{
		if (!GameMode || !Controller)
			return false;

		auto& AlivePlayers = GameMode->GetAlivePlayers();

		for (int Index = 0; Index < AlivePlayers.Num(); ++Index)
		{
			if (AlivePlayers.at(Index) != Controller)
				continue;

			const int LastIndex = AlivePlayers.Num() - 1;

			if (Index != LastIndex)
				AlivePlayers.at(Index) = AlivePlayers.at(LastIndex);

			// TArray::RemoveAt in this project does not currently move elements,
			// so use swap-with-last and adjust the public count without reallocating.
			AlivePlayers.ArrayNum--;
			return true;
		}

		return false;
	}

	void RemoveParticipantTrackingForManualDespawn(FPlayerBotRegistryEntry& Entry)
	{
		if (Entry.Type != EPlayerBotType::Participant || !Entry.bCountedAsAliveParticipant)
			return;

		auto World = GetWorld();
		auto GameMode = World ? Cast<AFortGameModeAthena>(World->GetGameMode()) : nullptr;
		auto GameState = World ? Cast<AFortGameStateAthena>(World->GetGameState()) : nullptr;
		auto Controller = Entry.Controller.Resolve<AFortPlayerControllerAthena>();
		auto StoredControllerAddress = Controller ? Controller : (AFortPlayerControllerAthena*)Entry.Controller.Object;

		if (!GameMode || !GameState || !StoredControllerAddress)
		{
			LOG_WARN(LogBots, "[BotLifecycle] Could not fully update alive tracking while removing bot {}.", Entry.BotId);
			return;
		}

		const bool bRemoved = RemoveControllerFromAliveArray(GameMode, StoredControllerAddress);

		if (bRemoved && GameState->GetPlayersLeft() > 0)
		{
			--GameState->GetPlayersLeft();
			GameState->OnRep_PlayersLeft();
		}
	}
}

namespace Bots
{
	FPlayerBotRegistry& GetRegistry()
	{
		static FPlayerBotRegistry Registry;
		return Registry;
	}

	const char* BotTypeToString(EPlayerBotType Type)
	{
		switch (Type)
		{
		case EPlayerBotType::Practice:
			return "Practice";
		case EPlayerBotType::Participant:
		default:
			return "Participant";
		}
	}

	const char* BotStateToString(EPlayerBotLifecycleState State)
	{
		switch (State)
		{
		case EPlayerBotLifecycleState::Dead:
			return "Dead";
		case EPlayerBotLifecycleState::PendingCleanup:
			return "PendingCleanup";
		case EPlayerBotLifecycleState::Removed:
			return "Removed";
		case EPlayerBotLifecycleState::Alive:
		default:
			return "Alive";
		}
	}

	bool TryParseBotType(const std::string& Value, EPlayerBotType& OutType)
	{
		std::string LowerValue = Value;
		std::transform(LowerValue.begin(), LowerValue.end(), LowerValue.begin(), [](unsigned char Character) {
			return (char)std::tolower(Character);
		});

		if (LowerValue == "participant")
		{
			OutType = EPlayerBotType::Participant;
			return true;
		}

		if (LowerValue == "practice")
		{
			OutType = EPlayerBotType::Practice;
			return true;
		}

		return false;
	}

	bool AddBotToAlivePlayerTracking(uint64 BotId)
	{
		auto Entry = GetRegistry().GetBot(BotId);

		if (!Entry)
			return false;

		// Practice bots are deliberately absent from both BR counters. Their
		// death hook is also excluded from RemoveFromAlivePlayers below.
		if (Entry->Type == EPlayerBotType::Practice)
			return true;

		auto World = GetWorld();
		auto GameMode = World ? Cast<AFortGameModeAthena>(World->GetGameMode()) : nullptr;
		auto GameState = World ? Cast<AFortGameStateAthena>(World->GetGameState()) : nullptr;
		auto Controller = Entry->Controller.Resolve<AFortPlayerControllerAthena>();

		if (!GameMode || !GameState || !Controller)
		{
			LOG_ERROR(LogBots, "[BotLifecycle] Failed to add participant bot {} to alive tracking.", BotId);
			return false;
		}

		auto& AlivePlayers = GameMode->GetAlivePlayers();

		for (int Index = 0; Index < AlivePlayers.Num(); ++Index)
		{
			if (AlivePlayers.at(Index) == Controller)
			{
				LOG_ERROR(LogBots, "[BotLifecycle] Duplicate AlivePlayers insertion refused for bot {}.", BotId);
				return false;
			}
		}

		++GameState->GetPlayersLeft();
		GameState->OnRep_PlayersLeft();
		AlivePlayers.Add(Controller);
		GetRegistry().MarkAliveTrackingAdded(BotId);
		return true;
	}

	bool ShouldRemoveFromAlivePlayersOnDeath(AController* Controller)
	{
		auto Entry = GetRegistry().FindByController(Controller);
		return !Entry || Entry->Type == EPlayerBotType::Participant;
	}

	void NotifyAlivePlayerTrackingRemoved(AController* Controller)
	{
		GetRegistry().MarkAliveTrackingRemoved(Controller);
	}

	bool CleanupRegisteredBot(uint64 BotId, const char* Reason, bool bDestroyActors)
	{
		auto Entry = GetRegistry().GetBot(BotId);

		if (!Entry)
			return false;

		if (Entry->State == EPlayerBotLifecycleState::Removed)
			return true;

		GetRegistry().MarkPendingCleanup(BotId);
		LOG_INFO(LogBots, "[BotLifecycle] Cleanup requested for bot {} reason={}.", BotId, Reason);

		if (Entry->bCountedAsAliveParticipant)
			RemoveParticipantTrackingForManualDespawn(*Entry);

		if (bDestroyActors)
		{
			auto Controller = Entry->Controller.Resolve<AController>();
			auto Pawn = Entry->Pawn.Resolve<AFortPlayerPawnAthena>();
			auto Inventory = Entry->Inventory.Resolve<AFortInventory>();
			const auto PlayerStateReference = Entry->PlayerState;

			if (Controller && Pawn && Controller->GetPawn() == Pawn)
				Controller->UnPossess();

			DestroyActorIfValid(Inventory);
			DestroyActorIfValid(Pawn);
			DestroyActorIfValid(Controller);
			DestroyActorIfValid(PlayerStateReference.Resolve<AFortPlayerStateAthena>());
		}

		const bool bRemoved = GetRegistry().RemoveEntry(BotId);

		if (bRemoved)
			LOG_INFO(LogBots, "[BotLifecycle] Cleanup completed for bot {}.", BotId);
		else
			LOG_ERROR(LogBots, "[BotLifecycle] Cleanup failed to remove registry entry for bot {}.", BotId);

		return bRemoved;
	}
}
