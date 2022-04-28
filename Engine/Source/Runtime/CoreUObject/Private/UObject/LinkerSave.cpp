// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/LinkerSave.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Serialization/LargeMemoryWriter.h"
#include "UObject/Package.h"
#include "UObject/Class.h"
#include "Templates/Casts.h"
#include "UObject/LazyObjectPtr.h"
#include "UObject/SoftObjectPtr.h"
#include "Internationalization/TextPackageNamespaceUtil.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/UObjectThreadContext.h"
#include "UObject/UnrealType.h"
#include "HAL/PlatformStackWalk.h"

#if WITH_EDITORONLY_DATA
#include "DerivedDataValueId.h"
#include "IO/IoDispatcher.h"
#endif

/*----------------------------------------------------------------------------
	FLinkerSave.
----------------------------------------------------------------------------*/

/** A mapping of package name to generated script SHA keys */
TMap<FString, TArray<uint8> > FLinkerSave::PackagesToScriptSHAMap;

FLinkerSave::FLinkerSave(UPackage* InParent, const TCHAR* InFilename, bool bForceByteSwapping, bool bInSaveUnversioned)
:	FLinker(ELinkerType::Save, InParent)
,	Saver(nullptr)
{
	SetFilename(InFilename);

	if (FPlatformProperties::HasEditorOnlyData())
	{
		// Create file saver.
		Saver = IFileManager::Get().CreateFileWriter( InFilename, 0 );
		if( !Saver )
		{
			TCHAR LastErrorText[1024];
			uint32 LastError = FPlatformMisc::GetLastError();
			if (LastError != 0)
			{
				FPlatformMisc::GetSystemErrorMessage(LastErrorText, UE_ARRAY_COUNT(LastErrorText), LastError);
			}
			else
			{
				FCString::Strcpy(LastErrorText, TEXT("Unknown failure reason."));
			}
			UE_LOG(LogLinker, Error, TEXT("Error opening file '%s': %s"), InFilename, LastErrorText);
			return; // Caller must test this->Saver to detect the failure
		}

		UPackage* Package = dynamic_cast<UPackage*>(LinkerRoot);

		// Set main summary info.
		Summary.Tag = PACKAGE_FILE_TAG;
		Summary.SetToLatestFileVersions(bInSaveUnversioned);
		Summary.SavedByEngineVersion = FEngineVersion::Current();
		Summary.CompatibleWithEngineVersion = FEngineVersion::CompatibleWith();
		Summary.SetPackageFlags(Package ? Package->GetPackageFlags() : 0);

#if USE_STABLE_LOCALIZATION_KEYS
		if (GIsEditor)
		{
			Summary.LocalizationId = TextNamespaceUtil::GetPackageNamespace(LinkerRoot);
		}
#endif // USE_STABLE_LOCALIZATION_KEYS

		if (Package)
		{
#if WITH_EDITORONLY_DATA
			Summary.FolderName = Package->GetFolderName().ToString();
#endif
			Summary.ChunkIDs = Package->GetChunkIDs();
		}

		// Set status info.
		this->SetIsSaving(true);
		this->SetIsPersistent(true);
		ArForceByteSwapping		= bForceByteSwapping;

#if USE_STABLE_LOCALIZATION_KEYS
		if (GIsEditor)
		{
			SetLocalizationNamespace(Summary.LocalizationId);
		}
#endif // USE_STABLE_LOCALIZATION_KEYS
	}
}


FLinkerSave::FLinkerSave(UPackage* InParent, FArchive *InSaver, bool bForceByteSwapping, bool bInSaveUnversioned)
: FLinker(ELinkerType::Save, InParent)
, Saver(nullptr)
{
	SetFilename(TEXT("$$Memory$$"));
	if (FPlatformProperties::HasEditorOnlyData())
	{
		// Create file saver.
		Saver = InSaver;
		check(Saver);
#if WITH_EDITOR
		ArDebugSerializationFlags = Saver->ArDebugSerializationFlags;
#endif
		

		UPackage* Package = dynamic_cast<UPackage*>(LinkerRoot);

		// Set main summary info.
		Summary.Tag = PACKAGE_FILE_TAG;
		Summary.SetToLatestFileVersions(bInSaveUnversioned);
		Summary.SavedByEngineVersion = FEngineVersion::Current();
		Summary.CompatibleWithEngineVersion = FEngineVersion::CompatibleWith();
		Summary.SetPackageFlags(Package ? Package->GetPackageFlags() : 0);

#if USE_STABLE_LOCALIZATION_KEYS
		if (GIsEditor)
		{
			Summary.LocalizationId = TextNamespaceUtil::GetPackageNamespace(LinkerRoot);
		}
#endif // USE_STABLE_LOCALIZATION_KEYS

		if (Package)
		{
#if WITH_EDITORONLY_DATA
			Summary.FolderName = Package->GetFolderName().ToString();
#endif
			Summary.ChunkIDs = Package->GetChunkIDs();
		}

		// Set status info.
		this->SetIsSaving(true);
		this->SetIsPersistent(true);
		ArForceByteSwapping = bForceByteSwapping;

#if USE_STABLE_LOCALIZATION_KEYS
		if (GIsEditor)
		{
			SetLocalizationNamespace(Summary.LocalizationId);
		}
#endif // USE_STABLE_LOCALIZATION_KEYS
	}
}

