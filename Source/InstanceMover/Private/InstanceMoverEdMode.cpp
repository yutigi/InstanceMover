#include "InstanceMoverEdMode.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "ConvexVolume.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FoliageInstancedStaticMeshComponent.h"
#include "Framework/Notifications/NotificationManager.h"
#include "InstanceMoverModule.h"
#include "InstanceMoverSettings.h"
#include "InstanceMoverToolkit.h"
#include "LevelUtils.h"
#include "Math/QuatRotationTranslationMatrix.h"
#include "PrimitiveDrawingUtils.h"
#include "Styling/AppStyle.h"
#include "UnrealWidget.h"
#include "Widgets/Notifications/SNotificationList.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(InstanceMoverEdMode)

#define LOCTEXT_NAMESPACE "InstanceMoverEdMode"

const FEditorModeID UInstanceMoverEdMode::EM_InstanceMover(TEXT("EM_InstanceMover"));

UInstanceMoverEdMode::UInstanceMoverEdMode()
{
	Info = FEditorModeInfo(
		EM_InstanceMover,
		LOCTEXT("ModeName", "Instance Mover"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.InstancedStaticMeshComponent"), TEXT("ClassIcon.InstancedStaticMeshComponent")),
		/*bVisible*/ true,
		/*PriorityOrder*/ 7000);

	SettingsClass = UInstanceMoverSettings::StaticClass();
	NumSelectedComponents = 0;
	bIsClearingActorSelection = false;
}

#pragma region Operations

void UInstanceMoverEdMode::SnapSelectionToSurface()
{
	UWorld* World = GetWorld();
	Selection.RemoveInvalid();
	if (!World || Selection.IsEmpty())
	{
		return;
	}

	const UInstanceMoverSettings* Settings = GetSettings();
	const FScopedTransaction Transaction(LOCTEXT("SnapInstancesToSurface", "Snap Instances to Surface"));
	ModifySelectedComponents();

	int32 NumMissed = 0;
	for (const TPair<UInstancedStaticMeshComponent*, TArray<int32>>& Group : Selection.GroupByComponent())
	{
		UInstancedStaticMeshComponent* Component = Group.Key;
		const FBox LocalBounds = FInstanceMoverInstanceRef::GetLocalMeshBounds(Component);

		// The whole component is ignored rather than just the instance being dropped: a trace cannot exclude a single
		// instance, and without this every instance would land on its own bounds. The cost is that instances of one
		// component cannot be stacked on each other this way.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(InstanceMoverSnap), /*bTraceComplex*/ true);
		Params.AddIgnoredComponent(Component);

		for (const int32 InstanceIndex : Group.Value)
		{
			FTransform InstanceWorld;
			Component->GetInstanceTransform(InstanceIndex, InstanceWorld, /*bWorldSpace*/ true);

			// Start at the top of the instance rather than its pivot, so one already sunk below the ground finds the
			// surface above its pivot instead of whatever lies further beneath.
			FVector TraceStart = InstanceWorld.GetLocation();
			if (LocalBounds.IsValid)
			{
				TraceStart.Z = LocalBounds.TransformBy(InstanceWorld).Max.Z;
			}
			TraceStart.Z += 1.0;
			const FVector TraceEnd = TraceStart - FVector::UpVector * Settings->SnapTraceDistance;

			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, Settings->SnapTraceChannel, Params))
			{
				++NumMissed;
				continue;
			}

			const FTransform Snapped = FInstanceMoverMath::ComputeSurfaceSnapTransform(InstanceWorld, LocalBounds,
				Hit.ImpactPoint, Hit.ImpactNormal, Settings->bAlignToSurfaceNormal, Settings->bRestOnBoundsBottom, Settings->SurfaceOffset);
			Component->UpdateInstanceTransform(InstanceIndex, Snapped, /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ false, /*bTeleport*/ true);
		}

		Component->MarkRenderStateDirty();
	}

	if (NumMissed > 0)
	{
		NotifyUser(FText::Format(LOCTEXT("SnapMissed", "{0} instance(s) found no surface within {1} cm and were left in place."),
			NumMissed, FText::AsNumber(Settings->SnapTraceDistance)));
	}

	RefreshPivot();
}

