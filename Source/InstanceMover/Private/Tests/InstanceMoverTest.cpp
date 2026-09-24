#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "InstanceMoverTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FInstanceMoverGizmoDeltaTest,
	"PetwallParade.InstanceMover.GizmoDelta",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FInstanceMoverRayPickTest,
	"PetwallParade.InstanceMover.RayPick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FInstanceMoverSurfaceSnapTest,
	"PetwallParade.InstanceMover.SurfaceSnap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FInstanceMoverSelectionTest,
	"PetwallParade.InstanceMover.Selection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	constexpr double Tolerance = 1.e-3;
}

bool FInstanceMoverGizmoDeltaTest::RunTest(const FString& Parameters)
{
	const FTransform Instance(FQuat::Identity, FVector(100.0, 0.0, 0.0), FVector::OneVector);

	// Translation alone just slides the instance.
	{
		const FTransform Moved = FInstanceMoverMath::ApplyGizmoDelta(Instance, FVector::ZeroVector, FVector(10.0, 20.0, 30.0),
			FRotator::ZeroRotator, FVector::ZeroVector);
		TestTrue(TEXT("Translation offsets the location"), Moved.GetLocation().Equals(FVector(110.0, 20.0, 30.0), Tolerance));
		TestTrue(TEXT("Translation leaves rotation alone"), Moved.GetRotation().Equals(FQuat::Identity, Tolerance));
	}

	// A yaw about a pivot the instance is not on must swing it round the pivot *and* turn it — rotating in place would
	// leave a multi-selection facing the new way while standing where it was.
	{
		const FTransform Rotated = FInstanceMoverMath::ApplyGizmoDelta(Instance, FVector::ZeroVector, FVector::ZeroVector,
			FRotator(0.0, 90.0, 0.0), FVector::ZeroVector);
		TestTrue(TEXT("Rotation orbits the pivot"), Rotated.GetLocation().Equals(FVector(0.0, 100.0, 0.0), Tolerance));
		TestTrue(TEXT("Rotation turns the instance"), Rotated.GetRotation().Equals(FRotator(0.0, 90.0, 0.0).Quaternion(), Tolerance));
	}

	// Rotating about the instance's own location turns it in place.
	{
		const FTransform Spun = FInstanceMoverMath::ApplyGizmoDelta(Instance, Instance.GetLocation(), FVector::ZeroVector,
			FRotator(0.0, 45.0, 0.0), FVector::ZeroVector);
		TestTrue(TEXT("Rotation about own pivot does not move it"), Spun.GetLocation().Equals(Instance.GetLocation(), Tolerance));
	}

	// Scale grows the instance and spreads it away from the pivot in proportion.
	{
		const FTransform Scaled = FInstanceMoverMath::ApplyGizmoDelta(Instance, FVector::ZeroVector, FVector::ZeroVector,
			FRotator::ZeroRotator, FVector(1.0, 1.0, 1.0));
		TestTrue(TEXT("Scale delta is added to the scale"), Scaled.GetScale3D().Equals(FVector(2.0), Tolerance));
		TestTrue(TEXT("Scale spreads the instance from the pivot"), Scaled.GetLocation().Equals(FVector(200.0, 0.0, 0.0), Tolerance));
	}

	return true;
}

bool FInstanceMoverRayPickTest::RunTest(const FString& Parameters)
{
	const FBox UnitBox(FVector(-50.0), FVector(50.0));
	double Distance = -1.0;

	TestTrue(TEXT("Ray from outside hits the box"), FInstanceMoverMath::RayBoxIntersection(UnitBox, FVector(-200.0, 0.0, 0.0), FVector::ForwardVector, Distance));
	TestEqual(TEXT("Hit distance is to the near face"), Distance, 150.0, Tolerance);

	TestFalse(TEXT("Ray passing beside the box misses"), FInstanceMoverMath::RayBoxIntersection(UnitBox, FVector(-200.0, 80.0, 0.0), FVector::ForwardVector, Distance));
	TestFalse(TEXT("Ray pointing away misses"), FInstanceMoverMath::RayBoxIntersection(UnitBox, FVector(-200.0, 0.0, 0.0), -FVector::ForwardVector, Distance));

	TestTrue(TEXT("Ray starting inside hits"), FInstanceMoverMath::RayBoxIntersection(UnitBox, FVector::ZeroVector, FVector::UpVector, Distance));
	TestEqual(TEXT("Ray starting inside reports zero"), Distance, 0.0, Tolerance);

	TestFalse(TEXT("Invalid box never hits"), FInstanceMoverMath::RayBoxIntersection(FBox(ForceInit), FVector::ZeroVector, FVector::UpVector, Distance));

	// An instance stretched 4x along X and turned 90 degrees reaches 200 units along world Y: a ray that would miss the
	// unscaled box has to hit it, at a world-space distance.
	const FTransform Stretched(FRotator(0.0, 90.0, 0.0), FVector(0.0, 0.0, 0.0), FVector(4.0, 1.0, 1.0));
	TestTrue(TEXT("Ray hits the rotated, scaled instance"),
		FInstanceMoverMath::RayInstanceIntersection(Stretched, UnitBox, FVector(0.0, 180.0, 500.0), -FVector::UpVector, Distance));
	TestEqual(TEXT("Instance hit distance is in world units"), Distance, 450.0, Tolerance);
	TestFalse(TEXT("Ray beside the stretched instance misses"),
		FInstanceMoverMath::RayInstanceIntersection(Stretched, UnitBox, FVector(180.0, 0.0, 500.0), -FVector::UpVector, Distance));

	return true;
}

