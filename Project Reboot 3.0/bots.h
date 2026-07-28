#pragma once

#include "FortGameModeAthena.h"
#include "OnlineReplStructs.h"
#include "FortAthenaAIBotController.h"
#include "BuildingContainer.h"
#include "GameplayTagContainer.h"
#include "botnames.h"
#include "bot_registry.h"

#include <algorithm>
#include <utility>

class BotPOI
{
	FVector CenterLocation;
	FVector Range; // this just has to be FVector2D
};

class BotPOIEncounter
{
public:
	int NumChestsSearched;
	int NumAmmoBoxesSearched;
	int NumPlayersEncountered;
};

class PlayerBot
{
public:
	static inline UClass* PawnClass = nullptr;
	static inline UClass* ControllerClass = nullptr;

	AController* Controller = nullptr; // This can be 1. AFortAthenaAIBotController OR AFortPlayerControllerAthena
	bool bIsAthenaController = false;
	AFortPlayerPawnAthena* Pawn = nullptr;
	AFortPlayerStateAthena* PlayerState = nullptr;
	AFortInventory* Inventory = nullptr;
	uint64 BotId = 0;
	EPlayerBotType Type = EPlayerBotType::Participant;
	BotPOIEncounter currentBotEncounter;
	int TotalPlayersEncountered;
	std::vector<BotPOI> POIsTraveled;
	float NextJumpTime = 1.0f;

	void OnPlayerEncountered()
	{
		currentBotEncounter.NumPlayersEncountered++;
		TotalPlayersEncountered++;
	}

	void MoveToNewPOI()
	{

	}

	static bool ShouldUseAIBotController()
	{
		return false;
		return Fortnite_Version >= 11 && Engine_Version < 500;
	}

	static void InitializeBotClasses()
	{
		static auto BlueprintGeneratedClassClass = FindObject<UClass>(L"/Script/Engine.BlueprintGeneratedClass");

		if (!ShouldUseAIBotController())
		{
			PawnClass = FindObject<UClass>(L"/Game/Athena/PlayerPawn_Athena.PlayerPawn_Athena_C");
			ControllerClass = AFortPlayerControllerAthena::StaticClass();
		}
		else
		{
			PawnClass = LoadObject<UClass>(L"/Game/Athena/AI/Phoebe/BP_PlayerPawn_Athena_Phoebe.BP_PlayerPawn_Athena_Phoebe_C", BlueprintGeneratedClassClass);
			// ControllerClass = PawnClass->CreateDefaultObject()->GetAIControllerClass();
		}

		if (/* !ControllerClass
			|| */ !PawnClass
			)
		{
			LOG_ERROR(LogBots, "Failed to find a class for the bots!");
			return;
		}
	}

	static bool IsReadyToSpawnBot()
	{
		return PawnClass;
	}

	bool SetupInventory()
	{
		auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());

