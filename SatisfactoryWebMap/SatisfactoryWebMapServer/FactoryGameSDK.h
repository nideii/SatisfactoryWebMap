#pragma once

#include <cstdint>
#include <string>
#include <cassert>
#include <iostream>
#include <vector>

#include <windows.h>

#pragma pack(1)

#define check(expr)					{ assert(expr); }
#define checkf(expr, format, ...)	{ assert(expr); }

#define NAME_NO_NUMBER_INTERNAL	0
#define NAME_INTERNAL_TO_EXTERNAL(x) (x - 1)
#define NAME_EXTERNAL_TO_INTERNAL(x) (x + 1)

struct TNameEntryArray;
struct FUObjectArray;

extern const TNameEntryArray *Names_0;
extern const FUObjectArray *GUObjectArray;

#define NAME_WIDE_MASK 0x1
#define NAME_INDEX_SHIFT 1

struct Vector3
{
	float x;
	float y;
	float z;

	std::vector<float> ToVec() const
	{
		return { x, y, z };
	}
};

std::ostream &operator<<(std::ostream &os, const Vector3 &vec)
{
	os << vec.x << "," << vec.y << "," << vec.z;
	return os;
}

struct FRotator
{
	float pitch;
	float yaw;
	float roll;

	std::vector<float> ToVec() const
	{
		return { pitch, yaw, roll };
	}
};

std::ostream &operator<<(std::ostream &os, const FRotator &rot)
{
	os << rot.pitch << "," << rot.yaw << "," << rot.roll;
	return os;
}

struct Vector4
{
	float x;
	float y;
	float z;
	float w;

	Vector3 ToVector3() const
	{
		return Vector3{ x, y, z };
	}

	operator Vector3() const
	{
		return ToVector3();
	}

	std::vector<float> ToVec() const
	{
		return { x, y, z, w };
	}
};

std::ostream &operator<<(std::ostream &os, const Vector4 &vec)
{
	os << vec.x << "," << vec.y << "," << vec.z << "," << vec.w;
	return os;
}

struct FNameEntry
{
	FNameEntry *HashNext; //0x0000
	int32_t Index; //0x0008
	char Name[2048]; //0x000C
	char pad_080C[4]; //0x080C

	FORCEINLINE int32_t GetIndex() const
	{
		return Index >> NAME_INDEX_SHIFT;
	}


	FORCEINLINE bool IsWide() const
	{
		return (Index & NAME_WIDE_MASK);
	}

	void AppendNameToString(std::string &Out) const
	{
		if (IsWide()) {
			Out = "**WIDESTR**";
		} else {
			Out += Name;
		}
	}

	std::string GetPlainNameString() const
	{
		if (IsWide()) {
			return "**WIDESTR**";
		} else {
			return Name;
		}
	}

}; //Size: 0x0810

static_assert(sizeof(FNameEntry) == 0x0810, "sizeof error");

struct TNameEntryArray
{
	enum
	{
		MaxTotalElements = 4 * 1024 * 1024,
		ElementsPerChunk = 16384,
		ChunkTableSize = (MaxTotalElements + ElementsPerChunk - 1) / ElementsPerChunk // 256
	};

	FNameEntry **Chunks[ChunkTableSize]; //0x0000
	int32_t NumElements; //0x0800
	int32_t NumChunks; //0x0804

	FORCEINLINE bool IsValidIndex(int32_t Index) const
	{
		return Index < NumElements &&Index >= 0;
	}

	FNameEntry const *const *GetItemPtr(int32_t Index) const
	{
		int32_t ChunkIndex = Index / ElementsPerChunk;
		int32_t WithinChunkIndex = Index % ElementsPerChunk;
		// checkf(IsValidIndex(Index), TEXT("IsValidIndex(%d)"), Index);
		checkf(ChunkIndex < NumChunks, TEXT("ChunkIndex (%d) < NumChunks (%d)"), ChunkIndex, NumChunks);
		checkf(Index < MaxTotalElements, TEXT("Index (%d) < MaxTotalElements (%d)"), Index, MaxTotalElements);
		FNameEntry **Chunk = Chunks[ChunkIndex];
		check(Chunk);
		return Chunk + WithinChunkIndex;
	}

	FORCEINLINE FNameEntry const *const &operator[](int32_t Index) const
	{
		FNameEntry const *const *ItemPtr = GetItemPtr(Index);
		check(ItemPtr);
		return *ItemPtr;
	}
}; //Size: 0x0808

static_assert(sizeof(TNameEntryArray) == 0x0808, "sizeof error");

struct FName
{
	int32_t ComparisonIndex; //0x0000
	int32_t Number; //0x0004

