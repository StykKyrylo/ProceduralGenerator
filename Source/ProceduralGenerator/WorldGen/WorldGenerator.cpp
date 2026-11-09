#include "WorldGenerator.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_EDITOR
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeProxy.h"
#endif

THIRD_PARTY_INCLUDES_START
#include "wg/climate.h"
#include "wg/png.h"
#include "wg/settlements.h"
#include "wg/terrain.h"
THIRD_PARTY_INCLUDES_END

DEFINE_LOG_CATEGORY_STATIC(LogWorldGen, Log, All);

namespace
{
	FColor RoleColor(wg::ModuleRole Role)
	{
		switch (Role)
		{
		case wg::ModuleRole::GateMain: return FColor::Red;
		case wg::ModuleRole::GateSide: return FColor::Orange;
		case wg::ModuleRole::HallMain: return FColor::Yellow;
		case wg::ModuleRole::Residence: return FColor::Green;
		case wg::ModuleRole::Pavilion: return FColor::Cyan;
		case wg::ModuleRole::Shrine: return FColor::Magenta;
		default: return FColor::White;
		}
	}
}

AWorldGenerator::AWorldGenerator()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AWorldGenerator::ClearGenerated()
{
	for (TObjectPtr<AActor>& A : SpawnedActors)
	{
		if (IsValid(A))
		{
			A->Destroy();
		}
	}
	SpawnedActors.Reset();

	if (IsValid(LandscapeActor))
	{
		LandscapeActor->Destroy();
	}
	LandscapeActor = nullptr;

	if (UWorld* World = GetWorld())
	{
		FlushPersistentDebugLines(World);
	}
}

void AWorldGenerator::Generate()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	ClearGenerated();

	const double T0 = FPlatformTime::Seconds();

	// ---------- Конфігурація ----------
	wg::WorldConfig Cfg;
	Cfg.seed = static_cast<uint32_t>(Seed);
	Cfg.cellMeters = CellMeters;
	Cfg.terrain.noise = bUsePerlin ? wg::NoiseType::Perlin : wg::NoiseType::Simplex;
	Cfg.ScaleWorld(WorldScale);

	// ---------- Етап 1: рельєф (шум) ----------
	const wg::TerrainResult Terrain = wg::GenerateTerrain(Cfg);
	const double T1 = FPlatformTime::Seconds();

	// ---------- Етап 2: клімат і біоми (правила) ----------
	const wg::ClimateResult Climate = wg::GenerateClimate(Cfg, Terrain);
	const double T2 = FPlatformTime::Seconds();

	// ---------- Етап 3: комплекси з модулів ----------
	const std::vector<wg::Settlement> Settlements = wg::GenerateSettlements(Cfg, Terrain, Climate);
	const double T3 = FPlatformTime::Seconds();

	const int32 W = Terrain.height.w;
	const int32 H = Terrain.height.h;
	UE_LOG(LogWorldGen, Log, TEXT("Світ %.0fx%.0f км, сітка %dx%d, суша %.0f км2, макс. висота %.0f м"),
		Cfg.widthKm, Cfg.heightKm, W, H, Terrain.landKm2, Terrain.maxHeightM);
	UE_LOG(LogWorldGen, Log, TEXT("Час: рельєф %.2f с, клімат %.2f с, комплекси %.2f с"),
		T1 - T0, T2 - T1, T3 - T2);
	UE_LOG(LogWorldGen, Log, TEXT("Комплексів: %d"), static_cast<int32>(Settlements.size()));

	// ---------- Експорт файлів (запасний шлях імпорту та ілюстрації для звіту) ----------
	if (bExportFiles)
	{
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("WorldGen");
		IFileManager::Get().MakeDirectory(*Dir, true);

		TArray<uint8> R16;
		R16.SetNumUninitialized(W * H * 2);
		for (int32 i = 0; i < W * H; ++i)
		{
			const float HeightM = Terrain.height.data[i];
			const int32 V = FMath::Clamp(FMath::RoundToInt(32768.f + HeightM * 16.f), 0, 65535);
			R16[i * 2] = static_cast<uint8>(V & 0xFF);
			R16[i * 2 + 1] = static_cast<uint8>((V >> 8) & 0xFF);
		}
		FFileHelper::SaveArrayToFile(R16, *(Dir / FString::Printf(TEXT("height_%dx%d_seed%d.r16"), W, H, Seed)));

		// PNG біомів (1 піксель = 1 клітинка) — також текстура для матеріалу ландшафту.
		std::vector<uint8_t> Rgb(static_cast<size_t>(W) * H * 3);
		for (int32 i = 0; i < W * H; ++i)
		{
			uint8_t R, G, B;
			wg::BiomeColor(static_cast<wg::Biome>(Climate.biome.data[i]), R, G, B);
			Rgb[i * 3] = R;
			Rgb[i * 3 + 1] = G;
			Rgb[i * 3 + 2] = B;
		}
		const FString PngPath = Dir / FString::Printf(TEXT("biomes_seed%d.png"), Seed);
		wg::WritePngRgb(std::string(TCHAR_TO_UTF8(*PngPath)), W, H, Rgb.data());
		UE_LOG(LogWorldGen, Log, TEXT("Файли записано у %s"), *Dir);
	}

	const float XYScale = CellMeters * 100.f;  // см на квад

	// ---------- Landscape ----------
