// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AndroidTargetPlatform.inl: Implements the FAndroidTargetPlatform class.
=============================================================================*/


/* FAndroidTargetPlatform structors
 *****************************************************************************/

#include "AndroidTargetPlatform.h"
#include "CoreTypes.h"
#include "Misc/AssertionMacros.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "UObject/NameTypes.h"
#include "Logging/LogMacros.h"
#include "Stats/Stats.h"
#include "Serialization/Archive.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IAndroidDeviceDetectionModule.h"
#include "Interfaces/IAndroidDeviceDetection.h"
#include "Modules/ModuleManager.h"
#include "Misc/SecureHash.h"


#if WITH_ENGINE
#include "AudioCompressionSettings.h"
#include "Sound/SoundWave.h"
#endif

#define LOCTEXT_NAMESPACE "FAndroidTargetPlatform"

class Error;
class FAndroidTargetDevice;
class FConfigCacheIni;
class FModuleManager;
class FScopeLock;
class FStaticMeshLODSettings;
class FTargetDeviceId;
class FTSTicker;
class IAndroidDeviceDetectionModule;
class UTexture;
class UTextureLODSettings;
struct FAndroidDeviceInfo;
enum class ETargetPlatformFeatures;
template<typename TPlatformProperties> class TTargetPlatformBase;

namespace AndroidTexFormat
{
	// Compressed Texture Formats
	const static FName NameDXT1(TEXT("DXT1"));
	const static FName NameDXT3(TEXT("DXT3"));
	const static FName NameDXT5(TEXT("DXT5"));
	const static FName NameDXT5n(TEXT("DXT5n"));
	const static FName NameAutoDXT(TEXT("AutoDXT"));
	const static FName NameBC4(TEXT("BC4"));
	const static FName NameBC5(TEXT("BC5"));
	const static FName NameBC6H(TEXT("BC6H"));
	const static FName NameBC7(TEXT("BC7"));
	const static FName NameETC2_RGB(TEXT("ETC2_RGB"));
	const static FName NameETC2_RGBA(TEXT("ETC2_RGBA"));
	const static FName NameETC2_R11(TEXT("ETC2_R11"));
	const static FName NameAutoETC2(TEXT("AutoETC2"));
	const static FName NameASTC_4x4(TEXT("ASTC_4x4"));
	const static FName NameASTC_6x6(TEXT("ASTC_6x6"));
	const static FName NameASTC_8x8(TEXT("ASTC_8x8"));
	const static FName NameASTC_10x10(TEXT("ASTC_10x10"));
	const static FName NameASTC_12x12(TEXT("ASTC_12x12"));
	const static FName NameAutoASTC(TEXT("ASTC_RGBAuto"));
	
	// Uncompressed Texture Formats
	const static FName NameBGRA8(TEXT("BGRA8"));
	const static FName NameG8(TEXT("G8"));
	const static FName NameVU8(TEXT("VU8"));
	const static FName NameRGBA16F(TEXT("RGBA16F"));
	const static FName NameR16F(TEXT("R16F"));
	const static FName NameR5G6B5(TEXT("R5G6B5"));
	const static FName NameA1RGB555(TEXT("A1RGB555"));
	//A1RGB555 is mapped to RGB555A1, because OpenGL GL_RGB5_A1 only supports alpha on the lowest bit
	const static FName NameRGB555A1(TEXT("RGB555A1"));

	const static FName ASTCRemap[][2] =
	{
		// Default format:		ASTC format:
		{ NameDXT1,			FName(TEXT("ASTC_RGB"))		},
		{ NameDXT5,			FName(TEXT("ASTC_RGBA"))	},
		{ NameDXT5n,		FName(TEXT("ASTC_NormalAG"))},
		{ NameBC5,			FName(TEXT("ASTC_NormalRG"))},
		{ NameBC4,			NameETC2_R11				},
		{ NameBC6H,			FName(TEXT("ASTC_RGB"))		},
		{ NameBC7,			NameAutoASTC				},
		{ NameAutoDXT,		NameAutoASTC				},
		{ NameA1RGB555,		NameRGB555A1				}
	};

