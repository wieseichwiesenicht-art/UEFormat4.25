// Copyright © 2025 Marcel K. All rights reserved.

#include "Readers/UEFModelReader.h"
#include <string>
#include "zstd.h"
#include "Misc/Compression.h"
#include <vector>

std::string ReadString(std::ifstream& Ar, int32 Size)
{
    std::string String;
    String.resize(Size);
    Ar.read(&String[0], Size);
    return String;
}

std::string ReadFString(std::ifstream& Ar)
{
    int32 Size = ReadData<int32>(Ar);
    std::string String;
    String.resize(Size);
    Ar.read(&String[0], Size);
    return String;
}


FQuat ReadBufferQuat(const char* DataArray, int& Offset)
{
    float X = ReadBufferData<float>(DataArray, Offset);
    float Y = ReadBufferData<float>(DataArray, Offset);
    float Z = ReadBufferData<float>(DataArray, Offset);
    float W = ReadBufferData<float>(DataArray, Offset);
    return FQuat(X, Y, Z, W).GetNormalized();
}

FVector ReadBufferVector3f(const char* DataArray, int& Offset)
{
    float X = ReadBufferData<float>(DataArray, Offset);
    float Y = ReadBufferData<float>(DataArray, Offset);
    float Z = ReadBufferData<float>(DataArray, Offset);
    return FVector(X, Y, Z);
}

FVector4 ReadBufferVector4f(const char* DataArray, int& Offset)
{
    float X = ReadBufferData<float>(DataArray, Offset);
    float Y = ReadBufferData<float>(DataArray, Offset);
    float Z = ReadBufferData<float>(DataArray, Offset);
    float W = ReadBufferData<float>(DataArray, Offset);
    return FVector4(X, Y, Z, W);
}

FVector2D ReadBufferVector2f(const char* DataArray, int& Offset)
{
    float U = ReadBufferData<float>(DataArray, Offset);
    float V = ReadBufferData<float>(DataArray, Offset);
    return FVector2D(U, V);
}

std::string ReadBufferString(const char* DataArray, int& Offset, int32 Size)
{
    std::string String;
    String.resize(Size);
    std::memcpy(&String[0], &DataArray[Offset], Size);
    Offset += Size;
    return String;
}

std::string ReadBufferFString(const char* DataArray, int& Offset)
{
    int32 Size = ReadBufferData<int32>(DataArray, Offset);
    std::string String;
    String.resize(Size);
    std::memcpy(&String[0], &DataArray[Offset], Size);
    Offset += Size;
    return String;
}

UEFModelReader::UEFModelReader(const FString Filename) : Ar(ToCStr(Filename), std::ios::binary) {}

UEFModelReader::~UEFModelReader()
{
    if (Ar.is_open())
        Ar.close();
}

