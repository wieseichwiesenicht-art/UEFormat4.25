// Copyright © 2025 Marcel K. All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "UEFAnimImportOptions.generated.h"

class USkeleton;

UCLASS(config = Engine, defaultconfig, transient)
class UEFORMAT_API UEFAnimImportOptions : public UObject
{
	GENERATED_BODY()
public:
	UEFAnimImportOptions();

	UPROPERTY(EditAnywhere, Category = "Import Settings")
	USkeleton* Skeleton;    // was TObjectPtr

	bool bInitialized;
};