	auto GetNumber() const
	{
		return Number;
	}

	const FNameEntry *GetDisplayNameEntry() const
	{
		return (*Names_0)[ComparisonIndex];
	}

	std::string ToString() const
	{
		if (GetNumber() == NAME_NO_NUMBER_INTERNAL) {
			if (const FNameEntry *const DisplayEntry = GetDisplayNameEntry()) {
				// Avoids some extra allocations in non-number case
				return DisplayEntry->GetPlainNameString();
			}
		}

		std::string Out;
		ToString(Out);
		return Out;
	}

	void ToString(std::string &Out) const
	{
		// A version of ToString that saves at least one string copy
		const FNameEntry *const NameEntry = GetDisplayNameEntry();

		if (NameEntry == nullptr) {
			Out = "*INVALID*";
		} else if (GetNumber() == NAME_NO_NUMBER_INTERNAL) {
			NameEntry->AppendNameToString(Out);
		} else {
			NameEntry->AppendNameToString(Out);

			Out += '_';
			Out += std::to_string(NAME_INTERNAL_TO_EXTERNAL(GetNumber()));
		}
	}

	operator std::string() const {
		return ToString();
	}

	bool operator==(const std::string &other) const
	{
		return ToString() == other;
	}


}; //Size: 0x0008

std::ostream &operator<<(std::ostream &os, const FName &fname)
{
	os << fname.ToString();
	return os;
}

static_assert(sizeof(FName) == 0x0008, "sizeof error");


struct UObjectBase
{
	void *vfptr;
	int32_t ObjectFlags; //0x0008
	int32_t InternalIndex; //0x000C
	void *ClassPrivate; //0x0010
	FName NamePrivate; //0x0018
	UObjectBase *OuterPrivate; //0x0020
}; //Size: 0x0028

static_assert(sizeof(UObjectBase) == 0x0028, "sizeof error");

struct FUObjectItem
{
	UObjectBase *Object; //0x0000
	int32_t Flags; //0x0008
	int32_t ClusterRootIndex; //0x000C
	int32_t SerialNumber; //0x0010
	char pad_0014[4]; //0x0014
}; //Size: 0x0018

static_assert(sizeof(FUObjectItem) == 0x0018, "sizeof error");

struct FChunkedFixedUObjectArray
{
	enum
	{
		NumElementsPerChunk = 64 * 1024,
	};

	FUObjectItem **Objects; //0x0000
	void *PreAllocatedObjects; //0x0008
	int32_t MaxElements; //0x0010
	int32_t NumElements; //0x0014
	int32_t MaxChunks; //0x0018
	int32_t NumChunks; //0x001C

	FORCEINLINE bool IsValidIndex(int32_t Index) const
	{
		return Index < NumElements &&Index >= 0;
	}

	/**
	* Return a pointer to the pointer to a given element
	* @param Index The Index of an element we want to retrieve the pointer-to-pointer for
	**/
	FORCEINLINE FUObjectItem const *GetObjectPtr(int32_t Index) const
	{
		const int32_t ChunkIndex = Index / NumElementsPerChunk;
		const int32_t WithinChunkIndex = Index % NumElementsPerChunk;
		checkf(IsValidIndex(Index), TEXT("IsValidIndex(%d)"), Index);
		checkf(ChunkIndex < NumChunks, TEXT("ChunkIndex (%d) < NumChunks (%d)"), ChunkIndex, NumChunks);
		checkf(Index < MaxElements, TEXT("Index (%d) < MaxElements (%d)"), Index, MaxElements);
		FUObjectItem *Chunk = Objects[ChunkIndex];
		check(Chunk);
		return Chunk + WithinChunkIndex;
	}
	FORCEINLINE FUObjectItem *GetObjectPtr(int32_t Index)
	{
		const int32_t ChunkIndex = Index / NumElementsPerChunk;
		const int32_t WithinChunkIndex = Index % NumElementsPerChunk;
		checkf(IsValidIndex(Index), TEXT("IsValidIndex(%d)"), Index);
		checkf(ChunkIndex < NumChunks, TEXT("ChunkIndex (%d) < NumChunks (%d)"), ChunkIndex, NumChunks);
		checkf(Index < MaxElements, TEXT("Index (%d) < MaxElements (%d)"), Index, MaxElements);
		FUObjectItem *Chunk = Objects[ChunkIndex];
		check(Chunk);
		return Chunk + WithinChunkIndex;
	}