void UInstanceMoverEdMode::DuplicateSelection()
{
	Selection.RemoveInvalid();
	if (Selection.IsEmpty())
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("DuplicateInstances", "Duplicate Instances"));

	FInstanceMoverSelection Copies;
	for (const TPair<UInstancedStaticMeshComponent*, TArray<int32>>& Group : Selection.GroupByComponent())
	{
		UInstancedStaticMeshComponent* Component = Group.Key;
		Component->Modify();

		TArray<FTransform> Transforms;
		Transforms.Reserve(Group.Value.Num());
		for (const int32 InstanceIndex : Group.Value)
		{
			Component->GetInstanceTransform(InstanceIndex, Transforms.AddDefaulted_GetRef(), /*bWorldSpace*/ true);
		}

		const TArray<int32> NewIndices = Component->AddInstances(Transforms, /*bShouldReturnIndices*/ true, /*bWorldSpace*/ true);

		// A copy without its custom data would lose whatever the material reads from it (tint, variation, ...) and
		// look like a different object from the one it was copied from.
		const int32 NumCustomData = Component->NumCustomDataFloats;
		for (int32 CopyIndex = 0; CopyIndex < NewIndices.Num(); ++CopyIndex)
		{
			if (NumCustomData > 0)
			{
				const int32 SourceOffset = Group.Value[CopyIndex] * NumCustomData;
				if (Component->PerInstanceSMCustomData.IsValidIndex(SourceOffset + NumCustomData - 1))
				{
					const TArray<float> SourceData(Component->PerInstanceSMCustomData.GetData() + SourceOffset, NumCustomData);
					Component->SetCustomData(NewIndices[CopyIndex], SourceData);
				}
			}
			Copies.Add(FInstanceMoverInstanceRef(Component, NewIndices[CopyIndex]));
		}

		Component->MarkRenderStateDirty();
	}

	Selection = MoveTemp(Copies);
	OnSelectionChanged();
}

void UInstanceMoverEdMode::DeleteSelection()
{
	Selection.RemoveInvalid();
	if (Selection.IsEmpty())
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("DeleteInstances", "Delete Instances"));
	for (TPair<UInstancedStaticMeshComponent*, TArray<int32>>& Group : Selection.GroupByComponent())
	{
		// Highest index first, so removing one never shifts an index still waiting to be removed.
		Group.Value.Sort(TGreater<int32>());
		Group.Key->Modify();
		Group.Key->RemoveInstances(Group.Value, /*bInstanceArrayAlreadySortedInReverseOrder*/ true);
	}

	// Removal shifted the indices of every surviving instance after it, so no remaining ref can be trusted.
	Selection.Clear();
	OnSelectionChanged();
}

void UInstanceMoverEdMode::SelectAllInSelectedComponents()
{
	for (const TPair<UInstancedStaticMeshComponent*, TArray<int32>>& Group : Selection.GroupByComponent())
	{
		const int32 NumInstances = Group.Key->GetInstanceCount();
		for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; ++InstanceIndex)
		{
			Selection.Add(FInstanceMoverInstanceRef(Group.Key, InstanceIndex));
		}
	}
	OnSelectionChanged();
}

void UInstanceMoverEdMode::ClearSelection()
{
	if (!Selection.IsEmpty())
	{
		Selection.Clear();
		NumSelectedComponents = 0;
		RefreshPivot();
	}
}

const UInstanceMoverSettings* UInstanceMoverEdMode::GetSettings() const
{
	const UInstanceMoverSettings* Settings = Cast<UInstanceMoverSettings>(SettingsObject);
	return Settings ? Settings : GetDefault<UInstanceMoverSettings>();
}

#pragma endregion

#pragma region UEdMode