FLinkerSave::FLinkerSave(UPackage* InParent, bool bForceByteSwapping, bool bInSaveUnversioned )
:	FLinker(ELinkerType::Save, InParent)
,	Saver(nullptr)
{
	SetFilename(TEXT("$$Memory$$"));
	if (FPlatformProperties::HasEditorOnlyData())
	{
		// Create file saver.
		Saver = new FLargeMemoryWriter( 0, false, *InParent->GetLoadedPath().GetDebugName() );
		check(Saver);

		UPackage* Package = dynamic_cast<UPackage*>(LinkerRoot);

		// Set main summary info.
		Summary.Tag = PACKAGE_FILE_TAG;
		Summary.SetToLatestFileVersions(bInSaveUnversioned);
		Summary.SavedByEngineVersion = FEngineVersion::Current();
		Summary.CompatibleWithEngineVersion = FEngineVersion::CompatibleWith();
		Summary.SetPackageFlags(Package ? Package->GetPackageFlags() : 0);

#if USE_STABLE_LOCALIZATION_KEYS
		if (GIsEditor)
		{
			Summary.LocalizationId = TextNamespaceUtil::GetPackageNamespace(LinkerRoot);
		}
#endif // USE_STABLE_LOCALIZATION_KEYS

		if (Package)
		{
#if WITH_EDITORONLY_DATA
			Summary.FolderName = Package->GetFolderName().ToString();
#endif
			Summary.ChunkIDs = Package->GetChunkIDs();
		}

		// Set status info.
		this->SetIsSaving(true);
		this->SetIsPersistent(true);
		ArForceByteSwapping		= bForceByteSwapping;

#if USE_STABLE_LOCALIZATION_KEYS
		if (GIsEditor)
		{
			SetLocalizationNamespace(Summary.LocalizationId);
		}
#endif // USE_STABLE_LOCALIZATION_KEYS
	}
}

bool FLinkerSave::CloseAndDestroySaver()
{
	bool bSuccess = true;
	if (Saver)
	{
		// first, do an explicit close to check for archive errors
		bSuccess = Saver->Close();
		// then, destroy it
		delete Saver;
	}
	Saver = nullptr;
	return bSuccess;
}

FLinkerSave::~FLinkerSave()
{
	CloseAndDestroySaver();
}

int32 FLinkerSave::MapName(FNameEntryId Id) const
{
	const int32* IndexPtr = NameIndices.Find(Id);

	if (IndexPtr)
	{
		return *IndexPtr;
	}

	return INDEX_NONE;
}

FPackageIndex FLinkerSave::MapObject( const UObject* Object ) const
{
	if (Object)
	{
		const FPackageIndex *Found = ObjectIndicesMap.Find(Object);
		if (Found)
		{
			if (IsCooking() && CurrentlySavingExport.IsExport() &&
				Object->GetOutermost()->GetFName() != GLongCoreUObjectPackageName && // We assume nothing in coreuobject ever loads assets in a constructor
				*Found != CurrentlySavingExport) // would be weird, but I can't be a dependency on myself
			{
				const FObjectExport& SavingExport = Exp(CurrentlySavingExport);
				bool bFoundDep = false;
				if (SavingExport.FirstExportDependency >= 0)
				{
					int32 NumDeps = SavingExport.CreateBeforeCreateDependencies + SavingExport.CreateBeforeSerializationDependencies + SavingExport.SerializationBeforeCreateDependencies + SavingExport.SerializationBeforeSerializationDependencies;
					for (int32 DepIndex = SavingExport.FirstExportDependency; DepIndex < SavingExport.FirstExportDependency + NumDeps; DepIndex++)
					{
						if (DepListForErrorChecking[DepIndex] == *Found)
						{
							bFoundDep = true;
						}
					}
				}
				if (!bFoundDep)
				{
					if (SavingExport.Object && SavingExport.Object->IsA(UClass::StaticClass()) && CastChecked<UClass>(SavingExport.Object)->GetDefaultObject() == Object)
					{
						bFoundDep = true; // the class is saving a ref to the CDO...which doesn't really work or do anything useful, but it isn't an error
					}
				}
				if (!bFoundDep)
				{
					UE_LOG(LogLinker, Fatal, TEXT("Attempt to map an object during save that was not listed as a dependency. Saving Export %d %s in %s. Missing Dep on %s %s."),
						CurrentlySavingExport.ForDebugging(), *SavingExport.ObjectName.ToString(), *GetArchiveName(),
						Found->IsExport() ? TEXT("Export") : TEXT("Import"), *ImpExp(*Found).ObjectName.ToString()
						);
				}
			}

			return *Found;
		}
	}
	return FPackageIndex();
}

