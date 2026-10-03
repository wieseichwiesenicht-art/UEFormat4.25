// Copyright © 2025 Marcel K. All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "UEFAnimImportOptions.h"
#include "Widgets/SCompoundWidget.h"

class IDetailsView;

enum class UEFAnimImportOptionDlgResponse : uint8
{
	Import,
	ImportAll,
	Cancel
};

class UEFORMAT_API UEFAnimWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(UEFAnimWidget) : _WidgetWindow() {}
	SLATE_ARGUMENT(TSharedPtr<SWindow>, WidgetWindow)
	SLATE_END_ARGS()

	UEFAnimWidget() : UserDlgResponse(UEFAnimImportOptionDlgResponse::Cancel) {}

	void Construct(const FArguments& InArgs);

	TSharedPtr<IDetailsView> PropertyView;

	mutable UEFAnimImportOptions* Stun;

	bool ShouldImport();
	bool ShouldImportAll();

	FReply OnImportAll();
	FReply OnImport();
	FReply OnCancel();

private:
	UEFAnimImportOptionDlgResponse UserDlgResponse;
	FReply HandleImport();
	TWeakPtr<SWindow> WidgetWindow;
};