		if (!ShouldUseAIBotController()) // TODO REWRITE
		{
			AFortInventory** InventoryPointer = nullptr;

			if (auto FortPlayerController = Cast<AFortPlayerController>(Controller))
			{
				InventoryPointer = &FortPlayerController->GetWorldInventory();
			}
			else
			{
				if (auto FortAthenaAIBotController = Cast<AFortAthenaAIBotController>(Controller))
				{
					static auto InventoryOffset = Controller->GetOffset("Inventory");
					InventoryPointer = Controller->GetPtr<AFortInventory*>(InventoryOffset);
				}
			}

			if (!InventoryPointer)
			{
				LOG_ERROR(LogBots, "No inventory pointer!");
				return false;
			}

			static auto FortInventoryClass = FindObject<UClass>(L"/Script/FortniteGame.FortInventory"); // AFortInventory::StaticClass()
			*InventoryPointer = GetWorld()->SpawnActor<AFortInventory>(FortInventoryClass, FTransform{}, CreateSpawnParameters(ESpawnActorCollisionHandlingMethod::AlwaysSpawn, false, Controller));
			Inventory = *InventoryPointer;

			if (!Inventory)
			{
				LOG_ERROR(LogBots, "Failed to spawn Inventory!");
				return false;
			}

			Inventory->GetInventoryType() = EFortInventoryType::World;

			if (auto FortPlayerController = Cast<AFortPlayerController>(Controller))
			{
				static auto bHasInitializedWorldInventoryOffset = FortPlayerController->GetOffset("bHasInitializedWorldInventory");
				FortPlayerController->Get<bool>(bHasInitializedWorldInventoryOffset) = true;
			}

			// if (false)
			{
				if (InventoryPointer)
				{
					auto& StartingItems = GameMode->GetStartingItems();

					for (int i = 0; i < StartingItems.Num(); ++i)
					{
						auto& StartingItem = StartingItems.at(i, FItemAndCount::GetStructSize());

						// TODO: Check if it is FortSmartBuildingItemDefinition

						Inventory->AddItem(StartingItem.GetItem(), nullptr, StartingItem.GetCount());
					}

					if (auto FortPlayerController = Cast<AFortPlayerController>(Controller))
					{
						UFortItem* PickaxeInstance = FortPlayerController->AddPickaxeToInventory();

						if (PickaxeInstance)
						{
							FortPlayerController->ServerExecuteInventoryItemHook(FortPlayerController, PickaxeInstance->GetItemEntry()->GetItemGuid());
						}
					}

					Inventory->Update();
				}
			}
		}