void FLinkerSave::Seek( int64 InPos )
{
	Saver->Seek( InPos );
}

int64 FLinkerSave::Tell()
{
	return Saver->Tell();
}

void FLinkerSave::Serialize( void* V, int64 Length )
{
#if WITH_EDITOR
	Saver->ArDebugSerializationFlags = ArDebugSerializationFlags;
	Saver->SetSerializedPropertyChain(GetSerializedPropertyChain(), GetSerializedProperty());
#endif
	Saver->Serialize( V, Length );
}

void FLinkerSave::OnPostSave(const FPackagePath& PackagePath, FObjectPostSaveContext ObjectSaveContext)
{
	for (TUniqueFunction<void(const FPackagePath&, FObjectPostSaveContext)>& Callback : PostSaveCallbacks)
	{
		Callback(PackagePath, ObjectSaveContext);
	}

	PostSaveCallbacks.Empty();
}
	
FString FLinkerSave::GetDebugName() const
{
	return GetFilename();
}

const FString& FLinkerSave::GetFilename() const
{
	// When the deprecated Filename is removed from FLinker, add a separate Filename variable to FLinkerSave
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return Filename;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void FLinkerSave::SetFilename(FStringView InFilename)
{
	// When the deprecated Filename is removed from FLinker, add a separate Filename variable to FLinkerSave
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Filename = FString(InFilename);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

FString FLinkerSave::GetArchiveName() const
{
	return Saver->GetArchiveName();
}

FArchive& FLinkerSave::operator<<( FName& InName )
{
	int32 Save = MapName(InName.GetDisplayIndex());

	check(GetSerializeContext());

	bool bNameMapped = Save != INDEX_NONE;
	if (!bNameMapped)
	{
		// Set an error on the archive and record the error on the log output if one is set.
		SetCriticalError();
		FString ErrorMessage = FString::Printf(TEXT("Name \"%s\" is not mapped when saving %s (object: %s, property: %s)."),
			*InName.ToString(),
			*GetArchiveName(),
			*GetSerializeContext()->SerializedObject->GetFullName(),
			*GetFullNameSafe(GetSerializedProperty()));
		ensureMsgf(false, TEXT("%s"), *ErrorMessage);
		if (LogOutput)
		{
			LogOutput->Logf(ELogVerbosity::Error, TEXT("%s"), *ErrorMessage);
		}
	}

	if (!CurrentlySavingExport.IsNull())
	{
		if (Save >= Summary.NamesReferencedFromExportDataCount)
		{
			SetCriticalError();
			FString ErrorMessage = FString::Printf(TEXT("Name \"%s\" is referenced from an export but not mapped in the export data names region when saving %s (object: %s, property: %s)."),
				*InName.ToString(),
				*GetArchiveName(),
				*GetSerializeContext()->SerializedObject->GetFullName(),
				*GetFullNameSafe(GetSerializedProperty()));
			ensureMsgf(false, TEXT("%s"), *ErrorMessage);
			if (LogOutput)
			{
				LogOutput->Logf(ELogVerbosity::Error, TEXT("%s"), *ErrorMessage);
			}
		}
	}

	int32 Number = InName.GetNumber();
	FArchive& Ar = *this;
	return Ar << Save << Number;
}

FArchive& FLinkerSave::operator<<( UObject*& Obj )
{
	FPackageIndex Save;
	if (Obj)
	{
		Save = MapObject(Obj);
	}
	return *this << Save;
}

FArchive& FLinkerSave::operator<<(FLazyObjectPtr& LazyObjectPtr)
{
	FUniqueObjectGuid ID;
	ID = LazyObjectPtr.GetUniqueID();
	return *this << ID;
}
void FLinkerSave::SetSerializeContext(FUObjectSerializeContext* InLoadContext)
{
	SaveContext = InLoadContext;
	if (Saver)
	{
		Saver->SetSerializeContext(InLoadContext);
	}
}

FUObjectSerializeContext* FLinkerSave::GetSerializeContext()
{
	return SaveContext;
}

void FLinkerSave::UsingCustomVersion(const struct FGuid& Guid)
{
	FArchiveUObject::UsingCustomVersion(Guid);

	// Here we're going to try and dump the callstack that added a new custom version after package summary has been serialized
	if (Summary.GetCustomVersionContainer().GetVersion(Guid) == nullptr)
	{
		FCustomVersion RegisteredVersion = FCurrentCustomVersions::Get(Guid).GetValue();

		FString CustomVersionWarning = FString::Printf(TEXT("Unexpected custom version \"%s\" used after package %s summary has been serialized. Callstack:\n"),
			*RegisteredVersion.GetFriendlyName().ToString(), *LinkerRoot->GetName());

		const int32 MaxStackFrames = 100;
		uint64 StackFrames[MaxStackFrames];
		int32 NumStackFrames = FPlatformStackWalk::CaptureStackBackTrace(StackFrames, MaxStackFrames);

		// Convert the stack trace to text, ignore the first functions (ProgramCounterToHumanReadableString)
		const int32 IgnoreStackLinesCount = 1;		
		ANSICHAR Buffer[1024];
		const ANSICHAR* CutoffFunction = "UPackage::Save";
		for (int32 Idx = IgnoreStackLinesCount; Idx < NumStackFrames; Idx++)
		{			
			Buffer[0] = '\0';
			FPlatformStackWalk::ProgramCounterToHumanReadableString(Idx, StackFrames[Idx], Buffer, sizeof(Buffer));
			CustomVersionWarning += TEXT("\t");
			CustomVersionWarning += Buffer;
			CustomVersionWarning += "\n";
			if (FCStringAnsi::Strstr(Buffer, CutoffFunction))
			{
				// Anything below UPackage::Save is not interesting from the point of view of what we're trying to find
				break;
			}
		}

		UE_LOG(LogLinker, Warning, TEXT("%s"), *CustomVersionWarning);
	}
}

void FLinkerSave::SetUseUnversionedPropertySerialization(bool bInUseUnversioned)
{
	FArchiveUObject::SetUseUnversionedPropertySerialization(bInUseUnversioned);
	if (Saver)
	{
		Saver->SetUseUnversionedPropertySerialization(bInUseUnversioned);
	}
	if (bInUseUnversioned)
	{
		Summary.SetPackageFlags(Summary.GetPackageFlags() | PKG_UnversionedProperties);
		if (LinkerRoot)
		{
			LinkerRoot->SetPackageFlags(PKG_UnversionedProperties);
		}
	}
	else
	{
		Summary.SetPackageFlags(Summary.GetPackageFlags() & ~PKG_UnversionedProperties);
		if (LinkerRoot)
		{
			LinkerRoot->ClearPackageFlags(PKG_UnversionedProperties);
		}
	}
}

#if WITH_EDITORONLY_DATA

static FIoChunkId CreateDerivedDataChunkId(const FPackageId PackageId, const int32 ChunkIndex)
{
	checkf(ChunkIndex >= 0 && ChunkIndex < (1 << 24), TEXT("ChunkIndex %d is out of range."), ChunkIndex);

	// PackageId                 ChunkIndex Type
	// [00 01 02 03 04 05 06 07] [08 09 10] [11]
	uint8 Data[12]{};
	*reinterpret_cast<uint8*>(&Data[11]) = static_cast<uint8>(EIoChunkType::DerivedData);
	*reinterpret_cast<uint32*>(&Data[7]) = NETWORK_ORDER32(ChunkIndex);
	*reinterpret_cast<uint64*>(&Data[0]) = PackageId.Value();

	FIoChunkId ChunkId;
	ChunkId.Set(Data, 12);
	return ChunkId;
}

FIoChunkId FLinkerSave::AddDerivedData(const FCompressedBuffer& Data)
{
	UE_LOG(LogLinker, Warning, TEXT("Data will not be able to load because derived data is not saved yet."));
	return CreateDerivedDataChunkId(LinkerRoot->GetPackageId(), ++LastDerivedDataIndex);
}

FIoChunkId FLinkerSave::AddDerivedData(const UE::DerivedData::FCacheKey& Key, const UE::DerivedData::FValueId& ValueId)
{
	UE_LOG(LogLinker, Warning, TEXT("Data will not be able to load because derived data is not saved yet."));
	return CreateDerivedDataChunkId(LinkerRoot->GetPackageId(), ++LastDerivedDataIndex);
}

#endif // WITH_EDITORONLY_DATA