void UInstanceMoverEdMode::Enter()
{
	Super::Enter();
	ClearSelection();
}

void UInstanceMoverEdMode::Exit()
{
	TrackingTransaction.Reset();
	ClearSelection();
	Super::Exit();
}

void UInstanceMoverEdMode::PostUndo()
{
	Super::PostUndo();

	// Undoing a duplicate shrinks the instance array under the copies that are still selected.
	Selection.RemoveInvalid();
	NumSelectedComponents = Selection.GroupByComponent().Num();
	RefreshPivot();
}

void UInstanceMoverEdMode::MapChangeNotify()
{
	Super::MapChangeNotify();
	TrackingTransaction.Reset();
	ClearSelection();
}

void UInstanceMoverEdMode::SelectNone()
{
	Super::SelectNone();

	// Reached from Escape / Select None and from a marquee drag starting without Shift, both of which mean "start over".
	if (!bIsClearingActorSelection)
	{
		ClearSelection();
	}
}

void UInstanceMoverEdMode::ActorSelectionChangeNotify()
{
	Super::ActorSelectionChangeNotify();

	// The user picked an actor: hand the gizmo over to it rather than leave two selections fighting for one gizmo.
	if (!bIsClearingActorSelection && GEditor && GEditor->GetSelectedActorCount() > 0)
	{
		ClearSelection();
	}
}

bool UInstanceMoverEdMode::HasCustomViewportFocus() const
{
	return !Selection.IsEmpty();
}

FBox UInstanceMoverEdMode::ComputeCustomViewportFocus() const
{
	FBox Bounds(ForceInit);
	Selection.ComputeBounds(Bounds);
	return Bounds;
}

EEditAction::Type UInstanceMoverEdMode::GetActionEditDuplicate()
{
	return Selection.IsEmpty() ? EEditAction::Skip : EEditAction::Process;
}

EEditAction::Type UInstanceMoverEdMode::GetActionEditDelete()
{
	return Selection.IsEmpty() ? EEditAction::Skip : EEditAction::Process;
}

bool UInstanceMoverEdMode::ProcessEditDuplicate()
{
	if (Selection.IsEmpty())
	{
		return false;
	}
	DuplicateSelection();
	return true;
}

bool UInstanceMoverEdMode::ProcessEditDelete()
{
	if (Selection.IsEmpty())
	{
		return Super::ProcessEditDelete();
	}
	DeleteSelection();
	return true;
}

void UInstanceMoverEdMode::CreateToolkit()
{
	Toolkit = MakeShared<FInstanceMoverToolkit>();
}

#pragma endregion

#pragma region Viewport

bool UInstanceMoverEdMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	if (Click.GetKey() != EKeys::LeftMouseButton)
	{
		return Super::HandleClick(InViewportClient, HitProxy, Click);
	}

	// A click that lands on the gizmo without dragging it must not read as "clicked empty space".
	if (HitProxy && HitProxy->IsA(HWidgetAxis::StaticGetType()))
	{
		return false;
	}

	FInstanceMoverInstanceRef Picked;
	if (PickInstance(HitProxy, Click.GetOrigin(), Click.GetDirection(), Picked))
	{
		ApplyClickSelection(Picked, Click.IsControlDown(), Click.IsShiftDown());
		return true;
	}

	// Not an instance: leave the click to the level editor (actor selection, or deselecting on empty space).
	if (!Click.IsControlDown() && !Click.IsShiftDown())
	{
		ClearSelection();
	}
	return Super::HandleClick(InViewportClient, HitProxy, Click);
}

bool UInstanceMoverEdMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (Event == IE_Pressed && Key == EKeys::Escape && !Selection.IsEmpty())
	{
		ClearSelection();
		return true;
	}
	return Super::InputKey(ViewportClient, Viewport, Key, Event);
}

