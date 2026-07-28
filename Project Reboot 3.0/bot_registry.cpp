#include "bot_registry.h"

#include "Controller.h"
#include "FortGameModeAthena.h"
#include "FortGameStateAthena.h"
#include "FortInventory.h"
#include "FortPlayerControllerAthena.h"
#include "FortPlayerPawnAthena.h"
#include "FortPlayerStateAthena.h"
#include "KismetSystemLibrary.h"
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

bool FSafeBotObjectReference::Matches(UObject* Candidate) const
{
	if (!Candidate || !Object || Candidate != Object || ObjectIndex < 0)
		return false;

	auto Item = GetItemByIndex(ObjectIndex);
	return Item && Item->Object == Candidate && Item->SerialNumber == ObjectSerialNumber;
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

std::optional<uint64> FPlayerBotRegistry::FindStableBotIdByController(AController* Controller)
{
	auto It = FindControllerIterator(Controller);

	if (It != Entries.end())
		return It->BotId;

	for (const auto& RetiredController : RetiredControllerIds)
	{
		if (RetiredController.second.Matches(Controller))
			return RetiredController.first;
	}

	return std::nullopt;
}

std::vector<FPlayerBotRegistryEntry> FPlayerBotRegistry::GetBots()
{
	return Entries;
}

size_t FPlayerBotRegistry::Num()
{
	return Entries.size();
}

EBotDeathNotificationResult FPlayerBotRegistry::BeginDeathNotification(AController* Controller,
	APawn* Pawn, FPlayerBotRegistryEntry* OutEntry)
{
	auto It = FindControllerIterator(Controller);

	if (It == Entries.end() && Pawn)
		It = FindPawnIterator(Pawn);

	if (It == Entries.end())
		return EBotDeathNotificationResult::NotRegistered;

	if (OutEntry)
		*OutEntry = *It;

	if (It->State == EPlayerBotLifecycleState::PendingCleanup ||
		It->State == EPlayerBotLifecycleState::Removed)
	{
		return EBotDeathNotificationResult::CleanupInProgress;
	}

	if (It->State == EPlayerBotLifecycleState::Dead ||
		It->bDeathNotificationInProgress ||
		It->bDeathNotificationHandled)
	{
		return EBotDeathNotificationResult::DuplicateSuppressed;
	}

	It->bDeathNotificationInProgress = true;
	It->State = EPlayerBotLifecycleState::Dead;

	if (OutEntry)
		*OutEntry = *It;

	LOG_INFO(LogBots, "[BotLifecycle] Death observed for bot {} type={}.",
		It->BotId, Bots::BotTypeToString(It->Type));
	return EBotDeathNotificationResult::Started;
}

bool FPlayerBotRegistry::CompleteDeathNotification(uint64 BotId)
{
	auto It = FindIterator(BotId);

	if (It == Entries.end())
		return WasRemoved(BotId);

	It->bDeathNotificationInProgress = false;
	It->bDeathNotificationHandled = true;
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

EBotCleanupStartResult FPlayerBotRegistry::BeginCleanup(uint64 BotId)
{
	if (WasRemoved(BotId))
		return EBotCleanupStartResult::AlreadyRemoved;

	auto It = FindIterator(BotId);

	if (It == Entries.end())
		return EBotCleanupStartResult::NotFound;

	if (It->State == EPlayerBotLifecycleState::Removed)
		return EBotCleanupStartResult::AlreadyRemoved;

	if (It->State == EPlayerBotLifecycleState::PendingCleanup)
		return EBotCleanupStartResult::AlreadyPending;

	It->State = EPlayerBotLifecycleState::PendingCleanup;
	return EBotCleanupStartResult::Started;
}

bool FPlayerBotRegistry::WasRemoved(uint64 BotId) const
{
	return RemovedBotIds.contains(BotId);
}

bool FPlayerBotRegistry::RemoveEntry(uint64 BotId)
{
	auto It = FindIterator(BotId);

	if (It == Entries.end())
		return false;

	It->State = EPlayerBotLifecycleState::Removed;
	RetiredControllerIds.push_back({ BotId, It->Controller });
	It->Controller.Reset();
	It->Pawn.Reset();
	It->PlayerState.Reset();
	It->Inventory.Reset();
	RemovedBotIds.insert(BotId);

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
			// Do not mutate into PendingCleanup while iterating. The caller
			// consumes this stable ID snapshot and begins cleanup afterward.
			InvalidBotIds.push_back(Entry.BotId);
		}
	}

	return InvalidBotIds;
}