		return true;
	}

	void PickRandomLoadout()
	{
		auto AllHeroTypes = GetAllObjectsOfClass(FindObject<UClass>(L"/Script/FortniteGame.FortHeroType"));
		std::vector<UFortItemDefinition*> AthenaHeroTypes;

		UFortItemDefinition* HeroType = FindObject<UFortItemDefinition>(L"/Game/Athena/Heroes/HID_030_Athena_Commando_M_Halloween.HID_030_Athena_Commando_M_Halloween");

		for (int i = 0; i < AllHeroTypes.size(); ++i)
		{
			auto CurrentHeroType = (UFortItemDefinition*)AllHeroTypes.at(i);

			if (CurrentHeroType->GetPathName().starts_with("/Game/Athena/Heroes/"))
				AthenaHeroTypes.push_back(CurrentHeroType);
		}

		if (AthenaHeroTypes.size())
		{
			HeroType = AthenaHeroTypes.at(std::rand() % AthenaHeroTypes.size());
		}

		static auto HeroTypeOffset = PlayerState->GetOffset("HeroType");
		PlayerState->Get(HeroTypeOffset) = HeroType;
	}

	void ApplyCosmeticLoadout()
	{
		static auto HeroTypeOffset = PlayerState->GetOffset("HeroType");
		const auto CurrentHeroType = PlayerState->Get(HeroTypeOffset);

		if (!CurrentHeroType)
		{
			LOG_WARN(LogBots, "CurrentHeroType called with an invalid HeroType!");
			return;
		}

		ApplyHID(Pawn, CurrentHeroType, true);
	}

	void SetName(const FString& NewName)
	{
		if (// true ||
			Fortnite_Version < 9
			)
		{
			if (auto PlayerController = Cast<APlayerController>(Controller))
			{
				PlayerController->ServerChangeName(NewName);
			}
		}
		else
		{
			auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());
			GameMode->ChangeName(Controller, NewName, true);
		}

		PlayerState->OnRep_PlayerName(); // ?
	}

	FString GetRandomName()
	{
		static int CurrentBotNum = 1;
		std::wstring BotNumWStr;
		FString NewName;

		if (Fortnite_Version < 9)
		{
			BotNumWStr = std::to_wstring(CurrentBotNum++);
			NewName = (L"RebootBot" + BotNumWStr).c_str();
		}
		else
		{
			if (Fortnite_Version < 11 || PlayerBotNames.empty())
			{
				BotNumWStr = std::to_wstring(CurrentBotNum++ + 200);
				NewName = (std::format(L"Anonymous[{}]", BotNumWStr)).c_str();
			}
			else
			{
				NewName = PlayerBotNames.back();
				PlayerBotNames.pop_back();
			}
		}

		return NewName;
	}

	void CleanupPartialSpawn()
	{
		const auto PlayerStateReference = FSafeBotObjectReference::Capture(PlayerState);

		if (Controller && Pawn && Controller->IsValidLowLevel() && Controller->GetPawn() == Pawn)
			Controller->UnPossess();

		if (Inventory && Inventory->IsValidLowLevel() && !Inventory->IsActorBeingDestroyed())
			Inventory->K2_DestroyActor();

		if (Pawn && Pawn->IsValidLowLevel() && !Pawn->IsActorBeingDestroyed())
			Pawn->K2_DestroyActor();

		if (Controller && Controller->IsValidLowLevel() && !Controller->IsActorBeingDestroyed())
			Controller->K2_DestroyActor();

		auto ValidPlayerState = PlayerStateReference.Resolve<AFortPlayerStateAthena>();

		if (ValidPlayerState && !ValidPlayerState->IsActorBeingDestroyed())
			ValidPlayerState->K2_DestroyActor();

		Inventory = nullptr;
		Pawn = nullptr;
		PlayerState = nullptr;
		Controller = nullptr;
	}

	bool Initialize(const FTransform& SpawnTransform, AActor* InSpawnLocator, EPlayerBotType InType = EPlayerBotType::Participant)
	{
		auto World = GetWorld();

		if (!World)
		{
			LOG_ERROR(LogBots, "[BotLifecycle] Spawn failed because the world is unavailable.");
			return false;
		}

		auto GameState = Cast<AFortGameStateAthena>(World->GetGameState());
		auto GameMode = Cast<AFortGameModeAthena>(World->GetGameMode());
		Type = InType;

		if (!GameState || !GameMode)
		{
			LOG_ERROR(LogBots, "[BotLifecycle] Spawn failed because Athena game state or game mode is unavailable.");
			return false;
		}

		if (!IsReadyToSpawnBot())
		{
			LOG_ERROR(LogBots, "We are not prepared to spawn a bot!");
			return false;
		}

		if (!ShouldUseAIBotController())
		{
			Controller = World->SpawnActor<AController>(ControllerClass);
			Pawn = World->SpawnActor<AFortPlayerPawnAthena>(PawnClass, SpawnTransform, CreateSpawnParameters(ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn));
			PlayerState = Controller ? Cast<AFortPlayerStateAthena>(Controller->GetPlayerState()) : nullptr;
		}
		else
		{
			Pawn = GameMode->GetServerBotManager()->GetCachedBotMutator()->SpawnBot(PawnClass, InSpawnLocator, SpawnTransform.Translation, SpawnTransform.Rotation.Rotator(), false);

			if (Pawn && Fortnite_Version < 17)
				Controller = Cast<AFortAthenaAIBotController>(Pawn->GetController());
			else if (Pawn)
				Controller = World->SpawnActor<AFortAthenaAIBotController>(Pawn->GetAIControllerClass());

			PlayerState = Controller ? Cast<AFortPlayerStateAthena>(Controller->GetPlayerState()) : nullptr;
		}

		if (!Controller || !Pawn || !PlayerState)
		{
			LOG_ERROR(LogBots, "[BotLifecycle] Partial spawn failed (controller={} pawn={} playerstate={}).",
				Controller != nullptr, Pawn != nullptr, PlayerState != nullptr);
			CleanupPartialSpawn();
			return false;
		}

		bIsAthenaController = Cast<AFortPlayerControllerAthena>(Controller) != nullptr;
		PlayerState->SetIsBot(true);

		if (Controller->GetPawn() != Pawn)
		{
			Controller->Possess(Pawn);
		}

		FString BotNewName = GetRandomName();
		auto BotDisplayName = BotNewName.ToString();
		
		LOG_INFO(LogBots, "BotNewName: {}", BotDisplayName);
		SetName(BotNewName);

		PlayerState->GetTeamIndex() = GameMode->Athena_PickTeamHook(GameMode, 0, Controller);

		static auto SquadIdOffset = PlayerState->GetOffset("SquadId", false);

		if (SquadIdOffset != -1)
			PlayerState->GetSquadId() = PlayerState->GetTeamIndex() - NumToSubtractFromSquadId;

		Pawn->SetHealth(100);
		Pawn->SetMaxHealth(100);

		auto PlayerAbilitySet = GetPlayerAbilitySet();
		auto AbilitySystemComponent = PlayerState->GetAbilitySystemComponent();

		if (PlayerAbilitySet && AbilitySystemComponent)
		{
			PlayerAbilitySet->GiveToAbilitySystem(AbilitySystemComponent);
		}

		if (!SetupInventory())
		{
			LOG_ERROR(LogBots, "[BotLifecycle] Inventory setup failed; rolling back partial spawn.");
			CleanupPartialSpawn();
			return false;
		}

		GameState->AddPlayerStateToGameMemberInfo(PlayerState);
		PickRandomLoadout();
		ApplyCosmeticLoadout();

		BotId = Bots::GetRegistry().RegisterBot(Type, Controller, Pawn, PlayerState, Inventory, BotDisplayName);

		if (!BotId)
		{
			LOG_ERROR(LogBots, "[BotLifecycle] Registry insertion failed; rolling back spawned objects.");
			CleanupPartialSpawn();
			return false;
		}

		if (!ShouldUseAIBotController() && !Bots::AddBotToAlivePlayerTracking(BotId))
		{
			LOG_ERROR(LogBots, "[BotLifecycle] Failed to establish match tracking for bot {}.", BotId);
			Bots::CleanupRegisteredBot(BotId, "alive tracking setup failed");
			Controller = nullptr;
			Pawn = nullptr;
			PlayerState = nullptr;
			Inventory = nullptr;
			BotId = 0;
			return false;
		}

		LOG_INFO(LogBots, "[BotLifecycle] Spawn succeeded for bot {} type={}.", BotId, Bots::BotTypeToString(Type));
		return true;
	}
};