	const static FName ETCRemap[][2] =
	{
		// Default format:	ETC2 format:
		{ NameDXT1,			NameETC2_RGB	},
		{ NameDXT5,			NameETC2_RGBA	},
		{ NameDXT5n,		NameETC2_RGB	},
		{ NameBC5,			NameETC2_RGB	},
		{ NameBC4,			NameETC2_R11	},
		{ NameBC6H,			NameETC2_RGB	}, // @todo Oodle : uncompressed float?
		{ NameBC7,			NameAutoETC2	},
		{ NameAutoDXT,		NameAutoETC2	},
		{ NameA1RGB555,		NameRGB555A1	}
	};
}

static FString GetLicensePath()
{
	auto &AndroidDeviceDetection = FModuleManager::LoadModuleChecked<IAndroidDeviceDetectionModule>("AndroidDeviceDetection");
	IAndroidDeviceDetection* DeviceDetection = AndroidDeviceDetection.GetAndroidDeviceDetection();
	FString ADBPath = DeviceDetection->GetADBPath();

	if (!FPaths::FileExists(ADBPath))
	{
		return TEXT("");
	}

	// strip off the adb.exe part
	FString PlatformToolsPath;
	FString Filename;
	FString Extension;
	FPaths::Split(ADBPath, PlatformToolsPath, Filename, Extension);

	// remove the platform-tools part and point to licenses
	FPaths::NormalizeDirectoryName(PlatformToolsPath);
	FString LicensePath = PlatformToolsPath + "/../licenses";
	FPaths::CollapseRelativeDirectories(LicensePath);

	return LicensePath;
}

#if WITH_ENGINE
static bool GetLicenseHash(FSHAHash& LicenseHash)
{
	bool bLicenseValid = false;

	// from Android SDK Tools 25.2.3
	FString LicenseFilename = FPaths::EngineDir() + TEXT("Source/ThirdParty/Android/package.xml");

	// Create file reader
	TUniquePtr<FArchive> FileReader(IFileManager::Get().CreateFileReader(*LicenseFilename));
	if (FileReader)
	{
		// Create buffer for file input
		uint32 BufferSize = FileReader->TotalSize();
		uint8* Buffer = (uint8*)FMemory::Malloc(BufferSize);
		FileReader->Serialize(Buffer, BufferSize);

		uint8 StartPattern[] = "<license id=\"android-sdk-license\" type=\"text\">";
		int32 StartPatternLength = strlen((char *)StartPattern);

		uint8* LicenseStart = Buffer;
		uint8* BufferEnd = Buffer + BufferSize - StartPatternLength;
		while (LicenseStart < BufferEnd)
		{
			if (!memcmp(LicenseStart, StartPattern, StartPatternLength))
			{
				break;
			}
			LicenseStart++;
		}

		if (LicenseStart < BufferEnd)
		{
			LicenseStart += StartPatternLength;

			uint8 EndPattern[] = "</license>";
			int32 EndPatternLength = strlen((char *)EndPattern);

			uint8* LicenseEnd = LicenseStart;
			BufferEnd = Buffer + BufferSize - EndPatternLength;
			while (LicenseEnd < BufferEnd)
			{
				if (!memcmp(LicenseEnd, EndPattern, EndPatternLength))
				{
					break;
				}
				LicenseEnd++;
			}

			if (LicenseEnd < BufferEnd)
			{
				int32 LicenseLength = LicenseEnd - LicenseStart;
				FSHA1::HashBuffer(LicenseStart, LicenseLength, LicenseHash.Hash);
				bLicenseValid = true;
			}
		}
		FMemory::Free(Buffer);
	}

	return bLicenseValid;
}
#endif

static bool HasLicense()
{
#if WITH_ENGINE
	FString LicensePath = GetLicensePath();

	if (LicensePath.IsEmpty())
	{
		return false;
	}

	// directory must exist
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*LicensePath))
	{
		return false;
	}

	// license file must exist
	FString LicenseFilename = LicensePath + "/android-sdk-license";
	if (!PlatformFile.FileExists(*LicenseFilename))
	{
		return false;
	}

	FSHAHash LicenseHash;
	if (!GetLicenseHash(LicenseHash))
	{
		return false;
	}

	// contents must match hash of license text
	FString FileData = "";
	FFileHelper::LoadFileToString(FileData, *LicenseFilename);
	TArray<FString> lines;
	int32 lineCount = FileData.ParseIntoArray(lines, TEXT("\n"), true);

	FString LicenseString = LicenseHash.ToString().ToLower();
	for (FString &line : lines)
	{
		if (line.TrimStartAndEnd().Equals(LicenseString))
		{
			return true;
		}
	}