bool UEFModelReader::Read()
{
    std::string Magic = ReadString(Ar, GMAGIC.length());
    if (Magic != GMAGIC) return false;

    Header.Identifier       = ReadFString(Ar);
    Header.FileVersionBytes = ReadData<uint8>(Ar);   // was std::byte
    if (Header.FileVersionBytes > UEF_VERSION_LATEST)
    {
        UE_LOG(LogTemp, Error, TEXT("UEFormat file version %d is not supported (latest supported: %d)."), Header.FileVersionBytes, UEF_VERSION_LATEST);
        return false;
    }
    Header.ObjectName       = ReadFString(Ar);
    if (Header.FileVersionBytes >= UEF_VERSION_ATTRIBUTE_FORMAT_RESTRUCTURE)
        Header.ObjectPath   = ReadFString(Ar);
    Header.IsCompressed     = ReadData<bool>(Ar);

    if (Header.IsCompressed)
    {
        Header.CompressionType   = ReadFString(Ar);
        Header.UncompressedSize  = ReadData<int32>(Ar);
        Header.CompressedSize    = ReadData<int32>(Ar);

        std::vector<char> CompressedBuffer(Header.CompressedSize);
        Ar.read(CompressedBuffer.data(), Header.CompressedSize);
        if (Ar.fail())
        {
            UE_LOG(LogTemp, Error, TEXT("Error reading compressed data."));
            return false;
        }

        std::vector<char> UncompressedBuffer(Header.UncompressedSize);

        if (Header.CompressionType == "ZSTD")
        {
            size_t Result = ZSTD_decompress(
                UncompressedBuffer.data(),
                Header.UncompressedSize,
                CompressedBuffer.data(),
                Header.CompressedSize
            );

            if (ZSTD_isError(Result))
            {
                UE_LOG(LogTemp, Error, TEXT("ZSTD decompression failed"));
                return false;
            }
        }
        else if (Header.CompressionType == "GZIP")
        {
            bool bSuccess = FCompression::UncompressMemory(
                NAME_Gzip,
                UncompressedBuffer.data(),
                Header.UncompressedSize,
                CompressedBuffer.data(),
                Header.CompressedSize
            );

            if (!bSuccess)
            {
                UE_LOG(LogTemp, Error, TEXT("GZIP decompression failed"));
                return false;
            }
        }
        ReadBuffer(UncompressedBuffer.data(), Header.UncompressedSize);
    }
    else
    {
        const auto CurrentPos    = Ar.tellg();
        Ar.seekg(0, std::ios::end);
        const auto RemainingSize = Ar.tellg() - CurrentPos;
        Ar.seekg(CurrentPos, std::ios::beg);

        std::vector<char> UncompressedBuffer(RemainingSize);
        Ar.read(UncompressedBuffer.data(), RemainingSize);
        if (Ar.fail())
        {
            UE_LOG(LogTemp, Error, TEXT("Error reading uncompressed data."));
            return false;
        }

        ReadBuffer(UncompressedBuffer.data(), RemainingSize);
    }

    Ar.close();
    return true;
}

void UEFModelReader::ReadBuffer(const char* Buffer, int32 BufferSize)
{
    if (Header.FileVersionBytes >= UEF_VERSION_ATTRIBUTE_FORMAT_RESTRUCTURE)
    {
        ReadAttributeModel(Buffer, 0, BufferSize);
        return;
    }

    int32 Offset = 0;

    while (Offset < BufferSize)
    {
        std::string ChunkName = ReadBufferFString(Buffer, Offset);
        int32 ArraySize       = ReadBufferData<int32>(Buffer, Offset);
        int32 ByteSize        = ReadBufferData<int32>(Buffer, Offset);

        if (ChunkName == "LODS")
        {
            LODs.SetNum(ArraySize);
            for (int32 index = 0; index < ArraySize; ++index)
            {
                std::string LODName    = ReadBufferFString(Buffer, Offset);
                int32       LODByteSize = ReadBufferData<int32>(Buffer, Offset);
                ReadChunks(Buffer, Offset, LODByteSize, index);
            }
        }
        else if (ChunkName == "SKELETON")
            ReadChunks(Buffer, Offset, ByteSize, 0);
        else
            Offset += ByteSize;
    }
}