inline std::vector<PlayerBot> AllPlayerBotsToTick;

namespace Bots
{
	struct FBotSpawnResult
	{
		uint64 BotId = 0;
		AController* Controller = nullptr;

		explicit operator bool() const { return BotId != 0 && Controller != nullptr; }
	};

	inline void ForgetRuntimeBot(uint64 BotId)
	{
		AllPlayerBotsToTick.erase(std::remove_if(AllPlayerBotsToTick.begin(), AllPlayerBotsToTick.end(), [BotId](const PlayerBot& Bot) {
			return Bot.BotId == BotId;
		}), AllPlayerBotsToTick.end());
	}

	inline FBotSpawnResult SpawnBotDetailed(FTransform SpawnTransform, AActor* InSpawnLocator,
		EPlayerBotType Type = EPlayerBotType::Participant)
	{
		LOG_INFO(LogBots, "[BotCommand] Spawn requested type={}.", BotTypeToString(Type));

		auto PlayerBotToSpawn = PlayerBot();

		if (!PlayerBotToSpawn.Initialize(SpawnTransform, InSpawnLocator, Type))
		{
			LOG_ERROR(LogBots, "[BotCommand] Spawn failed type={}.", BotTypeToString(Type));
			return {};
		}

		FBotSpawnResult Result{ PlayerBotToSpawn.BotId, PlayerBotToSpawn.Controller };
		AllPlayerBotsToTick.push_back(std::move(PlayerBotToSpawn));
		return Result;
	}

	inline AController* SpawnBot(FTransform SpawnTransform, AActor* InSpawnLocator)
	{
		return SpawnBotDetailed(SpawnTransform, InSpawnLocator, EPlayerBotType::Participant).Controller;
	}

	inline bool DespawnBot(uint64 BotId, const char* Reason = "manual despawn")
	{
		LOG_INFO(LogBots, "[BotCommand] Despawn requested for bot {}.", BotId);
		const bool bCleaned = CleanupRegisteredBot(BotId, Reason);

		if (bCleaned)
			ForgetRuntimeBot(BotId);

		return bCleaned;
	}