bool FInstanceMoverSurfaceSnapTest::RunTest(const FString& Parameters)
{
	const FBox UnitBox(FVector(-50.0), FVector(50.0));
	const FTransform Floating(FQuat::Identity, FVector(10.0, 20.0, 500.0), FVector(1.0, 1.0, 2.0));

	// Flat ground, bounds bottom: the pivot ends up half the (scaled) height above the hit.
	{
		const FTransform Snapped = FInstanceMoverMath::ComputeSurfaceSnapTransform(Floating, UnitBox, FVector(10.0, 20.0, 0.0),
			FVector::UpVector, /*bAlignToNormal*/ false, /*bRestOnBoundsBottom*/ true, /*SurfaceOffset*/ 0.0);
		TestTrue(TEXT("Bounds bottom rests on the ground"), Snapped.GetLocation().Equals(FVector(10.0, 20.0, 100.0), Tolerance));
	}

	// Pivot mode ignores the bounds, and the offset lifts it.
	{
		const FTransform Snapped = FInstanceMoverMath::ComputeSurfaceSnapTransform(Floating, UnitBox, FVector(10.0, 20.0, 0.0),
			FVector::UpVector, false, false, 5.0);
		TestTrue(TEXT("Pivot rests on the ground plus offset"), Snapped.GetLocation().Equals(FVector(10.0, 20.0, 5.0), Tolerance));
	}

	// On a 45 degree slope with alignment, the instance's up follows the normal and it stands off along it.
	{
		const FVector SlopeNormal = FVector(1.0, 0.0, 1.0).GetSafeNormal();
		const FTransform Snapped = FInstanceMoverMath::ComputeSurfaceSnapTransform(Floating, UnitBox, FVector::ZeroVector,
			SlopeNormal, true, true, 0.0);
		TestTrue(TEXT("Aligned up axis matches the surface normal"), Snapped.GetRotation().GetUpVector().Equals(SlopeNormal, Tolerance));
		TestTrue(TEXT("Aligned instance stands off along the normal"), Snapped.GetLocation().Equals(SlopeNormal * 100.0, Tolerance));
	}

	return true;
}

bool FInstanceMoverSelectionTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UInstancedStaticMeshComponent> Component(NewObject<UInstancedStaticMeshComponent>(GetTransientPackage()));
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Component->AddInstance(FTransform(FVector(100.0 * Index, 0.0, 0.0)));
	}

	FInstanceMoverSelection Selection;
	const FInstanceMoverInstanceRef First(Component.Get(), 0);
	const FInstanceMoverInstanceRef Last(Component.Get(), 3);

	TestTrue(TEXT("First add is new"), Selection.Add(Last));
	TestTrue(TEXT("Second instance is new"), Selection.Add(First));
	TestFalse(TEXT("Re-adding is not new"), Selection.Add(First));
	TestEqual(TEXT("Set holds two instances"), Selection.Num(), 2);
	TestTrue(TEXT("Last added is last selected"), Selection.GetLastSelected() && *Selection.GetLastSelected() == First);

	// Removing the last-selected instance must hand the role to a survivor, not leave the gizmo pointing at nothing.
	Selection.Toggle(First);
	TestFalse(TEXT("Toggle removes a selected instance"), Selection.Contains(First));
	TestTrue(TEXT("Last selected falls back to a survivor"), Selection.GetLastSelected() && *Selection.GetLastSelected() == Last);

	Selection.Toggle(First);
	FVector Centroid;
	TestTrue(TEXT("Centroid resolves"), Selection.ComputeCentroid(Centroid));
	TestTrue(TEXT("Centroid is the mean location"), Centroid.Equals(FVector(150.0, 0.0, 0.0), Tolerance));

	const TMap<UInstancedStaticMeshComponent*, TArray<int32>> Groups = Selection.GroupByComponent();
	TestEqual(TEXT("One component group"), Groups.Num(), 1);
	TestTrue(TEXT("Group indices are sorted"), Groups.Contains(Component.Get()) && Groups[Component.Get()] == TArray<int32>({ 0, 3 }));

	// Shrinking the instance array (as an undo of a duplicate does) strands the ref past the end.
	Component->RemoveInstance(3);
	TestEqual(TEXT("RemoveInvalid drops the stranded ref"), Selection.RemoveInvalid(), 1);
	TestEqual(TEXT("Valid ref survives"), Selection.Num(), 1);
	TestTrue(TEXT("Surviving ref becomes last selected"), Selection.GetLastSelected() && *Selection.GetLastSelected() == First);

	FBox Bounds;
	TestFalse(TEXT("No bounds without a mesh"), Selection.ComputeBounds(Bounds));

	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		Component->SetStaticMesh(Cube);
		TestTrue(TEXT("Bounds resolve with a mesh"), Selection.ComputeBounds(Bounds));
		TestTrue(TEXT("Bounds are the instance's mesh box"), Bounds.GetCenter().Equals(FVector::ZeroVector, Tolerance));
	}
	else
	{
		AddError(TEXT("Engine cube mesh failed to load"));
	}

	Selection.Clear();
	TestTrue(TEXT("Clear empties the selection"), Selection.IsEmpty());
	TestNull(TEXT("Clear drops the last selected"), Selection.GetLastSelected());

	return true;
}

#endif