void FPlayerBotRegistry::InvalidateAndClear(const char* Reason, bool bLog)
{
	if (bLog)
	{
		LOG_INFO(LogBots, "[BotLifecycle] Invalidating {} registry entries during {}.", Entries.size(), Reason);
	}

	for (auto& Entry : Entries)
	{
		Entry.State = EPlayerBotLifecycleState::Removed;
		RetiredControllerIds.push_back({ Entry.BotId, Entry.Controller });
		Entry.Controller.Reset();
		Entry.Pawn.Reset();
		Entry.PlayerState.Reset();
		Entry.Inventory.Reset();
		RemovedBotIds.insert(Entry.BotId);
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

	void LogDeathTimerRegistration(AController* Controller, APawn* Pawn, UObject* PlayerState,
		const char* CallbackName, float DelaySeconds)
	{
		const auto BotId = GetRegistry().FindStableBotIdByController(Controller);
		LOG_INFO(LogBots,
			"[BotLifecycle] Timer registration bot={} callback={} delay={:.2f}s controllerValid={} pawnValid={} playerStateValid={}.",
			BotId.value_or(0), CallbackName, DelaySeconds,
			Controller && Controller->IsValidLowLevel(),
			Pawn && Pawn->IsValidLowLevel(),
			PlayerState && PlayerState->IsValidLowLevel());
	}

	bool CancelBotOwnedDeathTimers(uint64 BotId, const char* Reason)
	{
		auto Entry = GetRegistry().GetBot(BotId);

		if (!Entry)
		{
			LOG_WARN(LogBots, "[BotLifecycle] Timer cancellation skipped for bot {} because the registry entry no longer exists.",
				BotId);
			return GetRegistry().WasRemoved(BotId);
		}

		auto Controller = Entry->Controller.Resolve<AController>();
		auto Pawn = Entry->Pawn.Resolve<APawn>();
		auto PlayerState = Entry->PlayerState.Resolve<UObject>();

		LOG_INFO(LogBots,
			"[BotLifecycle] Timer cancellation begin bot={} reason={} controllerValid={} pawnValid={} playerStateValid={}.",
			BotId, Reason, Controller != nullptr, Pawn != nullptr, PlayerState != nullptr);

		struct FTimerToCancel
		{
			UObject* Target;
			const wchar_t* FunctionName;
			const char* CallbackName;
		};

		const FTimerToCancel Timers[] = {
			{ Controller, L"SpectateOnDeath", "SpectateOnDeath" },
			{ Controller, L"RespawnPlayerAfterDeath", "RespawnPlayerAfterDeath" },
			{ Controller, L"ServerRestartPlayer", "ServerRestartPlayer" },
			{ Controller, L"RestartPlayer", "RestartPlayer" },
			{ Pawn, L"K2_DestroyActor", "K2_DestroyActor" },
			{ Pawn, L"Destroy", "Destroy" },
			{ PlayerState, L"RespawnPlayer", "RespawnPlayer" },
			{ PlayerState, L"RestartPlayer", "RestartPlayer" },
		};

		for (const auto& Timer : Timers)
		{
			if (!Timer.Target)
			{
				LOG_INFO(LogBots,
					"[BotLifecycle] Timer cancellation bot={} callback={} targetValid=false active=false remaining=-1.00s cleared=false.",
					BotId, Timer.CallbackName);
				continue;
			}

			const bool bWasActive = UKismetSystemLibrary::K2_IsTimerActive(Timer.Target, Timer.FunctionName);
			const float Remaining = bWasActive
				? UKismetSystemLibrary::K2_GetTimerRemainingTime(Timer.Target, Timer.FunctionName)
				: -1.f;
			const bool bClearCalled = UKismetSystemLibrary::K2_ClearTimer(Timer.Target, Timer.FunctionName);
			const bool bStillActive = UKismetSystemLibrary::K2_IsTimerActive(Timer.Target, Timer.FunctionName);

			LOG_INFO(LogBots,
				"[BotLifecycle] Timer cancellation bot={} callback={} targetValid=true active={} remaining={:.2f}s cleared={} stillActive={}.",
				BotId, Timer.CallbackName, bWasActive, Remaining, bClearCalled, bStillActive);
		}

		// AActor lifespan uses an internal timer rather than K2_SetTimer. Zero
		// lifespan cancels that delayed destruction without retaining a raw pawn
		// or controller pointer in our lifecycle system.
		if (Pawn)
		{
			const bool bCancelled = Pawn->SetLifeSpan(0.f);
			LOG_INFO(LogBots, "[BotLifecycle] Delayed pawn destruction cancellation bot={} SetLifeSpanAvailable={}.",
				BotId, bCancelled);
		}

		if (Controller)
		{
			const bool bCancelled = Controller->SetLifeSpan(0.f);
			LOG_INFO(LogBots, "[BotLifecycle] Delayed controller destruction cancellation bot={} SetLifeSpanAvailable={}.",
				BotId, bCancelled);
		}

		LOG_INFO(LogBots, "[BotLifecycle] Timer cancellation end bot={} reason={}.", BotId, Reason);
		return true;
	}

	bool ShouldSuppressDelayedDeathCallback(AController* Controller, const char* CallbackName,
		uint64* OutBotId)
	{
		const auto StableBotId = GetRegistry().FindStableBotIdByController(Controller);
		const uint64 BotId = StableBotId.value_or(0);

		if (OutBotId)
			*OutBotId = BotId;

		if (!StableBotId)
		{
			LOG_INFO(LogBots,
				"[BotLifecycle] Callback entry bot=0 callback={} tracked=false controllerValid={}; continuing normal player path.",
				CallbackName, Controller && Controller->IsValidLowLevel());
			return false;
		}

		auto Entry = GetRegistry().GetBot(BotId);

		if (!Entry)
		{
			LOG_WARN(LogBots,
				"[BotLifecycle] Callback entry bot={} callback={} registryEntry=false; suppressing post-cleanup callback.",
				BotId, CallbackName);
			return true;
		}

		LOG_INFO(LogBots,
			"[BotLifecycle] Callback entry bot={} callback={} type={} state={} controllerValid={} pawnValid={} playerStateValid={}.",
			BotId, CallbackName, BotTypeToString(Entry->Type), BotStateToString(Entry->State),
			Entry->Controller.IsValid(), Entry->Pawn.IsValid(), Entry->PlayerState.IsValid());

		return Entry->Type == EPlayerBotType::Practice;
	}

	void LogDelayedDeathCallbackExit(uint64 BotId, const char* CallbackName, bool bSuppressed)
	{
		LOG_INFO(LogBots, "[BotLifecycle] Callback exit bot={} callback={} suppressed={}.",
			BotId, CallbackName, bSuppressed);
	}

	bool CleanupRegisteredBot(uint64 BotId, const char* Reason, bool bDestroyActors)
	{
		LOG_INFO(LogBots, "[BotLifecycle] Cleanup request received for bot {} reason={}.", BotId, Reason);

		const auto CleanupStart = GetRegistry().BeginCleanup(BotId);

		if (CleanupStart == EBotCleanupStartResult::AlreadyPending)
		{
			LOG_WARN(LogBots, "[BotLifecycle] Duplicate cleanup suppressed for bot {} (already pending).", BotId);
			return true;
		}

		if (CleanupStart == EBotCleanupStartResult::AlreadyRemoved)
		{
			LOG_WARN(LogBots, "[BotLifecycle] Duplicate cleanup suppressed for bot {} (already removed).", BotId);
			return true;
		}

		if (CleanupStart == EBotCleanupStartResult::NotFound)
		{
			LOG_WARN(LogBots, "[BotLifecycle] Cleanup lookup found no bot with ID {}.", BotId);
			return false;
		}

		auto Entry = GetRegistry().GetBot(BotId);

		if (!Entry)
		{
			LOG_ERROR(LogBots, "[BotLifecycle] Bot {} disappeared after cleanup began; suppressing duplicate work.", BotId);
			return false;
		}

		LOG_INFO(LogBots, "[BotLifecycle] Cleanup registry lookup succeeded for bot {} type={} state={}.",
			BotId, BotTypeToString(Entry->Type), BotStateToString(Entry->State));

		// Cancel timers while registry-owned weak references still resolve and
		// before possession or actor lifetime is changed.
		CancelBotOwnedDeathTimers(BotId, Reason);

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

			LOG_INFO(LogBots, "[BotLifecycle] Bot {} inventory destruction: valid={}.", BotId, Inventory != nullptr);
			DestroyActorIfValid(Inventory);

			LOG_INFO(LogBots, "[BotLifecycle] Bot {} pawn destruction: valid={}.", BotId, Pawn != nullptr);
			DestroyActorIfValid(Pawn);

			LOG_INFO(LogBots, "[BotLifecycle] Bot {} controller destruction: valid={}.", BotId, Controller != nullptr);
			DestroyActorIfValid(Controller);

			auto PlayerState = PlayerStateReference.Resolve<AFortPlayerStateAthena>();
			LOG_INFO(LogBots, "[BotLifecycle] Bot {} player state cleanup: valid={}.", BotId, PlayerState != nullptr);
			DestroyActorIfValid(PlayerState);
		}
		else
		{
			LOG_INFO(LogBots, "[BotLifecycle] Bot {} UObject destruction skipped for shutdown-safe invalidation.", BotId);
		}

		const bool bRemoved = GetRegistry().RemoveEntry(BotId);

		if (bRemoved)
		{
			LOG_INFO(LogBots, "[BotLifecycle] Registry removal completed for bot {}.", BotId);
		}
		else
		{
			LOG_ERROR(LogBots, "[BotLifecycle] Registry removal failed for bot {}.", BotId);
		}

		return bRemoved;
	}
}
