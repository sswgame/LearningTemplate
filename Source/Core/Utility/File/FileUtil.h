#pragma once
/**
 * @file FileUtil.h
 * @brief Auto-generated documentation header
 */

#include "Core/Common/Types.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Utility/Delegate/Delegate.h"

namespace sw
{

	struct FileDialogParams
	{
		enum class Type : uint8
		{
			Open,
			Save,
		};

		Type					 _type = Type::Open;
		std::string				 _description;
		std::vector<std::string> _filterExtensionList;
		bool					 _bEnableMultiselect = true;
	};

	SW_DECLARE_DELEGATE( void, FileDialogDelegate, const std::vector<std::string>& fileName );

	struct SW_API FileUtil
	{
		static std::string getCurrentDateTimeAsString();

		static void		   splitPath( const std::string_view fullPath, std::string& outDirectoryPath, std::string& outFileName );
		static std::string getFileNamePart( const std::string_view fullPath );
		static std::string getDirectoryPart( const std::string_view fullPath );
		static void		   getExtensionPart( const std::string_view fileName, std::vector<std::string>& outPartList );
		static std::string replaceExtension( const std::string_view fileName, const std::string_view extension );
		static std::string forceExtension( const std::string_view fileName, const std::string_view extension );
		static std::string removeExtension( const std::string_view fileName );

		static bool		   makePathRelative( const std::string_view rootDir, const std::string_view path, std::string& outResult );
		static bool		   makePathAbsolute( const std::string_view path, std::string& outResult );
		static std::string normalizePath( const std::string_view path );
		static void		   createDirectory( const std::string_view path );
		static bool		   isFileExist( const std::string_view fileName );
		static bool		   isDirectoryExist( const std::string_view fileName );
		static std::string getCurrentPath();
		static std::string getExecutablePath();

		static uint64 getFileTimestamp( const std::string_view fileName );
		static uint32 getFileSize( const std::string_view fileName );
		static bool	  copyFile( const std::string_view source, const std::string_view destination );
		static bool	  writeFile( const std::string_view fileName, const uint8* data, uint64 size );
		static bool	  readFile( const std::string_view fileName, std::vector<uint8>& outData, uint32 offset = 0, uint32 maxReadCount = invalid_index::kUint32 );

		static void openFileDialog( const FileDialogParams& params, FileDialogDelegate onSuccess );
		static bool collectFiles( const std::string_view directory, const std::string_view filterExtension, std::vector<std::string>& outFilePathList, bool bRecursive, bool bNormalizePath = true );
		static bool collectFolders( const std::string_view directory, std::vector<std::string>& outFolderList, bool bRecursive, bool bNormalizePath = true );

		static std::string getClipboardText();
		static bool		   setClipboardText( const std::string& str );

		static std::string_view getSharedLibraryPrefix();
		static std::string_view getSharedLibraryExtension();
		static std::string		formatSharedLibraryName( const std::string& baseName );
		static std::string		getDebugSymbolPath( const std::string_view libraryPath );

		static void* loadDynamicLibrary( const std::string& libraryName );
		static void* getDynamicSymbol( void* handle, const std::string& symbolName );
		static void	 freeDynamicLibrary( void* handle );
	};
}