#if WITH_EDITOR
	if (bCreateLandscape)
	{
		const int32 QuadsPerSection = 63;
		const int32 SectionsPerComponent = 2;
		const int32 ComponentQuads = QuadsPerSection * SectionsPerComponent;
		const int32 CompX = FMath::DivideAndRoundUp(W - 1, ComponentQuads);
		const int32 CompY = FMath::DivideAndRoundUp(H - 1, ComponentQuads);
		const int32 HW = CompX * ComponentQuads + 1;
		const int32 HH = CompY * ComponentQuads + 1;

		// ZScale 800: висота в см = (value - 32768) * 800 / 128  =>  value = 32768 + метри * 16.
		const float ZScale = 800.f;
		TArray<uint16> HeightData;
		HeightData.SetNumUninitialized(HW * HH);
		for (int32 y = 0; y < HH; ++y)
		{
			for (int32 x = 0; x < HW; ++x)
			{
				const float HeightM = Terrain.height.at(FMath::Min(x, W - 1), FMath::Min(y, H - 1));
				HeightData[y * HW + x] = static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(32768.f + HeightM * 16.f), 0, 65535));
			}
		}

		ALandscape* Landscape = World->SpawnActor<ALandscape>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Landscape)
		{
			Landscape->SetActorTransform(FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector(XYScale, XYScale, ZScale)));
			if (LandscapeMaterial)
			{
				Landscape->LandscapeMaterial = LandscapeMaterial;
			}

			TMap<FGuid, TArray<uint16>> HeightByLayer;
			HeightByLayer.Add(FGuid(), MoveTemp(HeightData));
			TMap<FGuid, TArray<FLandscapeImportLayerInfo>> LayerInfos;
			LayerInfos.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

			Landscape->Import(FGuid::NewGuid(), 0, 0, HW - 1, HH - 1, SectionsPerComponent, QuadsPerSection,
				HeightByLayer, nullptr, LayerInfos, ELandscapeImportAlphamapType::Additive,
				TArrayView<const FLandscapeLayer>());

			if (ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
			{
				Info->UpdateLayerInfoMap(Landscape);
			}
			Landscape->RegisterAllComponents();
			Landscape->SetActorLabel(TEXT("WorldGen_Landscape"));
			LandscapeActor = Landscape;
			UE_LOG(LogWorldGen, Log, TEXT("Landscape створено: %dx%d вершин, %dx%d компонентів"), HW, HH, CompX, CompY);
		}
		else
		{
			UE_LOG(LogWorldGen, Error, TEXT("Не вдалося створити ALandscape"));
		}
	}
#else
	UE_LOG(LogWorldGen, Warning, TEXT("Landscape створюється лише в редакторі"));
#endif

	// ---------- Комплекси: модулі за ролями ----------
	if (bSpawnSettlements)
	{
		int32 Spawned = 0;
		for (const wg::Settlement& S : Settlements)
		{
			if (bDrawDebugMarkers)
			{
				const FVector C(S.x * 100.f, S.y * 100.f, S.z * 100.f + 200.f);
				DrawDebugCircle(World, C, S.radiusM * 100.f, 48, FColor::White, true, -1.f, 0, 150.f,
					FVector(1, 0, 0), FVector(0, 1, 0), false);
			}
			for (const wg::ModulePlacement& M : S.modules)
			{
				const FVector Loc(M.x * 100.f, M.y * 100.f, M.z * 100.f);
				const FRotator Rot(0.f, M.yawDeg + ModelYawOffsetDeg, 0.f);

				const FWGModuleVariants* Variants = ModuleClasses.Find(static_cast<EWGModuleRole>(M.role));
				if (Variants && Variants->Classes.Num() > 0)
				{
					TSubclassOf<AActor> Cls = Variants->Classes[M.variant % Variants->Classes.Num()];
					if (*Cls)
					{
						FActorSpawnParameters Params;
						Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
						AActor* A = World->SpawnActor<AActor>(Cls, FTransform(Rot, Loc, FVector(M.scale)), Params);
						if (A)
						{
#if WITH_EDITOR
							A->SetFolderPath(FName(TEXT("WorldGen/Settlements")));
#endif
							SpawnedActors.Add(A);
							++Spawned;
							continue;
						}
					}
				}
				if (bDrawDebugMarkers)
				{
					DrawDebugSphere(World, Loc + FVector(0, 0, 500.f), 1500.f, 12, RoleColor(M.role), true, -1.f, 0, 80.f);
				}
			}
		}
		UE_LOG(LogWorldGen, Log, TEXT("Створено акторів-модулів: %d (решта показана маркерами)"), Spawned);
	}

	const FVector CenterCm(Cfg.widthKm * 1000.0 * 50.0, Cfg.heightKm * 1000.0 * 50.0, 0.0);
	UE_LOG(LogWorldGen, Log, TEXT("Центр світу (см): X=%.0f Y=%.0f. Загальний час: %.2f с"),
		CenterCm.X, CenterCm.Y, FPlatformTime::Seconds() - T0);
}