void UEFModelReader::ReadChunks(const char* Buffer, int32& Offset, int32 ByteSize, int32 LODIndex)
{
    int32 InnerOffset = Offset;
    while (InnerOffset < Offset + ByteSize)
    {
        std::string InnerChunkName  = ReadBufferFString(Buffer, InnerOffset);
        int32       InnerArraySize  = ReadBufferData<int32>(Buffer, InnerOffset);
        int32       InnerByteSize   = ReadBufferData<int32>(Buffer, InnerOffset);

        if (InnerChunkName == "VERTICES")
        {
            LODs[LODIndex].Vertices.SetNum(InnerArraySize);
            for (auto i = 0; i < InnerArraySize; i++)
                LODs[LODIndex].Vertices[i] = ReadBufferVector3f(Buffer, InnerOffset);
        }
        else if (InnerChunkName == "INDICES")
        {
            ReadBufferArray(Buffer, InnerOffset, InnerArraySize, LODs[LODIndex].Indices);
        }
        else if (InnerChunkName == "NORMALS")
        {
            LODs[LODIndex].Normals.SetNum(InnerArraySize);
            for (auto i = 0; i < InnerArraySize; i++)
                LODs[LODIndex].Normals[i] = ReadBufferVector4f(Buffer, InnerOffset);
        }
        else if (InnerChunkName == "TANGENTS")
        {
            LODs[LODIndex].Tangents.SetNum(InnerArraySize);
            for (auto i = 0; i < InnerArraySize; i++)
                LODs[LODIndex].Tangents[i] = ReadBufferVector3f(Buffer, InnerOffset);
        }
        else if (InnerChunkName == "VERTEXCOLORS")
        {
            LODs[LODIndex].VertexColors.SetNum(InnerArraySize);
            for (auto i = 0; i < InnerArraySize; i++)
            {
                LODs[LODIndex].VertexColors[i].Name  = ReadBufferFString(Buffer, InnerOffset);
                LODs[LODIndex].VertexColors[i].Count = ReadBufferData<int32>(Buffer, InnerOffset);
                ReadBufferArray(Buffer, InnerOffset, LODs[LODIndex].VertexColors[i].Count, LODs[LODIndex].VertexColors[i].Data);
            }
        }
        else if (InnerChunkName == "MATERIALS")
        {
            LODs[LODIndex].Materials.SetNum(InnerArraySize);
            for (auto i = 0; i < InnerArraySize; i++)
            {
                LODs[LODIndex].Materials[i].Name       = ReadBufferFString(Buffer, InnerOffset);
                LODs[LODIndex].Materials[i].Path       = ReadBufferFString(Buffer, InnerOffset);
                LODs[LODIndex].Materials[i].FirstIndex = ReadBufferData<int32>(Buffer, InnerOffset);
                LODs[LODIndex].Materials[i].NumFaces   = ReadBufferData<int32>(Buffer, InnerOffset);
            }
        }
        else if (InnerChunkName == "TEXCOORDS")
        {
            LODs[LODIndex].TextureCoordinates.SetNum(InnerArraySize);
            for (auto i = 0; i < InnerArraySize; i++)
            {
                int32 UVCount = ReadBufferData<int32>(Buffer, InnerOffset);
                LODs[LODIndex].TextureCoordinates[i].SetNum(UVCount);
                for (auto j = 0; j < UVCount; j++)
                    LODs[LODIndex].TextureCoordinates[i][j] = ReadBufferVector2f(Buffer, InnerOffset);
            }
        }
        else if (InnerChunkName == "SOCKETS")
        {
            Skeleton.Sockets.SetNum(InnerArraySize);
            for (auto i = 0; i < InnerArraySize; i++)
            {
                Skeleton.Sockets[i].SocketName       = ReadBufferFString(Buffer, InnerOffset);
                Skeleton.Sockets[i].SocketParentName = ReadBufferFString(Buffer, InnerOffset);
                Skeleton.Sockets[i].SocketPos        = ReadBufferVector3f(Buffer, InnerOffset);
                Skeleton.Sockets[i].SocketRot        = ReadBufferQuat(Buffer, InnerOffset);
                Skeleton.Sockets[i].SocketScale      = ReadBufferVector3f(Buffer, InnerOffset);
            }
        }
        else if (InnerChunkName == "BONES")
        {
            Skeleton.Bones.SetNum(InnerArraySize);
            for (auto i = 0; i < InnerArraySize; i++)
            {
                Skeleton.Bones[i].BoneName        = ReadBufferFString(Buffer, InnerOffset);
                Skeleton.Bones[i].BoneParentIndex = ReadBufferData<int32>(Buffer, InnerOffset);
                Skeleton.Bones[i].BonePos         = ReadBufferVector3f(Buffer, InnerOffset);
                Skeleton.Bones[i].BoneRot         = ReadBufferQuat(Buffer, InnerOffset);
            }
        }
        else if (InnerChunkName == "WEIGHTS")
        {
            LODs[LODIndex].Weights.SetNum(InnerArraySize);
            for (auto i = 0; i < InnerArraySize; i++)
            {
                LODs[LODIndex].Weights[i].WeightBoneIndex   = ReadBufferData<short>(Buffer, InnerOffset);
                LODs[LODIndex].Weights[i].WeightVertexIndex = ReadBufferData<int32>(Buffer, InnerOffset);
                LODs[LODIndex].Weights[i].WeightAmount      = ReadBufferData<float>(Buffer, InnerOffset);
            }
        }
        else if (InnerChunkName == "MORPHTARGETS")
        {
            LODs[LODIndex].Morphs.SetNum(InnerArraySize);
            for (auto i = 0; i < InnerArraySize; i++)
            {
                LODs[LODIndex].Morphs[i].MorphName = ReadBufferFString(Buffer, InnerOffset);
                const auto DeltaNum = ReadBufferData<int32>(Buffer, InnerOffset);
                LODs[LODIndex].Morphs[i].MorphDeltas.SetNum(DeltaNum);
                for (auto j = 0; j < DeltaNum; j++)
                {
                    LODs[LODIndex].Morphs[i].MorphDeltas[j].MorphPosition    = ReadBufferVector3f(Buffer, InnerOffset);
                    LODs[LODIndex].Morphs[i].MorphDeltas[j].MorphNormals     = ReadBufferVector3f(Buffer, InnerOffset);
                    LODs[LODIndex].Morphs[i].MorphDeltas[j].MorphVertexIndex = ReadBufferData<int32>(Buffer, InnerOffset);
                }
            }
        }
        else if (InnerChunkName == "VIRTUALBONES")
        {
            Skeleton.VirtualBones.SetNum(InnerArraySize);
            for (auto i = 0; i < InnerArraySize; i++)
            {
                Skeleton.VirtualBones[i].SourceBoneName  = ReadBufferFString(Buffer, InnerOffset);
                Skeleton.VirtualBones[i].TargetBoneName  = ReadBufferFString(Buffer, InnerOffset);
                Skeleton.VirtualBones[i].VirtualBoneName = ReadBufferFString(Buffer, InnerOffset);
            }
        }
        else if (InnerChunkName == "METADATA")
        {
            Skeleton.Path = ReadBufferFString(Buffer, InnerOffset);
        }
        else
            InnerOffset += InnerByteSize;
    }
    Offset += ByteSize;
}

