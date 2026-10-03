// Copyright © 2025 Marcel K. All rights reserved.
#include "Readers/UEFAnimReader.h"
#include <vector>
#include <string>
#include <fstream>
#include <cstring>

#include "Readers/UEFModelReader.h"
#include "Misc/Compression.h"
#include "zstd.h"
UEFAnimReader::UEFAnimReader(const FString Filename)
{
	Ar.open(ToCStr(Filename), std::ios::binary);
}

UEFAnimReader::~UEFAnimReader()
{
	if (Ar.is_open())
		Ar.close();
}

bool UEFAnimReader::Read()
{
	const std::string Magic = ReadString(Ar, GMAGIC.length());
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
		Header.CompressionType  = ReadFString(Ar);
		Header.UncompressedSize = ReadData<int32>(Ar);
		Header.CompressedSize   = ReadData<int32>(Ar);

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

void UEFAnimReader::ReadBuffer(const char* Buffer, int32 BufferSize)
{
	if (Header.FileVersionBytes >= UEF_VERSION_ATTRIBUTE_FORMAT_RESTRUCTURE)
	{
		ReadAttributeAnim(Buffer, BufferSize);
		return;
	}

	int32 Offset = 0;

	while (Offset < BufferSize)
	{
		std::string ChunkName = ReadBufferFString(Buffer, Offset);
		int32 ArraySize       = ReadBufferData<int32>(Buffer, Offset);
		int32 ByteSize        = ReadBufferData<int32>(Buffer, Offset);

		if (ChunkName == "METADATA")
		{
			NumFrames        = ReadBufferData<int32>(Buffer, Offset);
			FramesPerSecond  = ReadBufferData<float>(Buffer, Offset);
			RefPosePath      = ReadBufferFString(Buffer, Offset);
			AdditiveAnimType = static_cast<EAdditiveAnimationType>(ReadBufferData<uint8>(Buffer, Offset));
			RefPoseType      = static_cast<EAdditiveBasePoseType>(ReadBufferData<uint8>(Buffer, Offset));
			RefFrameIndex    = ReadBufferData<int32>(Buffer, Offset);
		}
		else if (ChunkName == "TRACKS")
		{
			Tracks.SetNum(ArraySize);
			for (auto i = 0; i < ArraySize; i++)
			{
				Tracks[i].TrackName = ReadBufferFString(Buffer, Offset);

				const int32 PosArraySize = ReadBufferData<int32>(Buffer, Offset);
				Tracks[i].TrackPosKeys.SetNum(PosArraySize);
				for (auto j = 0; j < PosArraySize; j++)
				{
					Tracks[i].TrackPosKeys[j].Frame = ReadBufferData<int32>(Buffer, Offset);
					float X = ReadBufferData<float>(Buffer, Offset);
					float Y = ReadBufferData<float>(Buffer, Offset);
					float Z = ReadBufferData<float>(Buffer, Offset);
					Tracks[i].TrackPosKeys[j].VectorValue = FVector(X, Y, Z);
				}

				const int32 RotArraySize = ReadBufferData<int32>(Buffer, Offset);
				Tracks[i].TrackRotKeys.SetNum(RotArraySize);
				for (auto k = 0; k < RotArraySize; k++)
				{
					Tracks[i].TrackRotKeys[k].Frame     = ReadBufferData<int32>(Buffer, Offset);
					Tracks[i].TrackRotKeys[k].QuatValue = ReadBufferQuat(Buffer, Offset);
				}

				const int32 ScaleArraySize = ReadBufferData<int32>(Buffer, Offset);
				Tracks[i].TrackScaleKeys.SetNum(ScaleArraySize);
				for (auto l = 0; l < ScaleArraySize; l++)
				{
					Tracks[i].TrackScaleKeys[l].Frame = ReadBufferData<int32>(Buffer, Offset);
					float X = ReadBufferData<float>(Buffer, Offset);
					float Y = ReadBufferData<float>(Buffer, Offset);
					float Z = ReadBufferData<float>(Buffer, Offset);
					Tracks[i].TrackScaleKeys[l].VectorValue = FVector(X, Y, Z);
				}
			}
		}
		else if (ChunkName == "CURVES")
		{
			Curves.SetNum(ArraySize);
			for (auto i = 0; i < ArraySize; i++)
			{
				Curves[i].CurveName = ReadBufferFString(Buffer, Offset);
				const int32 KeyArraySize = ReadBufferData<int32>(Buffer, Offset);
				Curves[i].CurveKeys.SetNum(KeyArraySize);
				for (auto j = 0; j < KeyArraySize; j++)
				{
					Curves[i].CurveKeys[j].Frame      = ReadBufferData<int32>(Buffer, Offset);
					Curves[i].CurveKeys[j].FloatValue = ReadBufferData<float>(Buffer, Offset);
				}
			}
		}
		else
			Offset += ByteSize;
	}
}

// Version 10+: Count, then per attribute: Name, ByteSize, Payload. Arrays carry their own count.
void UEFAnimReader::ReadAttributeAnim(const char* Buffer, int32 BufferSize)
{
	int32 Offset = 0;
	const int32 AttributeCount = ReadBufferData<int32>(Buffer, Offset);

	for (int32 AttributeIndex = 0; AttributeIndex < AttributeCount && Offset < BufferSize; ++AttributeIndex)
	{
		std::string Name     = ReadBufferFString(Buffer, Offset);
		const int32 ByteSize = ReadBufferData<int32>(Buffer, Offset);
		const int32 Start    = Offset;
		int32 P              = Start;

		if (Name == "METADATA")
		{
			NumFrames        = ReadBufferData<int32>(Buffer, P);
			FramesPerSecond  = ReadBufferData<float>(Buffer, P);
			RefPosePath      = ReadBufferFString(Buffer, P);
			AdditiveAnimType = static_cast<EAdditiveAnimationType>(ReadBufferData<uint8>(Buffer, P));
			RefPoseType      = static_cast<EAdditiveBasePoseType>(ReadBufferData<uint8>(Buffer, P));
			RefFrameIndex    = ReadBufferData<int32>(Buffer, P);
		}
		else if (Name == "TRACKS")
		{
			const int32 TrackCount = ReadBufferData<int32>(Buffer, P);
			Tracks.SetNum(TrackCount);
			for (auto i = 0; i < TrackCount; i++)
			{
				Tracks[i].TrackName = ReadBufferFString(Buffer, P);

				const int32 PosArraySize = ReadBufferData<int32>(Buffer, P);
				Tracks[i].TrackPosKeys.SetNum(PosArraySize);
				for (auto j = 0; j < PosArraySize; j++)
				{
					Tracks[i].TrackPosKeys[j].Frame = ReadBufferData<int32>(Buffer, P);
					float X = ReadBufferData<float>(Buffer, P);
					float Y = ReadBufferData<float>(Buffer, P);
					float Z = ReadBufferData<float>(Buffer, P);
					Tracks[i].TrackPosKeys[j].VectorValue = FVector(X, Y, Z);
				}

				const int32 RotArraySize = ReadBufferData<int32>(Buffer, P);
				Tracks[i].TrackRotKeys.SetNum(RotArraySize);
				for (auto k = 0; k < RotArraySize; k++)
				{
					Tracks[i].TrackRotKeys[k].Frame     = ReadBufferData<int32>(Buffer, P);
					Tracks[i].TrackRotKeys[k].QuatValue = ReadBufferQuat(Buffer, P);
				}

				const int32 ScaleArraySize = ReadBufferData<int32>(Buffer, P);
				Tracks[i].TrackScaleKeys.SetNum(ScaleArraySize);
				for (auto l = 0; l < ScaleArraySize; l++)
				{
					Tracks[i].TrackScaleKeys[l].Frame = ReadBufferData<int32>(Buffer, P);
					float X = ReadBufferData<float>(Buffer, P);
					float Y = ReadBufferData<float>(Buffer, P);
					float Z = ReadBufferData<float>(Buffer, P);
					Tracks[i].TrackScaleKeys[l].VectorValue = FVector(X, Y, Z);
				}
			}
		}
		else if (Name == "CURVES")
		{
			const int32 CurveCount = ReadBufferData<int32>(Buffer, P);
			Curves.SetNum(CurveCount);
			for (auto i = 0; i < CurveCount; i++)
			{
				Curves[i].CurveName = ReadBufferFString(Buffer, P);
				const int32 KeyArraySize = ReadBufferData<int32>(Buffer, P);
				Curves[i].CurveKeys.SetNum(KeyArraySize);
				for (auto j = 0; j < KeyArraySize; j++)
				{
					Curves[i].CurveKeys[j].Frame      = ReadBufferData<int32>(Buffer, P);
					Curves[i].CurveKeys[j].FloatValue = ReadBufferData<float>(Buffer, P);
				}
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Unknown anim attribute: %s"), UTF8_TO_TCHAR(Name.c_str()));
		}

		Offset = Start + ByteSize;
	}
}
