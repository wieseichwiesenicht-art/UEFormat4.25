// Copyright © 2025 Marcel K. All rights reserved.

#include "Factories/UEFModelFactory.h"
#include "SkelImport.h"
#include "StaticMeshResources.h"
#include "AssetRegistryModule.h"           
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/StaticMesh.h"
#include "MeshUtilities.h"                 
#include "RawMesh.h"                       
#include "Animation/MorphTarget.h"
#include "Rendering/SkeletalMeshModel.h"   
#include "SkeletalMeshTypes.h"
#include "Runtime/Launch/Resources/Version.h"


const float ImportScale = 1.0f;


UEFModelFactory::UEFModelFactory(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    Formats.Add(TEXT("uemodel; UEMODEL Mesh File"));
    SupportedClass = UObject::StaticClass();
    bCreateNew     = false;
    bEditorImport  = true;
}


UObject* UEFModelFactory::FactoryCreateFile(
    UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags,
    const FString& Filename, const TCHAR* Params, FFeedbackContext* Warn,
    bool& bOutOperationCanceled)
{
    UEFModelReader Data(Filename);
    if (!Data.Read() || Data.LODs.Num() == 0)
        return nullptr;

    if (Data.Skeleton.Bones.Num() > 0)
    {
        USkeletalMesh* SkeletalMesh = CreateSkeletalMesh(Data.LODs, Data.Skeleton, Parent, Name, Flags);
        if (!SkeletalMesh) return nullptr;
        SkeletalMesh->PostEditChange();
        FAssetRegistryModule::AssetCreated(SkeletalMesh);
        return SkeletalMesh;
    }
    else
    {
        UStaticMesh* StaticMesh = CreateStaticMesh(Data.LODs, Parent, Name, Flags);
        if (!StaticMesh) return nullptr;
        StaticMesh->PostEditChange();
        FAssetRegistryModule::AssetCreated(StaticMesh);
        return StaticMesh;
    }
}

UStaticMesh* UEFModelFactory::CreateStaticMesh(TArray<FLODData>& LODData, UObject* Parent, FName Name, EObjectFlags Flags)
{
    UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Parent->GetOutermost(), Name, Flags);
    StaticMesh->PreEditChange(nullptr);

    for (int32 LodIndex = 0; LodIndex < LODData.Num(); ++LodIndex)
    {
        FLODData& Data = LODData[LodIndex];

#if ENGINE_MINOR_VERSION >= 24
        FStaticMeshSourceModel& SourceModel = StaticMesh->AddSourceModel();
#else
        new(StaticMesh->SourceModels) FStaticMeshSourceModel();
        FStaticMeshSourceModel& SourceModel = StaticMesh->SourceModels[LodIndex];
#endif
        SourceModel.BuildSettings.bRecomputeNormals    = false;
        SourceModel.BuildSettings.bRecomputeTangents   = false;
        SourceModel.BuildSettings.bRemoveDegenerates   = false;
        SourceModel.BuildSettings.bGenerateLightmapUVs = false;

        FRawMesh RawMesh;

        RawMesh.VertexPositions.Reserve(Data.Vertices.Num());
        for (const FVector& V : Data.Vertices)
            RawMesh.VertexPositions.Add(V * ImportScale);

        const int32 NumUVChannels = FMath::Max(1, Data.TextureCoordinates.Num());


        const int32 NumWedges = Data.Indices.Num();
        RawMesh.WedgeIndices.Reserve(NumWedges);
        RawMesh.WedgeTangentX.Reserve(NumWedges);
        RawMesh.WedgeTangentY.Reserve(NumWedges);
        RawMesh.WedgeTangentZ.Reserve(NumWedges);
        for (int32 ch = 0; ch < NumUVChannels; ++ch)
            RawMesh.WedgeTexCoords[ch].Reserve(NumWedges);
        if (Data.VertexColors.Num() > 0)
            RawMesh.WedgeColors.Reserve(NumWedges);

        for (int32 i = 0; i < NumWedges; i++)
        {
            const int32 VIdx = Data.Indices[i];
            RawMesh.WedgeIndices.Add(VIdx);

            FVector Normal   = FVector::ZeroVector;
            FVector Tangent  = FVector::ZeroVector;
            float   BinSign  = 1.f;
            if (Data.Normals.Num() > 0 && VIdx < Data.Normals.Num())
            {
                const FVector4& N4 = Data.Normals[VIdx];
                BinSign = N4.X;
                Normal  = FVector(N4.Y, N4.Z, N4.W);
            }
            if (Data.Tangents.Num() > 0 && VIdx < Data.Tangents.Num())
                Tangent = Data.Tangents[VIdx];

            FVector Binormal = FVector::CrossProduct(Normal, Tangent) * BinSign;

            RawMesh.WedgeTangentX.Add(Tangent);
            RawMesh.WedgeTangentY.Add(Binormal);
            RawMesh.WedgeTangentZ.Add(Normal);

            for (int32 ch = 0; ch < NumUVChannels; ++ch)
            {
                FVector2D UV = FVector2D::ZeroVector;
                if (ch < Data.TextureCoordinates.Num() && VIdx < Data.TextureCoordinates[ch].Num())
                    UV = Data.TextureCoordinates[ch][VIdx];
                RawMesh.WedgeTexCoords[ch].Add(UV);
            }

            if (Data.VertexColors.Num() > 0 && VIdx < Data.VertexColors[0].Data.Num())
                RawMesh.WedgeColors.Add(Data.VertexColors[0].Data[VIdx]);
        }

        const int32 NumFaces = NumWedges / 3;
        RawMesh.FaceMaterialIndices.SetNumZeroed(NumFaces);
        RawMesh.FaceSmoothingMasks.Init(0xFFFFFFFF, NumFaces);

        for (int32 MatIdx = 0; MatIdx < Data.Materials.Num(); ++MatIdx)
        {
            const FMaterialChunk& Mat = Data.Materials[MatIdx];
            for (int32 f = Mat.FirstIndex; f < Mat.FirstIndex + Mat.NumFaces; ++f)
            {
                if (f < NumFaces)
                    RawMesh.FaceMaterialIndices[f] = MatIdx;
            }
        }

        SourceModel.RawMeshBulkData->SaveRawMesh(RawMesh);
    }

    for (const FMaterialChunk& MatInfo : LODData[0].Materials)
    {
        FStaticMaterial Mat;
        Mat.MaterialSlotName         = FName(MatInfo.Name.c_str());
        Mat.ImportedMaterialSlotName = FName(MatInfo.Name.c_str());
        Mat.MaterialInterface        = nullptr;
        StaticMesh->StaticMaterials.Add(Mat);
    }

    StaticMesh->Build(false);
    StaticMesh->MarkPackageDirty();
    return StaticMesh;
}