bool UInstanceMoverEdMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	Selection.RemoveInvalid();
	if (Selection.IsEmpty() || GetCurrentWidgetAxis() == EAxisList::None)
	{
		return Super::StartTracking(InViewportClient, InViewport);
	}

	const bool bDuplicate = InViewportClient->IsAltPressed();
	TrackingTransaction = MakeUnique<FScopedTransaction>(bDuplicate
		? LOCTEXT("DuplicateMoveInstances", "Duplicate and Move Instances")
		: LOCTEXT("MoveInstances", "Move Instances"));

	// Alt-drag leaves the originals where they were and moves the copies, as Alt-drag does for actors. Copying inside
	// the drag's transaction is what makes the copy and the move a single undo step.
	if (bDuplicate)
	{
		DuplicateSelection();
	}

	ModifySelectedComponents();
	return true;
}

bool UInstanceMoverEdMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	if (!TrackingTransaction.IsValid())
	{
		return Super::EndTracking(InViewportClient, InViewport);
	}

	TrackingTransaction.Reset();
	RefreshPivot();
	return true;
}

bool UInstanceMoverEdMode::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	if (Selection.IsEmpty() || GetCurrentWidgetAxis() == EAxisList::None)
	{
		return Super::InputDelta(InViewportClient, InViewport, InDrag, InRot, InScale);
	}

	// A delta that did not come from a tracked drag still has to be undoable on its own.
	TUniquePtr<FScopedTransaction> OneShotTransaction;
	if (!TrackingTransaction.IsValid())
	{
		OneShotTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("MoveInstances", "Move Instances"));
		ModifySelectedComponents();
	}

	// One pivot for the whole selection, read before anything moves, so every instance orbits the same point.
	const FVector Pivot = ComputePivot();
	for (const TPair<UInstancedStaticMeshComponent*, TArray<int32>>& Group : Selection.GroupByComponent())
	{
		UInstancedStaticMeshComponent* Component = Group.Key;
		for (const int32 InstanceIndex : Group.Value)
		{
			FTransform InstanceWorld;
			Component->GetInstanceTransform(InstanceIndex, InstanceWorld, /*bWorldSpace*/ true);
			Component->UpdateInstanceTransform(InstanceIndex,
				FInstanceMoverMath::ApplyGizmoDelta(InstanceWorld, Pivot, InDrag, InRot, InScale),
				/*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ false, /*bTeleport*/ true);
		}

		// Once per component rather than per instance: a drag over thousands of instances would otherwise rebuild the
		// render state thousands of times a frame.
		Component->MarkRenderStateDirty();
	}

	RefreshPivot();
	return true;
}

void UInstanceMoverEdMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	Super::Render(View, Viewport, PDI);

	const UInstanceMoverSettings* Settings = GetSettings();
	for (const FInstanceMoverInstanceRef& Ref : Selection.GetInstances())
	{
		FTransform InstanceWorld;
		if (!Ref.GetWorldTransform(InstanceWorld))
		{
			continue;
		}

		const FBox LocalBounds = FInstanceMoverInstanceRef::GetLocalMeshBounds(Ref.Component.Get());
		if (LocalBounds.IsValid)
		{
			DrawWireBox(PDI, InstanceWorld.ToMatrixWithScale(), LocalBounds, Settings->SelectionColor, SDPG_Foreground,
				Settings->SelectionLineThickness);
		}
	}
}

bool UInstanceMoverEdMode::AllowsViewportDragTool() const
{
	return true;
}

#pragma endregion

#pragma region Widget

bool UInstanceMoverEdMode::AllowWidgetMove()
{
	return true;
}

bool UInstanceMoverEdMode::UsesTransformWidget() const
{
	return true;
}

bool UInstanceMoverEdMode::UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const
{
	return CheckMode == UE::Widget::WM_Translate
		|| CheckMode == UE::Widget::WM_Rotate
		|| CheckMode == UE::Widget::WM_Scale
		|| CheckMode == UE::Widget::WM_TranslateRotateZ;
}

bool UInstanceMoverEdMode::ShouldDrawWidget() const
{
	// With nothing of ours selected the gizmo belongs to whatever actor is selected, exactly as outside the mode.
	return Selection.IsEmpty() ? Super::ShouldDrawWidget() : true;
}

