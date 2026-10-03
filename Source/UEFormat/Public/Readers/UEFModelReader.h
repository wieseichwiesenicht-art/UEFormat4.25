// Copyright © 2025 Marcel K. All rights reserved.

#pragma once
#include <fstream>
#include "Math/Quat.h"
#include "Containers/Array.h"

// UEFormat file versions (EUEFormatVersion upstream)
constexpr uint8 UEF_VERSION_ATTRIBUTE_FORMAT_RESTRUCTURE = 10; // adds ObjectPath + attribute-based body
constexpr uint8 UEF_VERSION_LATEST                       = 10;

template<typename T>
T ReadData(std::ifstream& Ar) {
    T Data;
    Ar.read(reinterpret_cast<char*>(&Data), sizeof(T));
    return Data;
}

std::string ReadString(std::ifstream& Ar, int32 Size);
std::string ReadFString(std::ifstream& Ar);

template<typename T>
T ReadBufferData(const char* DataArray, int& Offset) {
    T Data;
    std::memcpy(&Data, &DataArray[Offset], sizeof(T));
    Offset += sizeof(T);
    return Data;
}

template<typename T>
void ReadBufferArray(const char* DataArray, int& Offset, int ArraySize, TArray<T>& Data) {
    Data.SetNum(ArraySize);
    for (auto i = 0; i < ArraySize; i++) {
        std::memcpy(&Data[i], &DataArray[Offset], sizeof(T));
        Offset += sizeof(T);
    }
}

FQuat ReadBufferQuat(const char* DataArray, int& Offset);
std::string ReadBufferString(const char* DataArray, int& Offset, int32 Size);
std::string ReadBufferFString(const char* DataArray, int& Offset);
struct FVertexColorChunk {
    std::string Name;
    int32 Count;
    TArray<FColor> Data;
};
struct FWeightChunk {
    short WeightBoneIndex;
    int32 WeightVertexIndex;
    float WeightAmount;
};
struct FBoneChunk {
    std::string BoneName;
    int32 BoneParentIndex;
    FVector BonePos;     // was FVector3f
    FQuat   BoneRot;     // was FQuat4f
    FVector BoneScale = FVector::OneVector; // only serialized since version 10
};
struct FSocketChunk {
    std::string SocketName;
    std::string SocketParentName;
    FVector SocketPos;   // was FVector3f
    FQuat   SocketRot;   // was FQuat4f
    FVector SocketScale; // was FVector3f
};
struct FMaterialChunk {
    std::string Name;
    std::string Path;
    int32 FirstIndex;
    int32 NumFaces;
};
struct FMorphTargetDataChunk {
    FVector MorphPosition;  // was FVector3f
    FVector MorphNormals;   // was FVector3f
    int32   MorphVertexIndex;
};
struct FMorphTargetChunk {
    std::string MorphName;
    TArray<FMorphTargetDataChunk> MorphDeltas;
};
struct FVirtualBoneChunk {
    std::string SourceBoneName;
    std::string TargetBoneName;
    std::string VirtualBoneName;
};
struct FUEFormatHeader {
    std::string Identifier;
    uint8       FileVersionBytes;   // was std::byte (UE5-only / C++17)
    std::string ObjectName;
    std::string ObjectPath;             // only serialized since version 10
    bool        IsCompressed;
    std::string CompressionType;
    int32       CompressedSize;
    int32       UncompressedSize;
};
struct FLODData {
    TArray<FVector>  Vertices;           // was FVector3f
    TArray<int32>    Indices;
    TArray<FVector4> Normals;            // was FVector4f  (X=binormal sign, YZW=normal)
    TArray<FVector>  Tangents;           // was FVector3f
    TArray<FVertexColorChunk>         VertexColors;
    TArray<TArray<FVector2D>>         TextureCoordinates; // was FVector2f
    TArray<FMaterialChunk>            Materials;
    TArray<FWeightChunk>              Weights;
    TArray<FMorphTargetChunk>         Morphs;
};
struct FSkeletonData {
    std::string Path;
    TArray<FBoneChunk>        Bones;
    TArray<FSocketChunk>      Sockets;
    TArray<FVirtualBoneChunk> VirtualBones;
};

class UEFORMAT_API UEFModelReader {
public:
    UEFModelReader(const FString Filename);
    ~UEFModelReader();
    
    bool Read();
    
    FUEFormatHeader  Header;
    TArray<FLODData> LODs;
    FSkeletonData    Skeleton;

private:
    const std::string GMAGIC = "UEFORMAT";
    const std::string GZIP   = "GZIP";
    const std::string ZSTD   = "ZSTD";
    
    std::ifstream Ar;
    void ReadBuffer(const char* Buffer, int32 BufferSize);
    void ReadChunks(const char* Buffer, int& Offset, int32 ByteSize, int LODIndex);

    // Version 10+ attribute-based layout
    void ReadAttributeModel(const char* Buffer, int Offset, int32 End);
    void ReadAttributeLOD(const char* Buffer, int& Offset, FLODData& LOD);
    void ReadAttributeSkeleton(const char* Buffer, int Offset, int32 End);
};