	inline bool ForceKillBotForStressTest(uint64 BotId, AController* KillerController)
	{
		auto Entry = GetRegistry().GetBot(BotId);

		if (!Entry || Entry->Type != EPlayerBotType::Practice ||
			Entry->State != EPlayerBotLifecycleState::Alive)
		{
			LOG_WARN(LogBots, "[BotStress] Bot {} is not an alive Practice bot.", BotId);
			return false;
		}

		auto Pawn = Entry->Pawn.Resolve<AFortPlayerPawnAthena>();

		if (!Pawn)
		{
			LOG_WARN(LogBots, "[BotStress] Bot {} has no valid pawn to kill.", BotId);
			return false;
		}

		static auto ForceKillFn = FindObject<UFunction>(L"/Script/FortniteGame.FortPawn.ForceKill");

		if (!ForceKillFn)
		{
			LOG_ERROR(LogBots, "[BotStress] FortPawn.ForceKill was not found.");
			return false;
		}

		FGameplayTag DeathReason;
		AActor* KillerActor = nullptr;
		struct
		{
			FGameplayTag DeathReason;
			AController* KillerController;
			AActor* KillerActor;
		} ForceKillParams{ DeathReason, KillerController, KillerActor };

		LOG_INFO(LogBots, "[BotStress] Force-kill requested for Practice bot {}.", BotId);
		Pawn->ProcessEvent(ForceKillFn, &ForceKillParams);
		return true;
	}

	inline int DespawnAllBots(const char* Reason = "manual despawn all", bool bDestroyActors = true)
	{
		const auto Entries = GetRegistry().GetBots();
		int RemovedCount = 0;

		// Iterate a stable ID snapshot; CleanupRegisteredBot mutates the registry.
		for (const auto& Entry : Entries)
		{
			if (CleanupRegisteredBot(Entry.BotId, Reason, bDestroyActors))
			{
				ForgetRuntimeBot(Entry.BotId);
				++RemovedCount;
			}
		}

		return RemovedCount;
	}

	inline void SweepInvalidBots()
	{
		const auto InvalidBotIds = GetRegistry().CollectInvalidAliveBotIds();

		for (const auto BotId : InvalidBotIds)
			DespawnBot(BotId, "invalid UObject reference");
	}

	inline void HandleMatchReset(const char* Reason)
	{
		LOG_INFO(LogBots, "[BotLifecycle] Match reset cleanup requested: {}.", Reason);
		const int RemovedCount = DespawnAllBots(Reason);
		LOG_INFO(LogBots, "[BotLifecycle] Match reset cleanup removed {} bots.", RemovedCount);
	}

	inline void PrepareForWorld(UWorld* World)
	{
		static FSafeBotObjectReference LastWorld;
		static int LastRestartGeneration = -1;

		auto PreviousWorld = LastWorld.Resolve<UWorld>();
		const bool bWorldChanged = PreviousWorld && PreviousWorld != World;
		const bool bRestarted = LastRestartGeneration != -1 && LastRestartGeneration != AmountOfRestarts;

		if ((bWorldChanged || bRestarted) && GetRegistry().Num() > 0)
			HandleMatchReset(bWorldChanged ? "world changed" : "new match");

		LastWorld = FSafeBotObjectReference::Capture(World);
		LastRestartGeneration = AmountOfRestarts;
		SweepInvalidBots();
	}

	inline void Shutdown(bool bDestroyActors)
	{
		if (bDestroyActors)
			DespawnAllBots("bot-system shutdown");
		else
		{
			AllPlayerBotsToTick.clear();
			GetRegistry().InvalidateAndClear("DLL shutdown", false);
		}
	}

	inline void SpawnBotsAtPlayerStarts(int AmountOfBots)
	{
		return;

		auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());

		static auto FortPlayerStartCreativeClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlayerStartCreative");
		static auto FortPlayerStartWarmupClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlayerStartWarmup");
		TArray<AActor*> PlayerStarts = UGameplayStatics::GetAllActorsOfClass(GetWorld(), Globals::bCreative ? FortPlayerStartCreativeClass : FortPlayerStartWarmupClass);

		int ActorsNum = PlayerStarts.Num();

		// Actors.Free();

		if (ActorsNum == 0)
		{
			// LOG_INFO(LogDev, "No Actors!");
			return;
		}

		// Find playerstart (scuffed)

		for (int i = 0; i < AmountOfBots; ++i)
		{
			AActor* PlayerStart = PlayerStarts.at(std::rand() % (PlayerStarts.size() - 1));

			if (!PlayerStart)
			{
				return;
			}

			auto NewBot = SpawnBot(PlayerStart->GetTransform(), PlayerStart);
			NewBot->SetCanBeDamaged(Fortnite_Version < 7); // idk lol for spawn island
		}

		return;
	}

	inline void Tick()
	{
		SweepInvalidBots();

		if (AllPlayerBotsToTick.size() == 0)
			return;

		auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());

		// auto AllBuildingContainers = UGameplayStatics::GetAllActorsOfClass(GetWorld(), ABuildingContainer::StaticClass());

		// for (int i = 0; i < GameMode->GetAlivePlayers().Num(); ++i)
		for (auto& PlayerBot : AllPlayerBotsToTick)
		{
			auto RegistryEntry = GetRegistry().GetBot(PlayerBot.BotId);

			if (!RegistryEntry || RegistryEntry->State != EPlayerBotLifecycleState::Alive)
				continue;

			auto CurrentPlayer = PlayerBot.Controller;

			if (!CurrentPlayer || !CurrentPlayer->IsValidLowLevel() || CurrentPlayer->IsActorBeingDestroyed())
				continue;

			auto CurrentPawn = CurrentPlayer->GetPawn();

			if (!CurrentPawn || !CurrentPawn->IsValidLowLevel() || CurrentPawn->IsActorBeingDestroyed())
				continue;

			auto CurrentPlayerState = Cast<AFortPlayerStateAthena>(CurrentPlayer->GetPlayerState());

			if (!CurrentPlayerState 
				// || !CurrentPlayerState->IsBot()
				)
				continue;

			if (GameState->GetGamePhase() == EAthenaGamePhase::Warmup)
			{
				/* if (!CurrentPlayer->IsPlayingEmote())
				{
					static auto AthenaDanceItemDefinitionClass = FindObject<UClass>("/Script/FortniteGame.AthenaDanceItemDefinition");
					auto RandomDanceID = GetRandomObjectOfClass(AthenaDanceItemDefinitionClass);

					CurrentPlayer->ServerPlayEmoteItemHook(CurrentPlayer, RandomDanceID);
				} */
			}	

			if (PlayerBot.bIsAthenaController && CurrentPlayerState->IsInAircraft() && !CurrentPlayerState->HasThankedBusDriver())
			{
				static auto ServerThankBusDriverFn = FindObject<UFunction>(L"/Script/FortniteGame.FortPlayerControllerAthena.ServerThankBusDriver");
				CurrentPlayer->ProcessEvent(ServerThankBusDriverFn);
			}

			if (CurrentPawn)
			{
				if (PlayerBot.NextJumpTime <= UGameplayStatics::GetTimeSeconds(GetWorld()))
				{
					static auto JumpFn = FindObject<UFunction>(L"/Script/Engine.Character.Jump");

					CurrentPawn->ProcessEvent(JumpFn);
					PlayerBot.NextJumpTime = UGameplayStatics::GetTimeSeconds(GetWorld()) + (rand() % 4 + 3);
				}
			}

			/* bool bShouldJumpFromBus = CurrentPlayerState->IsInAircraft(); // TODO (Milxnor) add a random percent thing

			if (bShouldJumpFromBus)
			{
				CurrentPlayer->ServerAttemptAircraftJumpHook(CurrentPlayer, FRotator());
			} */
		}

		// AllBuildingContainers.Free();
	}
}

namespace Bosses
{

}
