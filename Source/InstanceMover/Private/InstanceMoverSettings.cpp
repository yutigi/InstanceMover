#include "InstanceMoverSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(InstanceMoverSettings)

UInstanceMoverSettings::UInstanceMoverSettings()
{
	PivotMode = EInstanceMoverPivotMode::SelectionCenter;

	SnapTraceDistance = 100000.0f;
	SnapTraceChannel = ECC_Visibility;
	bAlignToSurfaceNormal = false;
	bRestOnBoundsBottom = true;
	SurfaceOffset = 0.0f;

	SelectionColor = FLinearColor(1.0f, 0.55f, 0.0f);
	SelectionLineThickness = 1.5f;
}
