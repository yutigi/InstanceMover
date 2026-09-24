#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UInstancedStaticMeshComponent;

#pragma region InstanceRef

/**
 * One instance of an instanced static mesh component, addressed by (component, index).
 *
 * An index is only as durable as the component's instance array: removing instances shifts the ones after them, and
 * an undo can shrink the array under a selection. Nothing here tracks that — FInstanceMoverSelection::RemoveInvalid()
 * prunes refs that no longer resolve, and the operations that remove instances clear the selection outright.
 */
struct INSTANCEMOVER_API FInstanceMoverInstanceRef
{
	TWeakObjectPtr<UInstancedStaticMeshComponent> Component;
	int32 InstanceIndex;

	FInstanceMoverInstanceRef();
	FInstanceMoverInstanceRef(UInstancedStaticMeshComponent* InComponent, int32 InInstanceIndex);

	/** True while the component is alive and still has an instance at this index. */
	bool IsValid() const;

	bool GetWorldTransform(FTransform& OutTransform) const;

	/** World-space AABB of the instance's mesh bounds; false when the ref is stale or the component has no mesh. */
	bool GetWorldBounds(FBox& OutBounds) const;

	/** Local-space bounds of the component's static mesh — the box every instance of it is drawn and picked with. */
	static FBox GetLocalMeshBounds(const UInstancedStaticMeshComponent* InComponent);

	bool operator==(const FInstanceMoverInstanceRef& Other) const;

	friend uint32 GetTypeHash(const FInstanceMoverInstanceRef& Ref)
	{
		return HashCombine(GetTypeHash(Ref.Component), ::GetTypeHash(Ref.InstanceIndex));
	}
};

#pragma endregion

#pragma region Selection

/**
 * The mode's instance selection. A set rather than an array because a marquee over a dense scatter adds thousands of
 * instances one at a time, and an array's Contains() would make that quadratic.
 */
class INSTANCEMOVER_API FInstanceMoverSelection
{
public:
	/** @return true if the instance was not already selected. It becomes the last-selected instance either way. */
	bool Add(const FInstanceMoverInstanceRef& Ref);

	/** @return true if the instance was selected. */
	bool Remove(const FInstanceMoverInstanceRef& Ref);

	void Toggle(const FInstanceMoverInstanceRef& Ref);
	void Clear();

	bool Contains(const FInstanceMoverInstanceRef& Ref) const;
	FORCEINLINE int32 Num() const { return Instances.Num(); }
	FORCEINLINE bool IsEmpty() const { return Instances.IsEmpty(); }
	FORCEINLINE const TSet<FInstanceMoverInstanceRef>& GetInstances() const { return Instances; }

	/** Drops refs whose component died or whose index fell off the end of its instance array. @return number dropped. */
	int32 RemoveInvalid();

	/** The most recently added instance still in the selection, or null when empty. Drives the local gizmo axes. */
	const FInstanceMoverInstanceRef* GetLastSelected() const;

	/** Valid selected indices bucketed per component, each bucket sorted ascending. */
	TMap<UInstancedStaticMeshComponent*, TArray<int32>> GroupByComponent() const;

	/** Mean world location of the valid selected instances. */
	bool ComputeCentroid(FVector& OutCentroid) const;

	/** Union of the valid selected instances' world bounds. */
	bool ComputeBounds(FBox& OutBounds) const;

private:
	TSet<FInstanceMoverInstanceRef> Instances;
	FInstanceMoverInstanceRef LastSelected;
};

#pragma endregion

#pragma region Math

/** Pure transform math behind the mode, kept free of editor state so the automation tests can drive it directly. */
struct INSTANCEMOVER_API FInstanceMoverMath
{
	/**
	 * Applies one frame of gizmo input to an instance's world transform, in the same order and with the same pivot
	 * handling as the level editor applies it to actors (FTypedElementViewportInteractionCustomization): rotate about
	 * the pivot, then translate, then scale about the pivot. Matching it is the point — a selection of instances has
	 * to swing, slide and spread exactly the way a selection of actors does under the same gizmo drag.
	 */
	static FTransform ApplyGizmoDelta(const FTransform& InstanceWorld, const FVector& Pivot, const FVector& DeltaTranslation,
		const FRotator& DeltaRotation, const FVector& DeltaScale);

	/** Slab test of a ray against an axis-aligned box. OutDistance is 0 when the origin is inside the box. */
	static bool RayBoxIntersection(const FBox& Box, const FVector& Origin, const FVector& Direction, double& OutDistance);

	/**
	 * Ray against an instance's oriented mesh bounds. The ray is taken into the instance's local space rather than the
	 * box into world space, so rotation and non-uniform scale cost nothing extra; OutDistance stays a world-space
	 * distance along Direction because that mapping is linear.
	 */
	static bool RayInstanceIntersection(const FTransform& InstanceWorld, const FBox& LocalBounds, const FVector& Origin,
		const FVector& Direction, double& OutDistance);

	/**
	 * Where an instance lands when dropped onto a surface hit.
	 * @param bAlignToNormal       Tilt the instance so its local up matches the surface normal.
	 * @param bRestOnBoundsBottom  Sit the bottom of the mesh bounds on the surface instead of the pivot — most meshes
	 *                             are not authored with their pivot at the base.
	 * @param SurfaceOffset        Extra distance along the surface up (negative sinks the instance in).
	 */
	static FTransform ComputeSurfaceSnapTransform(const FTransform& InstanceWorld, const FBox& LocalBounds,
		const FVector& HitLocation, const FVector& HitNormal, bool bAlignToNormal, bool bRestOnBoundsBottom, double SurfaceOffset);
};

#pragma endregion
