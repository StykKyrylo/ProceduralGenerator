#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldGenerator.generated.h"

class UMaterialInterface;

// Ролі модулів. Порядок збігається з wg::ModuleRole у ядрі.
UENUM(BlueprintType)
enum class EWGModuleRole : uint8
{
	GateMain,
	GateSide,
	HallMain,
	Residence,
	Pavilion,
	Shrine,
	GuardPavilion
};

// Список варіантів (Blueprint-класів / будівель) для однієї ролі.
USTRUCT(BlueprintType)
struct FWGModuleVariants
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldGen")
	TArray<TSubclassOf<AActor>> Classes;
};

/**
 * Генератор світу: 1) шум -> рельєф, 2) правила -> клімат і біоми, 3) модульні комплекси.
 * Розмістіть актор на рівні й натисніть кнопку Generate у панелі Details.
 * Уся логіка генерації — у C++ (папка Core), Blueprint використовуються лише як шаблони будівель.
 */
UCLASS()
class PROCEDURALGENERATOR_API AWorldGenerator : public AActor
{
	GENERATED_BODY()

public:
	AWorldGenerator();

	// --- Параметри світу ---
	UPROPERTY(EditAnywhere, Category = "WorldGen|World")
	int32 Seed = 1;

	// 1.0 = повна площа Південної Кореї (400x440 км); 0.1 = демо (40x44 км).
	UPROPERTY(EditAnywhere, Category = "WorldGen|World", meta = (ClampMin = "0.02", ClampMax = "1.0"))
	float WorldScale = 0.1f;

	// Розмір клітинки карти в метрах. Демо: 25, повний світ: 100.
	UPROPERTY(EditAnywhere, Category = "WorldGen|World", meta = (ClampMin = "5.0", ClampMax = "200.0"))
	float CellMeters = 25.f;

	// false = Simplex, true = Perlin (для порівняння в розділі з результатами).
	UPROPERTY(EditAnywhere, Category = "WorldGen|World")
	bool bUsePerlin = false;

	// --- Що створювати ---
	UPROPERTY(EditAnywhere, Category = "WorldGen|Output")
	bool bCreateLandscape = true;

	UPROPERTY(EditAnywhere, Category = "WorldGen|Output")
	bool bSpawnSettlements = true;

	// Записати heightmap (.r16) і карти (PNG) у Saved/WorldGen/.
	UPROPERTY(EditAnywhere, Category = "WorldGen|Output")
	bool bExportFiles = true;

	// Матеріал ландшафту (необов'язково).
	UPROPERTY(EditAnywhere, Category = "WorldGen|Output")
	TObjectPtr<UMaterialInterface> LandscapeMaterial;

	// --- Шаблони будівель (Blueprint-класи або актори зі StaticMesh) за ролями ---
	UPROPERTY(EditAnywhere, Category = "WorldGen|Modules")
	TMap<EWGModuleRole, FWGModuleVariants> ModuleClasses;

	// Якщо меш будівлі «дивиться» не вздовж осі X, поверніть його (наприклад, 90 або -90).
	UPROPERTY(EditAnywhere, Category = "WorldGen|Modules")
	float ModelYawOffsetDeg = 0.f;

	// Показувати кола/маркери комплексів (корисно, поки не призначені класи будівель).
	UPROPERTY(EditAnywhere, Category = "WorldGen|Modules")
	bool bDrawDebugMarkers = true;

	UFUNCTION(CallInEditor, Category = "WorldGen")
	void Generate();

	UFUNCTION(CallInEditor, Category = "WorldGen")
	void ClearGenerated();

private:
	UPROPERTY(Transient)
	TObjectPtr<AActor> LandscapeActor;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> SpawnedActors;
};