FVector UInstanceMoverEdMode::GetWidgetLocation() const
{
	return Selection.IsEmpty() ? Super::GetWidgetLocation() : ComputePivot();
}

bool UInstanceMoverEdMode::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	if (Selection.IsEmpty())
	{
		return Super::GetCustomDrawingCoordinateSystem(InMatrix, InData);
	}
	return GetModeManager()->GetCoordSystem() == COORD_Local && GetLastSelectedRotationMatrix(InMatrix);
}

bool UInstanceMoverEdMode::GetCustomInputCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	if (Selection.IsEmpty())
	{
		return Super::GetCustomInputCoordinateSystem(InMatrix, InData);
	}
	return GetModeManager()->GetCoordSystem() == COORD_Local && GetLastSelectedRotationMatrix(InMatrix);
}

#pragma endregion

#pragma region ILegacyEdModeSelectInterface

bool UInstanceMoverEdMode::BoxSelect(FBox& InBox, bool InSelect)
{
	return SelectInstancesWhere([&InBox](const FVector& Location) { return InBox.IsInside(Location); }, InSelect);
}

bool UInstanceMoverEdMode::FrustumSelect(const FConvexVolume& InFrustum, FEditorViewportClient* InViewportClient, bool InSelect)
{
	return SelectInstancesWhere([&InFrustum](const FVector& Location) { return InFrustum.IntersectPoint(Location); }, InSelect);
}

#pragma endregion

#pragma region Picking

bool UInstanceMoverEdMode::PickInstance(HHitProxy* HitProxy, const FVector& RayOrigin, const FVector& RayDirection, FInstanceMoverInstanceRef& OutRef) const
{
	if (!HitProxy)
	{
		return false;
	}

	// Only components with bHasPerInstanceHitProxies render these, but when they exist they are pixel-exact.
	if (HitProxy->IsA(HInstancedStaticMeshInstance::StaticGetType()))
	{
		const HInstancedStaticMeshInstance* InstanceProxy = static_cast<HInstancedStaticMeshInstance*>(HitProxy);
		UInstancedStaticMeshComponent* Component = InstanceProxy->Component;
		if (IsComponentEditable(Component, /*bNotifyUser*/ true) && Component->IsValidInstance(InstanceProxy->InstanceIndex))
		{
			OutRef = FInstanceMoverInstanceRef(Component, InstanceProxy->InstanceIndex);
			return true;
		}
		return false;
	}

	// Everything else reports the whole component, so the instance has to be recovered from the click ray.
	if (HitProxy->IsA(HActor::StaticGetType()))
	{
		const HActor* ActorProxy = static_cast<HActor*>(HitProxy);

		TArray<UInstancedStaticMeshComponent*, TInlineAllocator<4>> Candidates;
		if (UInstancedStaticMeshComponent* HitComponent = Cast<UInstancedStaticMeshComponent>(const_cast<UPrimitiveComponent*>(ActorProxy->PrimComponent.Get())))
		{
			Candidates.Add(HitComponent);
		}
		else if (ActorProxy->Actor)
		{
			ActorProxy->Actor->GetComponents(Candidates);
		}

		Candidates.RemoveAll([this](UInstancedStaticMeshComponent* Component) { return !IsComponentEditable(Component, /*bNotifyUser*/ true); });
		return PickInstanceAlongRay(Candidates, RayOrigin, RayDirection, OutRef);
	}

	return false;
}