USkeleton* UEFModelFactory::CreateSkeleton(
    const FString& Name, UObject* Parent, EObjectFlags Flags,
    FSkeletonData& Data, FReferenceSkeleton& OutRefSkeleton)
{
    FString SkeletonName = Name + TEXT("_Skeleton");
    UPackage* SkeletonPackage = CreatePackage(nullptr, *FPaths::Combine(FPaths::GetPath(Parent->GetPathName()), SkeletonName));
    USkeleton* Skeleton = NewObject<USkeleton>(SkeletonPackage, FName(*SkeletonName), Flags);

    {
        FReferenceSkeletonModifier Modifier(OutRefSkeleton, Skeleton);
        OutRefSkeleton.Empty();

        for (const FBoneChunk& Bone : Data.Bones)
        {
            FTransform Transform;
            Transform.SetLocation(Bone.BonePos);
            Transform.SetRotation(Bone.BoneRot);
            Transform.SetScale3D(FVector::OneVector);

            FMeshBoneInfo Info(FName(Bone.BoneName.c_str()), Bone.BoneName.c_str(), Bone.BoneParentIndex);
            Modifier.Add(Info, Transform);
        }
    }

    for (const FSocketChunk& Sock : Data.Sockets)
    {
        USkeletalMeshSocket* NewSocket = NewObject<USkeletalMeshSocket>(Skeleton);
        NewSocket->SocketName     = FName(Sock.SocketName.c_str());
        NewSocket->BoneName       = FName(Sock.SocketParentName.c_str());
        NewSocket->RelativeLocation = Sock.SocketPos * ImportScale;
        NewSocket->RelativeRotation = Sock.SocketRot.Rotator();
        NewSocket->RelativeScale    = Sock.SocketScale;
        Skeleton->Sockets.Add(NewSocket);
    }

    return Skeleton;
}