#endif

	// doesn't match
	return false;
}

FAndroidTargetPlatform::FAndroidTargetPlatform(bool bInIsClient, const TCHAR* FlavorName, const TCHAR* OverrideIniPlatformName)
	: TNonDesktopTargetPlatformBase(bInIsClient, FlavorName, OverrideIniPlatformName)
	, DeviceDetection(nullptr)
	, bDistanceField(false)

{
#if WITH_ENGINE
	TextureLODSettings = nullptr; // These are registered by the device profile system.
	StaticMeshLODSettings.Initialize(this);
	GetConfigSystem()->GetBool(TEXT("/Script/Engine.RendererSettings"), TEXT("r.DistanceFields"), bDistanceField, GEngineIni);
#endif

	TickDelegate = FTickerDelegate::CreateRaw(this, &FAndroidTargetPlatform::HandleTicker);
	TickDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(TickDelegate, 4.0f);
}


FAndroidTargetPlatform::~FAndroidTargetPlatform()
{
	 FTSTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
}

FAndroidTargetDevicePtr FAndroidTargetPlatform::CreateTargetDevice(const ITargetPlatform& InTargetPlatform, const FString& InSerialNumber, const FString& InAndroidVariant) const
{
	return MakeShareable(new FAndroidTargetDevice(InTargetPlatform, InSerialNumber, InAndroidVariant));
}

static bool UsesVirtualTextures()
{
	static auto* CVarMobileVirtualTextures = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.VirtualTextures"));
	return CVarMobileVirtualTextures->GetValueOnAnyThread() != 0;
}