bool UInstanceMoverEdMode::PickInstanceAlongRay(TConstArrayView<UInstancedStaticMeshComponent*> Candidates, const FVector& RayOrigin,
	const FVector& RayDirection, FInstanceMoverInstanceRef& OutRef) const
{
	if (Candidates.IsEmpty())
	{
		return false;
	}

	if (UWorld* World = GetWorld())
	{
		FHitResult Hit;
		const FCollisionQueryParams Params(SCENE_QUERY_STAT(InstanceMoverPick), /*bTraceComplex*/ true);
		if (World->LineTraceSingleByChannel(Hit, RayOrigin, RayOrigin + RayDirection * UE_OLD_WORLD_MAX, ECC_Visibility, Params))
		{
			UInstancedStaticMeshComponent* HitComponent = Cast<UInstancedStaticMeshComponent>(Hit.GetComponent());
			if (HitComponent && Candidates.Contains(HitComponent) && HitComponent->IsValidInstance(Hit.Item))
			{
				OutRef = FInstanceMoverInstanceRef(HitComponent, Hit.Item);
				return true;
			}
		}
	}

	double BestDistance = TNumericLimits<double>::Max();
	bool bFound = false;
	for (UInstancedStaticMeshComponent* Component : Candidates)
	{
		const FBox LocalBounds = FInstanceMoverInstanceRef::GetLocalMeshBounds(Component);
		if (!LocalBounds.IsValid)
		{
			continue;
		}

		const int32 NumInstances = Component->GetInstanceCount();
		for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; ++InstanceIndex)
		{
			FTransform InstanceWorld;
			double Distance = 0.0;
			if (Component->GetInstanceTransform(InstanceIndex, InstanceWorld, /*bWorldSpace*/ true)
				&& FInstanceMoverMath::RayInstanceIntersection(InstanceWorld, LocalBounds, RayOrigin, RayDirection, Distance)
				&& Distance < BestDistance)
			{
				BestDistance = Distance;
				OutRef = FInstanceMoverInstanceRef(Component, InstanceIndex);
				bFound = true;
			}
		}
	}
	return bFound;
}

bool UInstanceMoverEdMode::IsComponentEditable(UInstancedStaticMeshComponent* Component, bool bNotifyUser) const
{
	if (!IsValid(Component) || Component->GetWorld() != GetWorld())
	{
		return false;
	}

	FText Refusal;
	if (Component->IsA<UFoliageInstancedStaticMeshComponent>())
	{
		Refusal = LOCTEXT("RefuseFoliage", "Foliage instances are edited in Foliage mode.");
	}
	else if (Component->CreationMethod == EComponentCreationMethod::UserConstructionScript)
	{
		Refusal = FText::Format(LOCTEXT("RefuseConstructionScript",
			"'{0}' is built by its Blueprint's construction script and would undo any move the next time it reruns."),
			FText::FromString(Component->GetName()));
	}
	else if (AActor* OwningActor = Component->GetOwner())
	{
		if (OwningActor->IsLockLocation())
		{
			Refusal = FText::Format(LOCTEXT("RefuseLockedActor", "'{0}' has its location locked."), FText::FromString(OwningActor->GetActorNameOrLabel()));
		}
		else if (FLevelUtils::IsLevelLocked(OwningActor))
		{
			Refusal = FText::Format(LOCTEXT("RefuseLockedLevel", "'{0}' is in a locked level."), FText::FromString(OwningActor->GetActorNameOrLabel()));
		}
	}

	if (Refusal.IsEmpty())
	{
		return true;
	}

	if (bNotifyUser)
	{
		NotifyUser(Refusal);
	}
	return false;
}

void UInstanceMoverEdMode::CollectEditableComponents(TArray<UInstancedStaticMeshComponent*>& OutComponents) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor->IsHiddenEd() || Actor->IsTemporarilyHiddenInEditor() || !FLevelUtils::IsLevelVisible(Actor->GetLevel()))
		{
			continue;
		}

		TInlineComponentArray<UInstancedStaticMeshComponent*> Components(Actor);
		for (UInstancedStaticMeshComponent* Component : Components)
		{
			if (Component->IsVisibleInEditor() && IsComponentEditable(Component, /*bNotifyUser*/ false))
			{
				OutComponents.Add(Component);
			}
		}
	}
}

#pragma endregion

#pragma region SelectionHelpers