// ---------------------------------------------------------------------------
// Version 10+: Count, then per attribute: Name, ByteSize, Payload. Arrays carry their own count.
// ---------------------------------------------------------------------------

void UEFModelReader::ReadAttributeModel(const char* Buffer, int Offset, int32 End)
{
    const int32 AttributeCount = ReadBufferData<int32>(Buffer, Offset);
    for (int32 AttributeIndex = 0; AttributeIndex < AttributeCount && Offset < End; ++AttributeIndex)
    {
        std::string Name     = ReadBufferFString(Buffer, Offset);
        const int32 ByteSize = ReadBufferData<int32>(Buffer, Offset);
        const int32 Start    = Offset;

        if (Name == "LODS")
        {
            int32 P = Start;
            const int32 LODCount = ReadBufferData<int32>(Buffer, P);
            LODs.SetNum(LODCount);
            for (int32 index = 0; index < LODCount; ++index)
                ReadAttributeLOD(Buffer, P, LODs[index]);
        }
        else if (Name == "SKELETON")
        {
            ReadAttributeSkeleton(Buffer, Start, Start + ByteSize);
        }
        else if (Name != "COLLISION") // collision is unsupported
        {
            UE_LOG(LogTemp, Warning, TEXT("Unknown model attribute: %s"), UTF8_TO_TCHAR(Name.c_str()));
        }

        Offset = Start + ByteSize;
    }
}

