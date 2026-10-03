// Copyright © 2025 Marcel K. All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "Readers/UEFModelReader.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "UEFModelFactory.generated.h"

UCLASS(hidecategories=Object)
class UEFORMAT_API UEFModelFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

	virtual UObject* FactoryCreateFile(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, const FString& Filename, const TCHAR* Params, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;

private:
	UStaticMesh*   CreateStaticMesh  (TArray<FLODData>& LODData, UObject* Parent, FName Name, EObjectFlags Flags);
	USkeletalMesh* CreateSkeletalMesh(TArray<FLODData>& LODData, FSkeletonData& SkeletonData, UObject* Parent, FName Name, EObjectFlags Flags);
	USkeleton*     CreateSkeleton    (const FString& Name, UObject* Parent, EObjectFlags Flags, FSkeletonData& Data, FReferenceSkeleton& OutRefSkeleton);
};