bool UInstanceMoverEdMode::SelectInstancesWhere(TFunctionRef<bool(const FVector&)> Predicate, bool bSelect)
{
	// No clearing here: the drag tool already called SelectNone() unless Shift was held, which is what makes a
	// Shift-marquee additive.
	TArray<UInstancedStaticMeshComponent*> Components;
	CollectEditableComponents(Components);

	bool bAnyMatched = false;
	for (UInstancedStaticMeshComponent* Component : Components)
	{
		const int32 NumInstances = Component->GetInstanceCount();
		for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; ++InstanceIndex)
		{
			FTransform InstanceWorld;
			if (!Component->GetInstanceTransform(InstanceIndex, InstanceWorld, /*bWorldSpace*/ true) || !Predicate(InstanceWorld.GetLocation()))
			{
				continue;
			}

			const FInstanceMoverInstanceRef Ref(Component, InstanceIndex);
			if (bSelect)
			{
				Selection.Add(Ref);
			}
			else
			{
				Selection.Remove(Ref);
			}
			bAnyMatched = true;
		}
	}

	// Returning false when nothing matched lets the marquee fall through to ordinary actor selection.
	if (bAnyMatched)
	{
		OnSelectionChanged();
	}
	return bAnyMatched;
}

void UInstanceMoverEdMode::ApplyClickSelection(const FInstanceMoverInstanceRef& Picked, bool bToggle, bool bAdd)
{
	if (bToggle)
	{
		Selection.Toggle(Picked);
	}
	else if (bAdd)
	{
		Selection.Add(Picked);
	}
	else
	{
		Selection.Clear();
		Selection.Add(Picked);
	}
	OnSelectionChanged();
}

void UInstanceMoverEdMode::OnSelectionChanged()
{
	if (!Selection.IsEmpty() && GEditor && GEditor->GetSelectedActorCount() > 0)
	{
		TGuardValue<bool> ClearingGuard(bIsClearingActorSelection, true);
		GEditor->SelectNone(/*bNoteSelectionChange*/ true, /*bDeselectBSPSurfs*/ true, /*WarnAboutManyActors*/ false);
	}

	NumSelectedComponents = Selection.GroupByComponent().Num();
	RefreshPivot();
}

void UInstanceMoverEdMode::RefreshPivot()
{
	if (!Selection.IsEmpty())
	{
		GetModeManager()->SetPivotLocation(ComputePivot(), /*bIncGridBase*/ false);
	}

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

FVector UInstanceMoverEdMode::ComputePivot() const
{
	if (GetSettings()->PivotMode == EInstanceMoverPivotMode::LastSelected)
	{
		FTransform LastTransform;
		const FInstanceMoverInstanceRef* LastSelected = Selection.GetLastSelected();
		if (LastSelected && LastSelected->GetWorldTransform(LastTransform))
		{
			return LastTransform.GetLocation();
		}
	}

	FVector Centroid = FVector::ZeroVector;
	Selection.ComputeCentroid(Centroid);
	return Centroid;
}

bool UInstanceMoverEdMode::GetLastSelectedRotationMatrix(FMatrix& OutMatrix) const
{
	FTransform LastTransform;
	const FInstanceMoverInstanceRef* LastSelected = Selection.GetLastSelected();
	if (!LastSelected || !LastSelected->GetWorldTransform(LastTransform))
	{
		return false;
	}

	OutMatrix = FQuatRotationMatrix(LastTransform.GetRotation());
	return true;
}

void UInstanceMoverEdMode::ModifySelectedComponents() const
{
	for (const TPair<UInstancedStaticMeshComponent*, TArray<int32>>& Group : Selection.GroupByComponent())
	{
		Group.Key->Modify();
	}
}

void UInstanceMoverEdMode::NotifyUser(const FText& Message) const
{
	UE_LOG(LogInstanceMover, Warning, TEXT("%s"), *Message.ToString());

	FNotificationInfo Notification(Message);
	Notification.ExpireDuration = 4.0f;
	FSlateNotificationManager::Get().AddNotification(Notification);
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
