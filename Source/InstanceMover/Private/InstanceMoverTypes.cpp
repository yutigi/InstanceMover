#include "InstanceMoverTypes.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Math/RotationMatrix.h"
#include "Math/ScaleMatrix.h"

#pragma region InstanceRef

FInstanceMoverInstanceRef::FInstanceMoverInstanceRef()
{
	InstanceIndex = INDEX_NONE;
}

FInstanceMoverInstanceRef::FInstanceMoverInstanceRef(UInstancedStaticMeshComponent* InComponent, int32 InInstanceIndex)
{
	Component = InComponent;
	InstanceIndex = InInstanceIndex;
}

bool FInstanceMoverInstanceRef::IsValid() const
{
	const UInstancedStaticMeshComponent* ResolvedComponent = Component.Get();
	return ResolvedComponent && ResolvedComponent->IsValidInstance(InstanceIndex);
}

bool FInstanceMoverInstanceRef::GetWorldTransform(FTransform& OutTransform) const
{
	return IsValid() && Component->GetInstanceTransform(InstanceIndex, OutTransform, /*bWorldSpace*/ true);
}

bool FInstanceMoverInstanceRef::GetWorldBounds(FBox& OutBounds) const
{
	FTransform WorldTransform;
	if (!GetWorldTransform(WorldTransform))
	{
		return false;
	}

	const FBox LocalBounds = GetLocalMeshBounds(Component.Get());
	if (!LocalBounds.IsValid)
	{
		return false;
	}

	OutBounds = LocalBounds.TransformBy(WorldTransform);
	return true;
}

FBox FInstanceMoverInstanceRef::GetLocalMeshBounds(const UInstancedStaticMeshComponent* InComponent)
{
	const UStaticMesh* Mesh = InComponent ? InComponent->GetStaticMesh() : nullptr;
	return Mesh ? Mesh->GetBoundingBox() : FBox(ForceInit);
}

bool FInstanceMoverInstanceRef::operator==(const FInstanceMoverInstanceRef& Other) const
{
	return Component == Other.Component && InstanceIndex == Other.InstanceIndex;
}

#pragma endregion

#pragma region Selection

bool FInstanceMoverSelection::Add(const FInstanceMoverInstanceRef& Ref)
{
	bool bAlreadySelected = false;
	Instances.Add(Ref, &bAlreadySelected);
	LastSelected = Ref;
	return !bAlreadySelected;
}

bool FInstanceMoverSelection::Remove(const FInstanceMoverInstanceRef& Ref)
{
	const bool bRemoved = Instances.Remove(Ref) > 0;
	if (bRemoved && LastSelected == Ref)
	{
		// Any survivor will do: it only decides which instance the local-space gizmo borrows its axes from.
		LastSelected = Instances.IsEmpty() ? FInstanceMoverInstanceRef() : *Instances.CreateConstIterator();
	}
	return bRemoved;
}

void FInstanceMoverSelection::Toggle(const FInstanceMoverInstanceRef& Ref)
{
	if (!Remove(Ref))
	{
		Add(Ref);
	}
}

void FInstanceMoverSelection::Clear()
{
	Instances.Reset();
	LastSelected = FInstanceMoverInstanceRef();
}

bool FInstanceMoverSelection::Contains(const FInstanceMoverInstanceRef& Ref) const
{
	return Instances.Contains(Ref);
}

int32 FInstanceMoverSelection::RemoveInvalid()
{
	TArray<FInstanceMoverInstanceRef> Stale;
	for (const FInstanceMoverInstanceRef& Ref : Instances)
	{
		if (!Ref.IsValid())
		{
			Stale.Add(Ref);
		}
	}

	for (const FInstanceMoverInstanceRef& Ref : Stale)
	{
		Remove(Ref);
	}
	return Stale.Num();
}

const FInstanceMoverInstanceRef* FInstanceMoverSelection::GetLastSelected() const
{
	return Instances.Contains(LastSelected) ? &LastSelected : nullptr;
}

TMap<UInstancedStaticMeshComponent*, TArray<int32>> FInstanceMoverSelection::GroupByComponent() const
{
	TMap<UInstancedStaticMeshComponent*, TArray<int32>> Groups;
	for (const FInstanceMoverInstanceRef& Ref : Instances)
	{
		if (Ref.IsValid())
		{
			Groups.FindOrAdd(Ref.Component.Get()).Add(Ref.InstanceIndex);
		}
	}

	for (TPair<UInstancedStaticMeshComponent*, TArray<int32>>& Group : Groups)
	{
		Group.Value.Sort();
	}
	return Groups;
}

bool FInstanceMoverSelection::ComputeCentroid(FVector& OutCentroid) const
{
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;
	for (const FInstanceMoverInstanceRef& Ref : Instances)
	{
		FTransform WorldTransform;
		if (Ref.GetWorldTransform(WorldTransform))
		{
			Sum += WorldTransform.GetLocation();
			++Count;
		}
	}

	if (Count == 0)
	{
		return false;
	}

	OutCentroid = Sum / Count;
	return true;
}

