// Copyright © 2025 Marcel K. All rights reserved.

#pragma once
#include <fstream>
#include "Readers/UEFModelReader.h"
#include "Containers/Array.h"
#include "Math/Quat.h"

struct FFloatKey
{
	int32 Frame;
	float FloatValue;
};
struct FVectorKey
{
	int32   Frame;
	FVector VectorValue;  // was FVector3f
};
struct FQuatKey
{
	int32 Frame;
	FQuat QuatValue;      // was FQuat4f
};
struct FCurve
{
	std::string     CurveName;
	TArray<FFloatKey> CurveKeys;
};
struct FTrack
{
	std::string          TrackName;
	TArray<FVectorKey>   TrackPosKeys;
	TArray<FQuatKey>     TrackRotKeys;
	TArray<FVectorKey>   TrackScaleKeys;
};

class UEFORMAT_API UEFAnimReader
{
public:
	UEFAnimReader(const FString Filename);
	~UEFAnimReader();
	
	bool Read();
	
	FUEFormatHeader Header;

	int32   NumFrames;
	float   FramesPerSecond;
	std::string RefPosePath;
	EAdditiveAnimationType  AdditiveAnimType;
	EAdditiveBasePoseType   RefPoseType;
	int32   RefFrameIndex;
	TArray<FTrack>  Tracks;
	TArray<FCurve>  Curves;

private:
	const std::string GMAGIC           = "UEFORMAT";
	const std::string GZIP             = "GZIP";
	const std::string ZSTD             = "ZSTD";
	const std::string ANIM_IDENTIFIER  = "UEANIM";
	
	std::ifstream Ar;
	void ReadBuffer(const char* Buffer, int32 BufferSize);
	void ReadAttributeAnim(const char* Buffer, int32 BufferSize); // version 10+
};
