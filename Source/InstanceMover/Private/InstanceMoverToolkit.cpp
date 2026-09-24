#include "InstanceMoverToolkit.h"

#include "IDetailsView.h"
#include "InstanceMoverEdMode.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "InstanceMoverToolkit"

#pragma region FModeToolkit

void FInstanceMoverToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode)
{
	FModeToolkit::Init(InitToolkitHost, InOwningMode);

	const FMargin SectionPadding(8.0f, 6.0f);

	PanelContent = SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(SectionPadding)
		[
			SNew(STextBlock)
			.Text(this, &FInstanceMoverToolkit::GetSelectionSummary)
			.Font(FAppStyle::GetFontStyle(TEXT("BoldFont")))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(SectionPadding)
		[
			SNew(SUniformGridPanel)
			.SlotPadding(FMargin(2.0f))
			+ SUniformGridPanel::Slot(0, 0)
			[
				MakeActionButton(LOCTEXT("SnapToSurface", "Snap to Surface"),
					LOCTEXT("SnapToSurfaceTip", "Drop every selected instance onto the surface below it."),
					[](UInstanceMoverEdMode& Mode) { Mode.SnapSelectionToSurface(); })
			]
			+ SUniformGridPanel::Slot(1, 0)
			[
				MakeActionButton(LOCTEXT("Duplicate", "Duplicate"),
					LOCTEXT("DuplicateTip", "Copy the selected instances in place and select the copies (Ctrl+D)."),
					[](UInstanceMoverEdMode& Mode) { Mode.DuplicateSelection(); })
			]
			+ SUniformGridPanel::Slot(0, 1)
			[
				MakeActionButton(LOCTEXT("SelectAll", "Select All in Component"),
					LOCTEXT("SelectAllTip", "Select every instance of each component that already has one selected."),
					[](UInstanceMoverEdMode& Mode) { Mode.SelectAllInSelectedComponents(); })
			]
			+ SUniformGridPanel::Slot(1, 1)
			[
				MakeActionButton(LOCTEXT("Delete", "Delete"),
					LOCTEXT("DeleteTip", "Remove the selected instances (Delete)."),
					[](UInstanceMoverEdMode& Mode) { Mode.DeleteSelection(); })
			]
			+ SUniformGridPanel::Slot(0, 2)
			[
				MakeActionButton(LOCTEXT("ClearSelection", "Clear Selection"),
					LOCTEXT("ClearSelectionTip", "Deselect all instances (Esc)."),
					[](UInstanceMoverEdMode& Mode) { Mode.ClearSelection(); })
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(SectionPadding)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("Help",
				"Click an instance to select it. Shift+click adds, Ctrl+click toggles, drag a marquee to select many.\n"
				"Move / rotate / scale with the gizmo (W / E / R). Alt+drag duplicates. F frames the selection.\n"
				"Foliage and construction-script components are not editable here."))
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			ModeDetailsView.ToSharedRef()
		];
}

FName FInstanceMoverToolkit::GetToolkitFName() const
{
	return FName(TEXT("InstanceMoverMode"));
}

FText FInstanceMoverToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "Instance Mover");
}

TSharedPtr<SWidget> FInstanceMoverToolkit::GetInlineContent() const
{
	return PanelContent;
}

#pragma endregion

#pragma region Panel

UInstanceMoverEdMode* FInstanceMoverToolkit::GetInstanceMoverMode() const
{
	return Cast<UInstanceMoverEdMode>(GetScriptableEditorMode().Get());
}

FText FInstanceMoverToolkit::GetSelectionSummary() const
{
	const UInstanceMoverEdMode* Mode = GetInstanceMoverMode();
	if (!Mode || Mode->GetNumSelectedInstances() == 0)
	{
		return LOCTEXT("NothingSelected", "No instances selected");
	}

	return FText::Format(LOCTEXT("SelectionSummary", "{0} {0}|plural(one=instance,other=instances) selected in {1} {1}|plural(one=component,other=components)"),
		Mode->GetNumSelectedInstances(), Mode->GetNumSelectedComponents());
}

bool FInstanceMoverToolkit::HasSelection() const
{
	const UInstanceMoverEdMode* Mode = GetInstanceMoverMode();
	return Mode && Mode->GetNumSelectedInstances() > 0;
}

TSharedRef<SWidget> FInstanceMoverToolkit::MakeActionButton(const FText& Label, const FText& ToolTip, TFunction<void(UInstanceMoverEdMode&)> Action)
{
	return SNew(SButton)
		.HAlign(HAlign_Center)
		.Text(Label)
		.ToolTipText(ToolTip)
		.IsEnabled(this, &FInstanceMoverToolkit::HasSelection)
		.OnClicked_Lambda([this, Action = MoveTemp(Action)]()
		{
			if (UInstanceMoverEdMode* Mode = GetInstanceMoverMode())
			{
				Action(*Mode);
			}
			return FReply::Handled();
		});
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