USkeletalMesh* UEFModelFactory::CreateSkeletalMesh(
    TArray<FLODData>& LODData, FSkeletonData& SkeletonData,
    UObject* Parent, FName Name, EObjectFlags Flags)
{
    USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>(Parent->GetOutermost(), Name, Flags);
    SkeletalMesh->PreEditChange(nullptr);

    FReferenceSkeleton RefSkeleton;
    USkeleton* Skeleton = CreateSkeleton(Name.ToString(), Parent, Flags, SkeletonData, RefSkeleton);

    SkeletalMesh->RefSkeleton = RefSkeleton;
    SkeletalMesh->Skeleton = Skeleton;

    for (const FMaterialChunk& MatInfo : LODData[0].Materials)
    {
        FSkeletalMaterial Mat;
        Mat.MaterialSlotName         = FName(MatInfo.Name.c_str());
        Mat.ImportedMaterialSlotName = FName(MatInfo.Name.c_str());
        Mat.MaterialInterface        = nullptr;
        SkeletalMesh->Materials.Add(Mat);
    }

    IMeshUtilities& MeshUtils = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");
    FSkeletalMeshModel* ImportedModel = SkeletalMesh->GetImportedModel();
    ImportedModel->LODModels.Empty();

    for (int32 LodIndex = 0; LodIndex < LODData.Num(); ++LodIndex)
    {
        FLODData& Data = LODData[LodIndex];

        ImportedModel->LODModels.Add(new FSkeletalMeshLODModel());
        FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[LodIndex];

        const int32 NumUVChannels = FMath::Clamp<int32>(
            Data.TextureCoordinates.Num(),
            1,
            (int32)MAX_TEXCOORDS
        );
        LODModel.NumTexCoords = NumUVChannels;

        FSkeletalMeshImportData ImportData;

        ImportData.bHasNormals      = Data.Normals.Num() > 0;
        ImportData.bHasTangents     = Data.Tangents.Num() > 0;
        ImportData.bHasVertexColors = Data.VertexColors.Num() > 0;
        ImportData.NumTexCoords     = NumUVChannels;

        ImportData.Points.Reserve(Data.Vertices.Num());
        for (const FVector& V : Data.Vertices)
            ImportData.Points.Add(V * ImportScale);

        TArray<int32> PointToRawMap;
        PointToRawMap.Reserve(Data.Vertices.Num());
        for (int32 i = 0; i < Data.Vertices.Num(); i++)
            PointToRawMap.Add(i);
        ImportData.PointToRawMap = PointToRawMap;

        ImportData.RefBonesBinary.Reserve(SkeletonData.Bones.Num());
        for (const FBoneChunk& Bone : SkeletonData.Bones)
        {
            SkeletalMeshImportData::FBone ImportBone;
            ImportBone.Name        = Bone.BoneName.c_str();
            ImportBone.Flags       = 0;
            ImportBone.NumChildren = 0;
            ImportBone.ParentIndex = Bone.BoneParentIndex;
            FTransform BoneTransform;
            BoneTransform.SetLocation(Bone.BonePos * ImportScale);
            BoneTransform.SetRotation(Bone.BoneRot);
            BoneTransform.SetScale3D(FVector::OneVector);
            ImportBone.BonePos.Transform = BoneTransform;
            ImportBone.BonePos.Length    = 0.f;
            ImportBone.BonePos.XSize     = 0.f;
            ImportBone.BonePos.YSize     = 0.f;
            ImportBone.BonePos.ZSize     = 0.f;
            ImportData.RefBonesBinary.Add(ImportBone);
        }

        ImportData.Materials.Reserve(Data.Materials.Num());
        for (const FMaterialChunk& Mat : Data.Materials)
        {
            SkeletalMeshImportData::FMaterial ImportMat;
            ImportMat.MaterialImportName = Mat.Name.c_str();
            ImportMat.Material           = nullptr;
            ImportData.Materials.Add(ImportMat);
        }

        for (int32 MatIdx = 0; MatIdx < Data.Materials.Num(); ++MatIdx)
        {
            const FMaterialChunk& Mat = Data.Materials[MatIdx];
            for (int32 f = 0; f < Mat.NumFaces; ++f)
            {
                SkeletalMeshImportData::FTriangle Face;
                FMemory::Memzero(Face);
                Face.MatIndex      = static_cast<uint8>(MatIdx);
                Face.SmoothingGroups = 255;

                for (int32 w = 0; w < 3; ++w)
                {
                    const int32 WedgeGlobalIdx = (Mat.FirstIndex + f) * 3 + w;
                    if (WedgeGlobalIdx >= Data.Indices.Num()) break;
                    const int32 VIdx = Data.Indices[WedgeGlobalIdx];

                    FVector Normal(FVector::ZeroVector), Tangent(FVector::ZeroVector);
                    float   BinSign = 1.f;
                    if (Data.Normals.Num() > 0 && VIdx < Data.Normals.Num())
                    {
                        const FVector4& N4 = Data.Normals[VIdx];
                        BinSign = N4.X;
                        Normal  = FVector(N4.Y, N4.Z, N4.W);
                    }
                    if (Data.Tangents.Num() > 0 && VIdx < Data.Tangents.Num())
                        Tangent = Data.Tangents[VIdx];

                    Face.TangentX[w] = Tangent;
                    Face.TangentY[w] = FVector::CrossProduct(Normal, Tangent) * BinSign;
                    Face.TangentZ[w] = Normal;

                    SkeletalMeshImportData::FVertex Wedge;
                    Wedge.VertexIndex = static_cast<uint32>(VIdx);
                    Wedge.Color       = (Data.VertexColors.Num() > 0 && VIdx < Data.VertexColors[0].Data.Num())
                                            ? Data.VertexColors[0].Data[VIdx]
                                            : FColor::White;
                    for (int32 ch = 0; ch < NumUVChannels; ++ch)
                    {
                        FVector2D UV = FVector2D::ZeroVector;
                        if (ch < Data.TextureCoordinates.Num() && VIdx < Data.TextureCoordinates[ch].Num())
                            UV = Data.TextureCoordinates[ch][VIdx];
                        Wedge.UVs[ch] = UV;
                    }

                    Face.WedgeIndex[w] = ImportData.Wedges.Add(Wedge);
                }
                ImportData.Faces.Add(Face);
            }
        }

        ImportData.Influences.Reserve(Data.Weights.Num());
        for (const FWeightChunk& W : Data.Weights)
        {
            SkeletalMeshImportData::FRawBoneInfluence Inf;
            Inf.BoneIndex   = W.WeightBoneIndex;
            Inf.VertexIndex = W.WeightVertexIndex;
            Inf.Weight      = W.WeightAmount;
            ImportData.Influences.Add(Inf);
        }


        TArray<FVector>                                LODPoints;
        TArray<SkeletalMeshImportData::FMeshWedge>     LODWedges;
        TArray<SkeletalMeshImportData::FMeshFace>      LODFaces;
        TArray<SkeletalMeshImportData::FVertInfluence> LODInfluences;
        TArray<int32>                                  LODPointToRaw;

        ImportData.CopyLODImportData(LODPoints, LODWedges, LODFaces, LODInfluences, LODPointToRaw);

        IMeshUtilities::MeshBuildOptions BuildOptions;
        BuildOptions.bComputeNormals  = !ImportData.bHasNormals;
        BuildOptions.bComputeTangents = !ImportData.bHasTangents;
        BuildOptions.bUseMikkTSpace   = true;

        bool bBuildOK = MeshUtils.BuildSkeletalMesh(
            LODModel,
            RefSkeleton,
            LODInfluences,
            LODWedges,
            LODFaces,
            LODPoints,
            LODPointToRaw,
            BuildOptions,
            nullptr,
            nullptr
        );

        if (!bBuildOK)
        {
            UE_LOG(LogTemp, Error, TEXT("UEFModelFactory: BuildSkeletalMesh failed for LOD %d"), LodIndex);
            return nullptr;
        }

        if (LodIndex == 0)
        {
            for (const FMorphTargetChunk& MorphChunk : Data.Morphs)
            {
                UMorphTarget* MorphTarget = NewObject<UMorphTarget>(SkeletalMesh, FName(MorphChunk.MorphName.c_str()));
                FMorphTargetLODModel MorphLOD;
                MorphLOD.NumBaseMeshVerts = Data.Vertices.Num();
                for (const FMorphTargetDataChunk& Delta : MorphChunk.MorphDeltas)
                {
                    FMorphTargetDelta MTD;
                    MTD.PositionDelta = FVector(Delta.MorphPosition.X, -Delta.MorphPosition.Y, Delta.MorphPosition.Z);
                    MTD.TangentZDelta = FVector::ZeroVector;
                    MTD.SourceIdx     = static_cast<uint32>(Delta.MorphVertexIndex);
                    MorphLOD.Vertices.Add(MTD);
                }
                MorphTarget->MorphLODModels.Add(MorphLOD);
                SkeletalMesh->MorphTargets.Add(MorphTarget);
                FAssetRegistryModule::AssetCreated(MorphTarget);
            }
        }
    }

    SkeletalMesh->ResetLODInfo();
    for (int32 i = 0; i < LODData.Num(); i++)
    {
        FSkeletalMeshLODInfo LODInfo;
        LODInfo.ScreenSize.Default = (i == 0) ? 1.f : (1.f / FMath::Pow(2.f, (float)i));
        SkeletalMesh->AddLODInfo(LODInfo);
    }

    Skeleton->MergeAllBonesToBoneTree(SkeletalMesh);
    Skeleton->SetPreviewMesh(SkeletalMesh);

    SkeletalMesh->InitMorphTargets();
    SkeletalMesh->CalculateInvRefMatrices();
    SkeletalMesh->PostEditChange();

    Skeleton->PostEditChange();
    FAssetRegistryModule::AssetCreated(Skeleton);

    return SkeletalMesh;
}