bool FInstanceMoverSelection::ComputeBounds(FBox& OutBounds) const
{
	FBox Total(ForceInit);
	for (const FInstanceMoverInstanceRef& Ref : Instances)
	{
		FBox InstanceBounds;
		if (Ref.GetWorldBounds(InstanceBounds))
		{
			Total += InstanceBounds;
		}
	}

	OutBounds = Total;
	return Total.IsValid != 0;
}

#pragma endregion

#pragma region Math

FTransform FInstanceMoverMath::ApplyGizmoDelta(const FTransform& InstanceWorld, const FVector& Pivot,
	const FVector& DeltaTranslation, const FRotator& DeltaRotation, const FVector& DeltaScale)
{
	FTransform Result = InstanceWorld;

	if (!DeltaRotation.IsZero())
	{
		const FQuat DeltaQuat = DeltaRotation.Quaternion();
		Result.SetRotation((DeltaQuat * Result.GetRotation()).GetNormalized());
		Result.SetTranslation(Pivot + FRotationMatrix::Make(DeltaQuat).TransformPosition(Result.GetTranslation() - Pivot));
	}

	Result.AddToTranslation(DeltaTranslation);

	if (!DeltaScale.IsNearlyZero(0.000001))
	{
		Result.SetScale3D(Result.GetScale3D() + DeltaScale);

		// Spread the instances apart (or together) as well, so a multi-selection scales as one object about the pivot.
		const FVector FromPivot = Result.GetTranslation() - Pivot;
		Result.SetTranslation(Pivot + FromPivot + FScaleMatrix::Make(DeltaScale).TransformPosition(FromPivot));
	}

	return Result;
}

bool FInstanceMoverMath::RayBoxIntersection(const FBox& Box, const FVector& Origin, const FVector& Direction, double& OutDistance)
{
	if (!Box.IsValid)
	{
		return false;
	}

	double Near = 0.0;
	double Far = TNumericLimits<double>::Max();
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const double RayOrigin = Origin[Axis];
		const double RayDirection = Direction[Axis];
		const double SlabMin = Box.Min[Axis];
		const double SlabMax = Box.Max[Axis];

		if (FMath::Abs(RayDirection) < UE_SMALL_NUMBER)
		{
			// Parallel to this slab: it either lies inside it for its whole length or never touches the box.
			if (RayOrigin < SlabMin || RayOrigin > SlabMax)
			{
				return false;
			}
			continue;
		}

		const double InverseDirection = 1.0 / RayDirection;
		double EnterDistance = (SlabMin - RayOrigin) * InverseDirection;
		double ExitDistance = (SlabMax - RayOrigin) * InverseDirection;
		if (EnterDistance > ExitDistance)
		{
			Swap(EnterDistance, ExitDistance);
		}

		Near = FMath::Max(Near, EnterDistance);
		Far = FMath::Min(Far, ExitDistance);
		if (Near > Far)
		{
			return false;
		}
	}

	OutDistance = Near;
	return true;
}

bool FInstanceMoverMath::RayInstanceIntersection(const FTransform& InstanceWorld, const FBox& LocalBounds, const FVector& Origin,
	const FVector& Direction, double& OutDistance)
{
	const FVector LocalOrigin = InstanceWorld.InverseTransformPosition(Origin);
	const FVector LocalDirection = InstanceWorld.InverseTransformVector(Direction);
	return RayBoxIntersection(LocalBounds, LocalOrigin, LocalDirection, OutDistance);
}

FTransform FInstanceMoverMath::ComputeSurfaceSnapTransform(const FTransform& InstanceWorld, const FBox& LocalBounds,
	const FVector& HitLocation, const FVector& HitNormal, bool bAlignToNormal, bool bRestOnBoundsBottom, double SurfaceOffset)
{
	FTransform Result = InstanceWorld;

	const FVector SurfaceNormal = HitNormal.GetSafeNormal();
	const bool bCanAlign = bAlignToNormal && !SurfaceNormal.IsZero();
	if (bCanAlign)
	{
		const FQuat Tilt = FQuat::FindBetweenNormals(Result.GetRotation().GetUpVector(), SurfaceNormal);
		Result.SetRotation((Tilt * Result.GetRotation()).GetNormalized());
	}

	const FVector Up = bCanAlign ? SurfaceNormal : FVector::UpVector;

	double PivotHeight = 0.0;
	if (bRestOnBoundsBottom && LocalBounds.IsValid)
	{
		if (bCanAlign)
		{
			// Aligned, the instance's local up *is* the surface normal, so the base sits straight down its local Z.
			PivotHeight = -LocalBounds.Min.Z * Result.GetScale3D().Z;
		}
		else
		{
			// Unaligned, a tilted instance's lowest corner is whatever the rotated box reaches down to in world Z.
			const FTransform RotationAndScale(Result.GetRotation(), FVector::ZeroVector, Result.GetScale3D());
			PivotHeight = -LocalBounds.TransformBy(RotationAndScale).Min.Z;
		}
	}

	Result.SetTranslation(HitLocation + Up * (PivotHeight + SurfaceOffset));
	return Result;
}

#pragma endregion
