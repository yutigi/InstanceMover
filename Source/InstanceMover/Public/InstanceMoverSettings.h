#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "UObject/Object.h"

#include "InstanceMoverSettings.generated.h"

UENUM()
enum class EInstanceMoverPivotMode : uint8
{
	/** Gizmo sits at the mean location of the selected instances; a rotation swings the group about its middle. */
	SelectionCenter,

	/** Gizmo sits on the most recently clicked instance; the rest orbit it. */
	LastSelected,
};

/**
 * Per-user options for the Instance Mover mode, shown in the mode panel.
 *
 * config=EditorPerProjectUserSettings on purpose: these are one artist's working preferences (pivot, snap behaviour,
 * highlight colour), not project behaviour, so they persist to Saved/Config for that user and never reach
 * DefaultGame.ini. UEdMode loads them on Enter() and saves them on Exit().
 */
UCLASS(config = EditorPerProjectUserSettings)
class INSTANCEMOVER_API UInstanceMoverSettings : public UObject
{
	GENERATED_BODY()

public:
	UInstanceMoverSettings();

#pragma region Gizmo

	UPROPERTY(EditAnywhere, Config, Category = "Gizmo")
	EInstanceMoverPivotMode PivotMode;

#pragma endregion

#pragma region SnapToSurface

	/** How far below the top of each instance's bounds the drop trace looks for a surface. */
	UPROPERTY(EditAnywhere, Config, Category = "Snap to Surface", meta = (ClampMin = "1", Units = "cm"))
	float SnapTraceDistance;

	/** Channel the drop trace runs on. The instance's own component is always ignored. */
	UPROPERTY(EditAnywhere, Config, Category = "Snap to Surface")
	TEnumAsByte<ECollisionChannel> SnapTraceChannel;

	/** Tilt each instance so its up axis follows the surface normal it lands on. */
	UPROPERTY(EditAnywhere, Config, Category = "Snap to Surface")
	bool bAlignToSurfaceNormal;

	/** Rest the bottom of the mesh bounds on the surface rather than the mesh pivot. */
	UPROPERTY(EditAnywhere, Config, Category = "Snap to Surface")
	bool bRestOnBoundsBottom;

	/** Extra distance above the surface after snapping. Negative values sink instances in. */
	UPROPERTY(EditAnywhere, Config, Category = "Snap to Surface", meta = (Units = "cm"))
	float SurfaceOffset;

#pragma endregion

#pragma region Display

	UPROPERTY(EditAnywhere, Config, Category = "Display")
	FLinearColor SelectionColor;

	UPROPERTY(EditAnywhere, Config, Category = "Display", meta = (ClampMin = "0", ClampMax = "10"))
	float SelectionLineThickness;

#pragma endregion
};
