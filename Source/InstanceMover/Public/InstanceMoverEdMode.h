#pragma once

#include "CoreMinimal.h"
#include "InstanceMoverTypes.h"
#include "ScopedTransaction.h"
#include "Tools/LegacyEdModeInterfaces.h"
#include "Tools/LegacyEdModeWidgetHelpers.h"

#include "InstanceMoverEdMode.generated.h"

class UInstanceMoverSettings;
class UInstancedStaticMeshComponent;

/**
 * Editor mode for moving individual instances of Instanced / Hierarchical Instanced Static Mesh components.
 *
 * Click an instance to select it (Shift adds, Ctrl toggles), marquee-drag to select many, then drive them with the
 * ordinary transform gizmo — W/E/R, world/local space and grid snapping all behave as they do for actors. Alt-drag
 * duplicates, Ctrl+D duplicates in place, Delete removes, and every edit is one undo step.
 *
 * Built on UBaseLegacyWidgetEdMode because the level viewport's transform gizmo is still driven through the legacy
 * widget interface (InputDelta / StartTracking / GetWidgetLocation); the ITF gizmos would mean re-implementing the
 * snapping and coordinate-space handling the legacy path already gets right.
 *
 * Out of scope on purpose:
 *  - Foliage instances: Foliage mode owns them and keeps its own spatial hash, which an outside edit would desync.
 *  - Components created by a Blueprint construction script: the script rebuilds them on every change, so a moved
 *    instance would snap back the next time the actor is touched.
 */
UCLASS()
class INSTANCEMOVER_API UInstanceMoverEdMode : public UBaseLegacyWidgetEdMode, public ILegacyEdModeSelectInterface
{
	GENERATED_BODY()

public:
	static const FEditorModeID EM_InstanceMover;

	UInstanceMoverEdMode();

#pragma region Operations

	/** Drops every selected instance onto the surface below it, per the Snap to Surface settings. */
	void SnapSelectionToSurface();

	/** Copies the selected instances in place (custom data included) and selects the copies. */
	void DuplicateSelection();

	void DeleteSelection();

	/** Grows the selection to every instance of each component that already has one selected. */
	void SelectAllInSelectedComponents();

	void ClearSelection();

	FORCEINLINE const FInstanceMoverInstanceRef* GetLastSelectedInstance() const { return Selection.GetLastSelected(); }
	FORCEINLINE int32 GetNumSelectedInstances() const { return Selection.Num(); }
	FORCEINLINE int32 GetNumSelectedComponents() const { return NumSelectedComponents; }

	const UInstanceMoverSettings* GetSettings() const;

#pragma endregion

#pragma region UEdMode

	virtual void Enter() override;
	virtual void Exit() override;
	virtual void PostUndo() override;
	virtual void MapChangeNotify() override;
	virtual void SelectNone() override;
	virtual void ActorSelectionChangeNotify() override;
	virtual bool HasCustomViewportFocus() const override;
	virtual FBox ComputeCustomViewportFocus() const override;

	virtual EEditAction::Type GetActionEditDuplicate() override;
	virtual EEditAction::Type GetActionEditDelete() override;
	virtual bool ProcessEditDuplicate() override;
	virtual bool ProcessEditDelete() override;

#pragma endregion

#pragma region Viewport

	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click) override;
	virtual bool InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;
	virtual bool StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale) override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual bool AllowsViewportDragTool() const override;

#pragma endregion

#pragma region Widget

	virtual bool AllowWidgetMove() override;
	virtual bool UsesTransformWidget() const override;
	virtual bool UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const override;
	virtual bool ShouldDrawWidget() const override;
	virtual FVector GetWidgetLocation() const override;
	virtual bool GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData) override;
	virtual bool GetCustomInputCoordinateSystem(FMatrix& InMatrix, void* InData) override;

#pragma endregion

#pragma region ILegacyEdModeSelectInterface

	virtual bool BoxSelect(FBox& InBox, bool InSelect = true) override;
	virtual bool FrustumSelect(const FConvexVolume& InFrustum, FEditorViewportClient* InViewportClient, bool InSelect = true) override;

#pragma endregion

protected:
	virtual void CreateToolkit() override;

private:
	/** Resolves a viewport click to one instance: exact via a per-instance hit proxy, otherwise along the click ray. */
	bool PickInstance(HHitProxy* HitProxy, const FVector& RayOrigin, const FVector& RayDirection, FInstanceMoverInstanceRef& OutRef) const;

	/**
	 * Nearest instance of Candidates along a ray. Collision first, because an ISM trace reports the exact instance it
	 * hit; mesh bounds second, so components with collision disabled — common on set dressing — stay pickable.
	 */
	bool PickInstanceAlongRay(TConstArrayView<UInstancedStaticMeshComponent*> Candidates, const FVector& RayOrigin,
		const FVector& RayDirection, FInstanceMoverInstanceRef& OutRef) const;

	/** Whether this mode may edit the component. With bNotifyUser, a refusal says why in a toast. */
	bool IsComponentEditable(UInstancedStaticMeshComponent* Component, bool bNotifyUser) const;

	/** Every editable, visible instanced component in the edited world — the candidate set for marquee selection. */
	void CollectEditableComponents(TArray<UInstancedStaticMeshComponent*>& OutComponents) const;

	/** Adds or removes every editable instance whose world location passes Predicate. @return true if any matched. */
	bool SelectInstancesWhere(TFunctionRef<bool(const FVector&)> Predicate, bool bSelect);

	void ApplyClickSelection(const FInstanceMoverInstanceRef& Picked, bool bToggle, bool bAdd);

	/** Keeps the actor selection and the pivot in step with the instance selection after it changes. */
	void OnSelectionChanged();

	/** Re-reads the pivot and redraws, for after the selected instances move. */
	void RefreshPivot();

	FVector ComputePivot() const;

	/** The local-space gizmo axes: the last-selected instance's rotation. */
	bool GetLastSelectedRotationMatrix(FMatrix& OutMatrix) const;

	void ModifySelectedComponents() const;

	void NotifyUser(const FText& Message) const;

	FInstanceMoverSelection Selection;

	/**
	 * Cached on each selection change rather than counted on demand: the mode panel polls it every Slate paint, and
	 * counting means bucketing the whole selection.
	 */
	int32 NumSelectedComponents;

	/** Open from StartTracking to EndTracking, so one whole gizmo drag — however many frames — is one undo step. */
	TUniquePtr<FScopedTransaction> TrackingTransaction;

	/**
	 * Set while this mode is the one clearing the actor selection. Picking an instance deselects actors so two gizmos
	 * never compete, and that deselection comes straight back through SelectNone / ActorSelectionChangeNotify — which
	 * would otherwise clear the instance that was just picked.
	 */
	bool bIsClearingActorSelection;
};
