#pragma once

#include "CoreMinimal.h"
#include "Toolkits/BaseToolkit.h"

class UInstanceMoverEdMode;

/** The Instance Mover mode panel: selection summary, one-click operations, and the per-user settings. */
class FInstanceMoverToolkit : public FModeToolkit
{
public:
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode) override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual TSharedPtr<SWidget> GetInlineContent() const override;

private:
	UInstanceMoverEdMode* GetInstanceMoverMode() const;

	FText GetSelectionSummary() const;
	bool HasSelection() const;

	TSharedRef<SWidget> MakeActionButton(const FText& Label, const FText& ToolTip, TFunction<void(UInstanceMoverEdMode&)> Action);

	TSharedPtr<SWidget> PanelContent;
};