	/**
	* Return a reference to an element
	* @param	Index	Index to return
	* @return	a reference to the pointer to the element
	* Thread safe, if it is valid now, it is valid forever. This might return nullptr, but by then, some other thread might have made it non-nullptr.
	**/
	FORCEINLINE FUObjectItem const &operator[](int32_t Index) const
	{
		FUObjectItem const *ItemPtr = GetObjectPtr(Index);
		check(ItemPtr);
		return *ItemPtr;
	}
	FORCEINLINE FUObjectItem &operator[](int32_t Index)
	{
		FUObjectItem *ItemPtr = GetObjectPtr(Index);
		check(ItemPtr);
		return *ItemPtr;
	}
}; //Size: 0x0020

static_assert(sizeof(FChunkedFixedUObjectArray) == 0x0020, "sizeof error");

struct FUObjectArray
{
	int32_t ObjFirstGCIndex; //0x0000
	int32_t ObjLastNonGCIndex; //0x0004
	int32_t MaxObjectsNotConsideredByGC; //0x0008
	bool OpenForDisregardForGC; //0x000C
	uint8_t Padding1; //0x000D
	uint8_t Padding2; //0x000E
	uint8_t Padding3; //0x000F
	FChunkedFixedUObjectArray ObjObjects; //0x0010
	uint8_t Padding4[256]; //0x0030
}; //Size: 0x0130

static_assert(offsetof(FUObjectArray, ObjObjects) == 0x0010, "offset error");
static_assert(sizeof(FUObjectArray) == 0x0130, "sizeof error");

struct FLinearColor
{
	int32_t R;
	int32_t G;
	int32_t B;
	int32_t A;

	std::vector<int32_t> ToVec() const
	{
		return { R, G, B, A };
	}

}; //Size: 0x0010

static_assert(sizeof(FLinearColor) == 0x0010, "sizeof error");

struct FTransform
{
	Vector4 Rotation; //0x0000
	Vector4 Translation; //0x0010
	Vector4 Scale3D; //0x0020
}; //Size: 0x0030

static_assert(sizeof(FTransform) == 0x0030, "sizeof error");

struct USceneComponent
{
	char pad_0000[400]; //0x0000
	FTransform ComponentToWorld; //0x0190
	Vector3 ComponentVelocity; //0x01C0
	char pad_01CC[148]; //0x01CC
}; //Size: 0x0260

static_assert(sizeof(USceneComponent) == 0x0260, "sizeof error");

struct AActor : public UObjectBase
{
	char pad_0028[304]; //0x0028
	USceneComponent *RootComponent; //0x0158
	char pad_0180[464]; //0x0160
}; //Size: 0x0330

static_assert(sizeof(AActor) == 0x0330, "sizeof error");

struct FGActorRepresentation : public UObjectBase
{
	bool mIsLocal; //0x0028
	bool mIsOnClient; //0x0029
	char pad_002A[6]; //0x002A
	AActor *mRealActor; //0x0030
	Vector3 mActorLocation; //0x0038
	FRotator mActorRotation; //0x0044
	bool mIsStatic; //0x0050
	char pad_0051[7]; //0x0051
	void *mRepresentationTexture; //0x0058
	char mRepresentationText[0x18]; //0x0060
	FLinearColor mRepresentationColor; //0x0078
	int8_t mRepresentationType; //0x0088
	int8_t mFogOfWarRevealType; //0x0089
	char pad_008A[2]; //0x008A
	float mFogOfWarRevealRadius; //0x008C
	bool mIsTemporary; //0x0090
	char pad_0091[3]; //0x0091
	float mLifeTime; //0x0094
	bool mShouldShowInCompass; //0x0098
	bool mShouldShowOnMap; //0x0099
	bool mCompassViewDistance; //0x009A
	char pad_009B[5]; //0x009B
}; //Size: 0x00A0

static_assert(sizeof(FGActorRepresentation) == 0x00A0, "sizeof error");

template <typename T>
struct TArray
{
	T *Data;
	int32_t ArrayNum; //0x0008
	int32_t ArrayMax; //0x000C
};

struct FGActorRepresentationManager : public UObjectBase
{
	char pad_0028[888]; //0x0028
	TArray<FGActorRepresentation *> mReplicatedRepresentations; //0x03A0
	TArray<FGActorRepresentation *> mClientReplicatedRepresentations; //0x03B0
	TArray<FGActorRepresentation *> mLocalRepresentations; //0x03C0
}; //Size: 0x03D0

static_assert(sizeof(FGActorRepresentationManager) == 0x03D0, "sizeof error");

struct FGMapManager : public UObjectBase
{
	char pad_0028[896]; //0x0028
	FGActorRepresentationManager *mActorRepresentationManager; //0x03A8
	char pad_03A8[24]; //0x03B0
}; //Size: 0x03C8

static_assert(sizeof(FGMapManager) == 0x3C8, "sizeof error");

#pragma pack()