bool FAndroidTargetPlatform::SupportsES31() const
{
	// default no support for ES31
	bool bBuildForES31 = false;
#if WITH_ENGINE
	GConfig->GetBool(TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings"), TEXT("bBuildForES31"), bBuildForES31, GEngineIni);
#endif
	return bBuildForES31;
}

bool FAndroidTargetPlatform::SupportsVulkan() const
{
	// default to not supporting Vulkan
	bool bSupportsVulkan = false;
#if WITH_ENGINE
	GConfig->GetBool(TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings"), TEXT("bSupportsVulkan"), bSupportsVulkan, GEngineIni);
#endif
	return bSupportsVulkan;
}

bool FAndroidTargetPlatform::SupportsVulkanSM5() const
{
	// default to no support for VulkanSM5
	bool bSupportsMobileVulkanSM5 = false;
#if WITH_ENGINE
	GConfig->GetBool(TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings"), TEXT("bSupportsVulkanSM5"), bSupportsMobileVulkanSM5, GEngineIni);
#endif
	return bSupportsMobileVulkanSM5;
}

bool FAndroidTargetPlatform::SupportsLandscapeMeshLODStreaming() const
{
	bool bStreamLandscapeMeshLODs = false;
#if WITH_ENGINE
	GConfig->GetBool(TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings"), TEXT("bStreamLandscapeMeshLODs"), bStreamLandscapeMeshLODs, GEngineIni);
#endif
	return bStreamLandscapeMeshLODs;
}

bool FAndroidTargetPlatform::UseMobileLandscapeMesh() const
{
	// By default mobile uses landscape mesh
	static bool bUseMobileLandscapeMesh = true;
	static bool bInitialized = false;
	if (!bInitialized)
	{
		GetConfigSystem()->GetBool(TEXT("/Script/Engine.RendererSettings"), TEXT("r.Mobile.LandscapeMesh"), bUseMobileLandscapeMesh, GEngineIni);
		bInitialized = true;
	}
	return bUseMobileLandscapeMesh;
}

/* ITargetPlatform overrides
 *****************************************************************************/

void FAndroidTargetPlatform::GetAllDevices( TArray<ITargetDevicePtr>& OutDevices ) const
{
	OutDevices.Reset();

	for (auto Iter = Devices.CreateConstIterator(); Iter; ++Iter)
	{
		OutDevices.Add(Iter.Value());
	}
}

ITargetDevicePtr FAndroidTargetPlatform::GetDefaultDevice( ) const
{
	// return the first device in the list
	if (Devices.Num() > 0)
	{
		auto Iter = Devices.CreateConstIterator();
		if (Iter)
		{
			return Iter.Value();
		}
	}

	return nullptr;
}

ITargetDevicePtr FAndroidTargetPlatform::GetDevice( const FTargetDeviceId& DeviceId )
{
	if (DeviceId.GetPlatformName() == PlatformName())
	{
		return Devices.FindRef(DeviceId.GetDeviceName());
	}

	return nullptr;
}


bool FAndroidTargetPlatform::IsSdkInstalled(bool bProjectHasCode, FString& OutDocumentationPath) const
{
	OutDocumentationPath = FString("Shared/Tutorials/SettingUpAndroidTutorial");
	return true;
}

int32 FAndroidTargetPlatform::CheckRequirements(bool bProjectHasCode, EBuildConfiguration Configuration, bool bRequiresAssetNativization, FString& OutTutorialPath, FString& OutDocumentationPath, FText& CustomizedLogMessage) const
{
	OutDocumentationPath = TEXT("Platforms/Android/GettingStarted");

	int32 bReadyToBuild = ETargetPlatformReadyStatus::Ready;
	if (!IsSdkInstalled(bProjectHasCode, OutTutorialPath))
	{
		bReadyToBuild |= ETargetPlatformReadyStatus::SDKNotFound;
	}

	bool bEnableGradle;
	GConfig->GetBool(TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings"), TEXT("bEnableGradle"), bEnableGradle, GEngineIni);

	if (bEnableGradle)
	{
		// need to check license was accepted
		if (!HasLicense())
		{
			OutTutorialPath.Empty();
			CustomizedLogMessage = LOCTEXT("AndroidLicenseNotAcceptedMessageDetail", "SDK License must be accepted in the Android project settings to deploy your app to the device.");
			bReadyToBuild |= ETargetPlatformReadyStatus::LicenseNotAccepted;
		}
	}

	return bReadyToBuild;
}

bool FAndroidTargetPlatform::SupportsFeature( ETargetPlatformFeatures Feature ) const
{
	switch (Feature)
	{
		case ETargetPlatformFeatures::Packaging:
		case ETargetPlatformFeatures::DeviceOutputLog:
			return true;

		case ETargetPlatformFeatures::LowQualityLightmaps:
		case ETargetPlatformFeatures::MobileRendering:
			return SupportsES31() || SupportsVulkan();

		case ETargetPlatformFeatures::HighQualityLightmaps:
		case ETargetPlatformFeatures::DeferredRendering:
			return SupportsVulkanSM5();

		case ETargetPlatformFeatures::VirtualTextureStreaming:
			return UsesVirtualTextures();

		case ETargetPlatformFeatures::LandscapeMeshLODStreaming:
			return SupportsLandscapeMeshLODStreaming();

		case ETargetPlatformFeatures::DistanceFieldAO:
			return UsesDistanceFields();

		case ETargetPlatformFeatures::MobileLandscapeMesh:
			return SupportsFeature(ETargetPlatformFeatures::MobileRendering) && UseMobileLandscapeMesh();
			
		default:
			break;
	}

	return TTargetPlatformBase<FAndroidPlatformProperties>::SupportsFeature(Feature);
}


void FAndroidTargetPlatform::GetAllPossibleShaderFormats( TArray<FName>& OutFormats ) const
{
	static FName NAME_SF_VULKAN_ES31_ANDROID(TEXT("SF_VULKAN_ES31_ANDROID"));
	static FName NAME_GLSL_ES3_1_ANDROID(TEXT("GLSL_ES3_1_ANDROID"));
	static FName NAME_SF_VULKAN_SM5_ANDROID(TEXT("SF_VULKAN_SM5_ANDROID"));

	if (SupportsVulkan())
	{
		OutFormats.AddUnique(NAME_SF_VULKAN_ES31_ANDROID);	
	}

	if (SupportsVulkanSM5())
	{
		OutFormats.AddUnique(NAME_SF_VULKAN_SM5_ANDROID);
	}

	if (SupportsES31())
	{
		OutFormats.AddUnique(NAME_GLSL_ES3_1_ANDROID);
	}
}

void FAndroidTargetPlatform::GetAllTargetedShaderFormats( TArray<FName>& OutFormats ) const
{
	GetAllPossibleShaderFormats(OutFormats);
}


#if WITH_ENGINE

const FStaticMeshLODSettings& FAndroidTargetPlatform::GetStaticMeshLODSettings( ) const
{
	return StaticMeshLODSettings;
}

void FAndroidTargetPlatform::GetTextureFormats( const UTexture* Texture, TArray< TArray<FName> >& OutFormats) const
{
	check(Texture);
	
	// Supported in ES3.2 with ASTC
	const bool bSupportCompressedVolumeTexture = SupportsTextureFormatCategory(EAndroidTextureFormatCategory::ASTC);
	// TODO: compressed HDR formats
	const bool bSupportDX11TextureFormats = false;

	TArray<FName>& LayerFormats = OutFormats.AddDefaulted_GetRef();
	GetDefaultTextureFormatNamePerLayer(LayerFormats, this, Texture, bSupportDX11TextureFormats, bSupportCompressedVolumeTexture, 1);

	for (FName& TextureFormatName : LayerFormats)
	{
		if (Texture->LODGroup == TEXTUREGROUP_Shadowmap)
		{
			// forward rendering only needs one channel for shadow maps
			TextureFormatName = FName(TEXT("G8"));
		}

		if (Texture->IsA(UTextureCube::StaticClass()))
		{
			const UTextureCube* Cube = CastChecked<UTextureCube>(Texture);
			if (Cube != nullptr)
			{
				FTextureFormatSettings FormatSettings;
				Cube->GetDefaultFormatSettings(FormatSettings);
				if (FormatSettings.CompressionSettings == TC_EncodedReflectionCapture && !FormatSettings.CompressionNone)
				{
					TextureFormatName = FName(TEXT("ETC2_RGBA"));
				}
			}
		}
	}
}

FName FAndroidTargetPlatform::FinalizeVirtualTextureLayerFormat(FName Format) const
{
#if WITH_EDITOR
	// Remap non-ETC variants to ETC

	// VirtualTexture Format was already run through the ordinary texture remaps to change AutoDXT to ASTC or ETC
	// this then runs again
	// currently it forces all ASTC to ETC
	// this is needed because the runtime virtual texture encoder only supports ETC

	const static FName VTRemap[][2] =
	{
		{ { FName(TEXT("ASTC_RGB")) },			{ AndroidTexFormat::NameETC2_RGB } },
		{ { FName(TEXT("ASTC_RGBA")) },			{ AndroidTexFormat::NameETC2_RGBA } },
		{ { FName(TEXT("ASTC_RGBAuto")) },		{ AndroidTexFormat::NameAutoETC2 } },
		{ { FName(TEXT("ASTC_NormalAG")) },		{ AndroidTexFormat::NameETC2_RGB } },
		{ { FName(TEXT("ASTC_NormalRG")) },		{ AndroidTexFormat::NameETC2_RGB } },
		{ { AndroidTexFormat::NameDXT1 },		{ AndroidTexFormat::NameETC2_RGB } },
		{ { AndroidTexFormat::NameDXT5 },		{ AndroidTexFormat::NameAutoETC2 } },
		{ { AndroidTexFormat::NameAutoDXT },	{ AndroidTexFormat::NameAutoETC2 } }
	};

	for (int32 RemapIndex = 0; RemapIndex < UE_ARRAY_COUNT(VTRemap); RemapIndex++)
	{
		if (VTRemap[RemapIndex][0] == Format)
		{
			return VTRemap[RemapIndex][1];
		}
	}
#endif
	return Format;
}

void FAndroidTargetPlatform::GetAllTextureFormats(TArray<FName>& OutFormats) const
{
	GetAllDefaultTextureFormats(this, OutFormats, false);
}

void FAndroid_ASTCTargetPlatform::GetAllTextureFormats(TArray<FName>& OutFormats) const
{
	GetAllDefaultTextureFormats(this, OutFormats, false);

	// not supported
	OutFormats.Remove(AndroidTexFormat::NameDXT3);
	for (int32 RemapIndex = 0; RemapIndex < UE_ARRAY_COUNT(AndroidTexFormat::ASTCRemap); ++RemapIndex)
	{
		OutFormats.Remove(AndroidTexFormat::ASTCRemap[RemapIndex][0]);
	}
	
	// ASTC for compressed textures
	OutFormats.Add(AndroidTexFormat::NameAutoASTC);
	// ETC for ETC2_R11
	OutFormats.Add(AndroidTexFormat::NameAutoETC2);
}

void FAndroid_ASTCTargetPlatform::GetTextureFormats(const UTexture* Texture, TArray< TArray<FName> >& OutFormats) const
{
	FAndroidTargetPlatform::GetTextureFormats(Texture, OutFormats);

	// perform any remapping away from defaults
	TArray<FName>& LayerFormats = OutFormats.Last();
	for (FName& TextureFormatName : LayerFormats)
	{
		for (int32 RemapIndex = 0; RemapIndex < UE_ARRAY_COUNT(AndroidTexFormat::ASTCRemap); ++RemapIndex)
		{
			if (TextureFormatName == AndroidTexFormat::ASTCRemap[RemapIndex][0])
			{
				TextureFormatName = AndroidTexFormat::ASTCRemap[RemapIndex][1];
				break;
			}
		}
	}
}

void FAndroid_ETC2TargetPlatform::GetAllTextureFormats(TArray<FName>& OutFormats) const
{
	GetAllDefaultTextureFormats(this, OutFormats, false);

	// not supported
	OutFormats.Remove(AndroidTexFormat::NameDXT3);
	for (int32 RemapIndex = 0; RemapIndex < UE_ARRAY_COUNT(AndroidTexFormat::ETCRemap); ++RemapIndex)
	{
		OutFormats.Remove(AndroidTexFormat::ETCRemap[RemapIndex][0]);
	}
	
	// support only ETC for compressed textures
	OutFormats.Add(AndroidTexFormat::NameAutoETC2);
}

void FAndroid_ETC2TargetPlatform::GetTextureFormats(const UTexture* Texture, TArray< TArray<FName> >& OutFormats) const
{
	FAndroidTargetPlatform::GetTextureFormats(Texture, OutFormats);

	// perform any remapping away from defaults
	TArray<FName>& LayerFormats = OutFormats.Last();
	for (FName& TextureFormatName : LayerFormats)
	{
		for (int32 RemapIndex = 0; RemapIndex < UE_ARRAY_COUNT(AndroidTexFormat::ETCRemap); ++RemapIndex)
		{
			if (TextureFormatName == AndroidTexFormat::ETCRemap[RemapIndex][0])
			{
				TextureFormatName = AndroidTexFormat::ETCRemap[RemapIndex][1];
				break;
			}
		}
	}
}

void FAndroidTargetPlatform::GetReflectionCaptureFormats( TArray<FName>& OutFormats ) const
{
	static auto* MobileShadingPathCvar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.ShadingPath"));
	const bool bMobileDeferredShading = (MobileShadingPathCvar->GetValueOnAnyThread() == 1);
	
	if (SupportsVulkanSM5() || bMobileDeferredShading)
	{
		// use Full HDR with SM5 and Mobile Deferred
		OutFormats.Add(FName(TEXT("FullHDR")));
	}

	// always emit encoded
	OutFormats.Add(FName(TEXT("EncodedHDR")));
}

const UTextureLODSettings& FAndroidTargetPlatform::GetTextureLODSettings() const
{
	return *TextureLODSettings;
}


FName FAndroidTargetPlatform::GetWaveFormat(const class USoundWave* Wave) const
{
	FName FormatName = Audio::ToName(Wave->GetSoundAssetCompressionType());
	if (FormatName == Audio::NAME_PLATFORM_SPECIFIC)
	{
		FormatName = Audio::NAME_OGG;
	}
	return FormatName;
}


void FAndroidTargetPlatform::GetAllWaveFormats(TArray<FName>& OutFormats) const
{

	OutFormats.Add(Audio::NAME_BINKA);
	OutFormats.Add(Audio::NAME_OGG);
	OutFormats.Add(Audio::NAME_PCM);
	OutFormats.Add(Audio::NAME_ADPCM);
}

#endif //WITH_ENGINE

bool FAndroidTargetPlatform::SupportsVariants() const
{
	return true;
}


/* FAndroidTargetPlatform implementation
 *****************************************************************************/
void FAndroidTargetPlatform::InitializeDeviceDetection()
{
	DeviceDetection = FModuleManager::LoadModuleChecked<IAndroidDeviceDetectionModule>("AndroidDeviceDetection").GetAndroidDeviceDetection();
	DeviceDetection->Initialize(TEXT("ANDROID_HOME"),
#if PLATFORM_WINDOWS
		TEXT("platform-tools\\adb.exe"),
#else
		TEXT("platform-tools/adb"),
#endif
		TEXT("shell getprop"), true);
}

bool FAndroidTargetPlatform::ShouldExpandTo32Bit(const uint16* Indices, const int32 NumIndices) const
{
	bool bIsMaliBugIndex = false;
	const uint16 MaliBugIndexMaxDiff = 16;
	uint16 LastIndex = Indices[0];
	for (int32 i = 1; i < NumIndices; ++i)
	{
		uint16 CurrentIndex = Indices[i];
		if ((FMath::Abs(LastIndex - CurrentIndex) > MaliBugIndexMaxDiff))
		{
			bIsMaliBugIndex = true;
			break;
		}
		else
		{
			LastIndex = CurrentIndex;
		}
	}
	return bIsMaliBugIndex;
}

/* FAndroidTargetPlatform callbacks
 *****************************************************************************/

bool FAndroidTargetPlatform::HandleTicker( float DeltaTime )
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FAndroidTargetPlatform_HandleTicker);

	if (DeviceDetection == nullptr)
	{
		InitializeDeviceDetection();
		checkf(DeviceDetection != nullptr, TEXT("A target platform didn't create a device detection object in InitializeDeviceDetection()!"));
	}

	TArray<FString> ConnectedDeviceIds;

	{
		FScopeLock ScopeLock(DeviceDetection->GetDeviceMapLock());

		auto DeviceIt = DeviceDetection->GetDeviceMap().CreateConstIterator();

		for (; DeviceIt; ++DeviceIt)
		{
			ConnectedDeviceIds.Add(DeviceIt.Key());

			const FAndroidDeviceInfo& DeviceInfo = DeviceIt.Value();

			// see if this device is already known
			if (Devices.Contains(DeviceIt.Key()))
			{
				FAndroidTargetDevicePtr TestDevice = Devices[DeviceIt.Key()];

				// ignore if authorization didn't change
				if (DeviceInfo.bAuthorizedDevice == TestDevice->IsAuthorized())
				{
					continue;
				}

				// remove it to add again
				TestDevice->SetConnected(false);
				Devices.Remove(DeviceIt.Key());

				OnDeviceLost().Broadcast(TestDevice.ToSharedRef());
			}

			// check if this platform is supported by the extensions and version
			if (!SupportedByExtensionsString(DeviceInfo.GLESExtensions, DeviceInfo.GLESVersion))
			{
				continue;
			}

			// create target device
			FAndroidTargetDevicePtr& Device = Devices.Add(DeviceInfo.SerialNumber);

			Device = CreateTargetDevice(*this, DeviceInfo.SerialNumber, GetAndroidVariantName());

			Device->SetConnected(true);
			Device->SetModel(DeviceInfo.Model);
			Device->SetDeviceName(DeviceInfo.DeviceName);
			Device->SetAuthorized(DeviceInfo.bAuthorizedDevice);
			Device->SetVersions(DeviceInfo.SDKVersion, DeviceInfo.HumanAndroidVersion);

			OnDeviceDiscovered().Broadcast(Device.ToSharedRef());
		}
	}

	// remove disconnected devices
	for (auto Iter = Devices.CreateIterator(); Iter; ++Iter)
	{
		if (!ConnectedDeviceIds.Contains(Iter.Key()))
		{
			FAndroidTargetDevicePtr RemovedDevice = Iter.Value();
			RemovedDevice->SetConnected(false);

			Iter.RemoveCurrent();

			OnDeviceLost().Broadcast(RemovedDevice.ToSharedRef());
		}
	}

	return true;
}

FAndroidTargetDeviceRef FAndroidTargetPlatform::CreateNewDevice(const FAndroidDeviceInfo &DeviceInfo)
{
	return MakeShareable(new FAndroidTargetDevice(*this, DeviceInfo.SerialNumber, GetAndroidVariantName()));
}

#undef LOCTEXT_NAMESPACE