void UEFModelReader::ReadAttributeLOD(const char* Buffer, int& Offset, FLODData& LOD)
{
    std::string LODName = ReadBufferFString(Buffer, Offset);

    const int32 AttributeCount = ReadBufferData<int32>(Buffer, Offset);
    for (int32 AttributeIndex = 0; AttributeIndex < AttributeCount; ++AttributeIndex)
    {
        std::string Name     = ReadBufferFString(Buffer, Offset);
        const int32 ByteSize = ReadBufferData<int32>(Buffer, Offset);
        const int32 Start    = Offset;
        int32 P              = Start;

        if (Name == "VERTICES")
        {
            const int32 Count = ReadBufferData<int32>(Buffer, P);
            LOD.Vertices.SetNum(Count);
            for (auto i = 0; i < Count; i++)
                LOD.Vertices[i] = ReadBufferVector3f(Buffer, P);
        }
        else if (Name == "NORMALS")
        {
            const int32 Count = ReadBufferData<int32>(Buffer, P);
            LOD.Normals.SetNum(Count);
            for (auto i = 0; i < Count; i++)
                LOD.Normals[i] = ReadBufferVector4f(Buffer, P);
        }
        else if (Name == "TANGENTS")
        {
            const int32 Count = ReadBufferData<int32>(Buffer, P);
            LOD.Tangents.SetNum(Count);
            for (auto i = 0; i < Count; i++)
                LOD.Tangents[i] = ReadBufferVector3f(Buffer, P);
        }
        else if (Name == "INDICES")
        {
            const int32 Count = ReadBufferData<int32>(Buffer, P);
            ReadBufferArray(Buffer, P, Count, LOD.Indices);
        }
        else if (Name == "TEXCOORDS")
        {
            const int32 ChannelCount = ReadBufferData<int32>(Buffer, P);
            LOD.TextureCoordinates.SetNum(ChannelCount);
            for (auto i = 0; i < ChannelCount; i++)
            {
                std::string ChannelName = ReadBufferFString(Buffer, P);
                const int32 UVCount = ReadBufferData<int32>(Buffer, P);
                LOD.TextureCoordinates[i].SetNum(UVCount);
                for (auto j = 0; j < UVCount; j++)
                    LOD.TextureCoordinates[i][j] = ReadBufferVector2f(Buffer, P);
            }
        }
        else if (Name == "VERTEXCOLORS")
        {
            const int32 Count = ReadBufferData<int32>(Buffer, P);
            LOD.VertexColors.SetNum(Count);
            for (auto i = 0; i < Count; i++)
            {
                LOD.VertexColors[i].Name  = ReadBufferFString(Buffer, P);
                LOD.VertexColors[i].Count = ReadBufferData<int32>(Buffer, P);
                LOD.VertexColors[i].Data.SetNum(LOD.VertexColors[i].Count);
                for (auto j = 0; j < LOD.VertexColors[i].Count; j++)
                {
                    const uint8 R = ReadBufferData<uint8>(Buffer, P);
                    const uint8 G = ReadBufferData<uint8>(Buffer, P);
                    const uint8 B = ReadBufferData<uint8>(Buffer, P);
                    const uint8 A = ReadBufferData<uint8>(Buffer, P);
                    LOD.VertexColors[i].Data[j] = FColor(R, G, B, A);
                }
            }
        }
        else if (Name == "MATERIALS")
        {
            const int32 Count = ReadBufferData<int32>(Buffer, P);
            LOD.Materials.SetNum(Count);
            for (auto i = 0; i < Count; i++)
            {
                LOD.Materials[i].Name       = ReadBufferFString(Buffer, P);
                LOD.Materials[i].Path       = ReadBufferFString(Buffer, P);
                LOD.Materials[i].FirstIndex = ReadBufferData<int32>(Buffer, P);
                LOD.Materials[i].NumFaces   = ReadBufferData<int32>(Buffer, P);
            }
        }
        else if (Name == "WEIGHTS")
        {
            const int32 Count = ReadBufferData<int32>(Buffer, P);
            LOD.Weights.SetNum(Count);
            for (auto i = 0; i < Count; i++)
            {
                LOD.Weights[i].WeightBoneIndex   = static_cast<short>(ReadBufferData<uint16>(Buffer, P));
                LOD.Weights[i].WeightVertexIndex = ReadBufferData<int32>(Buffer, P);
                LOD.Weights[i].WeightAmount      = ReadBufferData<float>(Buffer, P);
            }
        }
        else if (Name == "MORPHTARGETS")
        {
            const int32 Count = ReadBufferData<int32>(Buffer, P);
            LOD.Morphs.SetNum(Count);
            for (auto i = 0; i < Count; i++)
            {
                LOD.Morphs[i].MorphName = ReadBufferFString(Buffer, P);
                const int32 DeltaNum = ReadBufferData<int32>(Buffer, P);
                LOD.Morphs[i].MorphDeltas.SetNum(DeltaNum);
                for (auto j = 0; j < DeltaNum; j++)
                {
                    LOD.Morphs[i].MorphDeltas[j].MorphPosition    = ReadBufferVector3f(Buffer, P);
                    LOD.Morphs[i].MorphDeltas[j].MorphNormals     = ReadBufferVector3f(Buffer, P);
                    LOD.Morphs[i].MorphDeltas[j].MorphVertexIndex = ReadBufferData<int32>(Buffer, P);
                }
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Unknown LOD attribute: %s"), UTF8_TO_TCHAR(Name.c_str()));
        }

        Offset = Start + ByteSize;
    }
}

void UEFModelReader::ReadAttributeSkeleton(const char* Buffer, int Offset, int32 End)
{
    const int32 AttributeCount = ReadBufferData<int32>(Buffer, Offset);
    for (int32 AttributeIndex = 0; AttributeIndex < AttributeCount && Offset < End; ++AttributeIndex)
    {
        std::string Name     = ReadBufferFString(Buffer, Offset);
        const int32 ByteSize = ReadBufferData<int32>(Buffer, Offset);
        const int32 Start    = Offset;
        int32 P              = Start;

        if (Name == "METADATA")
        {
            Skeleton.Path = ReadBufferFString(Buffer, P);
        }
        else if (Name == "BONES")
        {
            const int32 Count = ReadBufferData<int32>(Buffer, P);
            Skeleton.Bones.SetNum(Count);
            for (auto i = 0; i < Count; i++)
            {
                Skeleton.Bones[i].BoneName        = ReadBufferFString(Buffer, P);
                Skeleton.Bones[i].BoneParentIndex = ReadBufferData<int32>(Buffer, P);
                Skeleton.Bones[i].BonePos         = ReadBufferVector3f(Buffer, P);
                Skeleton.Bones[i].BoneRot         = ReadBufferQuat(Buffer, P);
                Skeleton.Bones[i].BoneScale       = ReadBufferVector3f(Buffer, P);
            }
        }
        else if (Name == "SOCKETS")
        {
            const int32 Count = ReadBufferData<int32>(Buffer, P);
            Skeleton.Sockets.SetNum(Count);
            for (auto i = 0; i < Count; i++)
            {
                Skeleton.Sockets[i].SocketName       = ReadBufferFString(Buffer, P);
                Skeleton.Sockets[i].SocketParentName = ReadBufferFString(Buffer, P);
                Skeleton.Sockets[i].SocketPos        = ReadBufferVector3f(Buffer, P);
                Skeleton.Sockets[i].SocketRot        = ReadBufferQuat(Buffer, P);
                Skeleton.Sockets[i].SocketScale      = ReadBufferVector3f(Buffer, P);
            }
        }
        else if (Name == "VIRTUALBONES")
        {
            const int32 Count = ReadBufferData<int32>(Buffer, P);
            Skeleton.VirtualBones.SetNum(Count);
            for (auto i = 0; i < Count; i++)
            {
                Skeleton.VirtualBones[i].SourceBoneName  = ReadBufferFString(Buffer, P);
                Skeleton.VirtualBones[i].TargetBoneName  = ReadBufferFString(Buffer, P);
                Skeleton.VirtualBones[i].VirtualBoneName = ReadBufferFString(Buffer, P);
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Unknown skeleton attribute: %s"), UTF8_TO_TCHAR(Name.c_str()));
        }

        Offset = Start + ByteSize;
    }
}
