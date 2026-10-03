// Copyright © 2025 Marcel K. All rights reserved.

#include "Factories/UEFAnimFactory.h"

#include "Animation/AnimSequence.h"
#include "AssetRegistryModule.h"
#include "ComponentReregisterContext.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IMainFrameModule.h"
#include "Misc/FeedbackContext.h"
#include "Misc/ScopedSlowTask.h"
#include "Readers/UEFAnimReader.h"
#include "Widgets/Anim/UEFAnimImportOptions.h"
#include "Widgets/Anim/UEFAnimWidget.h"
#include "Widgets/SWindow.h"

UEFAnimFactory::UEFAnimFactory(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    Formats.Add(TEXT("ueanim; UEANIM Animation File"));
    SupportedClass = UAnimSequence::StaticClass();
    bCreateNew     = false;
    bEditorImport  = true;
    SettingsImporter = CreateDefaultSubobject<UEFAnimImportOptions>(TEXT("Anim Options"));
}

UObject* UEFAnimFactory::FactoryCreateFile(
    UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags,
    const FString& Filename, const TCHAR* Params, FFeedbackContext* Warn,
    bool& bOutOperationCanceled)
{
    UE_LOG(LogTemp, Error, TEXT("UEFAnimFactory IS RUNNING !!!"));
    FScopedSlowTask SlowTask(5, NSLOCTEXT("UEFAnimFactory", "BeginReadUEAnimFile", "Reading UEAnim file"), true);
    if (Warn->GetScopeStack().Num() == 0)
        SlowTask.MakeDialog(true);

    SlowTask.EnterProgressFrame(0);

    UEFAnimReader Data(Filename);
    if (!Data.Read())
        return nullptr;

    // --- Import options dialog ---
    if (!SettingsImporter->bInitialized)
    {
        TSharedPtr<UEFAnimWidget> ImportOptionsWindow;
        TSharedPtr<SWindow>      ParentWindow;

        if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
        {
            IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
            ParentWindow = MainFrame.GetParentWindow();
        }

        TSharedRef<SWindow> Window = SNew(SWindow)
            .Title(FText::FromString(TEXT("Animation Import Options")))
            .SizingRule(ESizingRule::Autosized);

        Window->SetContent(SAssignNew(ImportOptionsWindow, UEFAnimWidget).WidgetWindow(Window));
        SettingsImporter = ImportOptionsWindow.Get()->Stun;
        FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);
        bImport    = ImportOptionsWindow.Get()->ShouldImport();
        bImportAll = ImportOptionsWindow.Get()->ShouldImportAll();
        SettingsImporter->bInitialized = true;
    }

    UAnimSequence* AnimSequence = NewObject<UAnimSequence>(Parent, Name, Flags);
    USkeleton*     Skeleton     = SettingsImporter->Skeleton;

    AnimSequence->SetSkeleton(Skeleton);


    const float SafeFPS = FMath::Max(Data.FramesPerSecond, 1.f);
    AnimSequence->SequenceLength = Data.NumFrames / SafeFPS;
    AnimSequence->RateScale      = 1.f;

    AnimSequence->ImportResampleFramerate = FMath::RoundToInt(SafeFPS);

    FScopedSlowTask ImportTask(Data.Tracks.Num(), FText::FromString(TEXT("Importing UEAnim Animation")));
    ImportTask.MakeDialog(false);

    AnimSequence->ImportResampleFramerate = FMath::RoundToInt(SafeFPS);
    AnimSequence->ImportFileFramerate     = SafeFPS;

    AnimSequence->SetRawNumberOfFrame(Data.NumFrames);
    AnimSequence->SequenceLength = (float)Data.NumFrames / SafeFPS;
    UE_LOG(LogTemp, Warning, TEXT("NumFrames (Data): %d"), Data.NumFrames);
    UE_LOG(LogTemp, Warning, TEXT("SequenceLength: %f"), AnimSequence->SequenceLength);
    UE_LOG(LogTemp, Warning, TEXT("FPS: %d"), AnimSequence->ImportResampleFramerate);

    const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
    int32 NumBones = RefSkeleton.GetNum();
    
    int32 NumTracksImported = 0;

    for (const FTrack& Track : Data.Tracks)
    {
        ImportTask.EnterProgressFrame();

        FName BoneName(Track.TrackName.c_str());
        int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
        if (BoneIndex == INDEX_NONE)
        {
            UE_LOG(LogTemp, Warning, TEXT("Skipping unknown bone: %s"), *BoneName.ToString());
            UE_LOG(LogTemp, Warning, TEXT("Track: %s -> BoneIndex: %d"), *BoneName.ToString(), BoneIndex);
            continue;
        }

        FRawAnimSequenceTrack RawTrack;

        TArray<FVector> FinalPosKeys;
        TArray<FQuat>   FinalRotKeys;
        TArray<FVector> FinalScaleKeys;

        FinalPosKeys.SetNum(Data.NumFrames);
        FinalRotKeys.SetNum(Data.NumFrames);
        FinalScaleKeys.SetNum(Data.NumFrames);

        FVector PrevPos   = FVector::ZeroVector;
        FQuat   PrevRot   = FQuat::Identity;
        FVector PrevScale = FVector::OneVector;

        int32 PosIdx = 0, RotIdx = 0, ScaleIdx = 0;

        for (int32 Frame = 0; Frame < Data.NumFrames; ++Frame)
        {
            if (PosIdx < Track.TrackPosKeys.Num() && Track.TrackPosKeys[PosIdx].Frame == Frame)
                PrevPos = Track.TrackPosKeys[PosIdx++].VectorValue;
            FinalPosKeys[Frame] = PrevPos;

            if (RotIdx < Track.TrackRotKeys.Num() && Track.TrackRotKeys[RotIdx].Frame == Frame)
                PrevRot = Track.TrackRotKeys[RotIdx++].QuatValue;
            FinalRotKeys[Frame] = PrevRot;

            if (ScaleIdx < Track.TrackScaleKeys.Num() && Track.TrackScaleKeys[ScaleIdx].Frame == Frame)
                PrevScale = Track.TrackScaleKeys[ScaleIdx++].VectorValue;
            FinalScaleKeys[Frame] = PrevScale;
        }

        RawTrack.PosKeys   = FinalPosKeys;
        RawTrack.RotKeys   = FinalRotKeys;
        RawTrack.ScaleKeys = FinalScaleKeys;

        AnimSequence->AddNewRawTrack(BoneName, &RawTrack);
        ++NumTracksImported;
    }


    if (Skeleton && Data.Curves.Num() > 0)
    {
        for (const FCurve& Curve : Data.Curves)
        {
            FName CurveName(Curve.CurveName.c_str());

            FSmartName NewSmartName;
            Skeleton->AddSmartNameAndModify(USkeleton::AnimCurveMappingName, CurveName, NewSmartName);

            AnimSequence->RawCurveData.AddCurveData(NewSmartName, AACF_Editable, ERawCurveTrackTypes::RCT_Float);

            FFloatCurve* FloatCurve = static_cast<FFloatCurve*>(
                AnimSequence->RawCurveData.GetCurveData(NewSmartName.UID, ERawCurveTrackTypes::RCT_Float));

            if (FloatCurve)
            {
                for (const FFloatKey& Key : Curve.CurveKeys)
                {
                    FRichCurveKey RichKey;
                    RichKey.Time  = Key.Frame / SafeFPS;
                    RichKey.Value = Key.FloatValue;
                    FloatCurve->FloatCurve.Keys.Add(RichKey);
                }
                FloatCurve->FloatCurve.AutoSetTangents();
            }
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("Tracks in file: %d"), Data.Tracks.Num());
    UE_LOG(LogTemp, Warning, TEXT("Tracks imported: %d"), NumTracksImported);
    UE_LOG(LogTemp, Warning, TEXT("Frames: %d"), Data.NumFrames);


    AnimSequence->MarkRawDataAsModified();

    AnimSequence->PostProcessSequence();

    AnimSequence->MarkPackageDirty();
    AnimSequence->PostEditChange();

    FAssetRegistryModule::AssetCreated(AnimSequence);
    FGlobalComponentReregisterContext RecreateComponents;


    if (!bImportAll)
        SettingsImporter->bInitialized = false;

    return AnimSequence;
}
