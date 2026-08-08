/**
 * @file FileUtil.cpp
 * @brief FileUtil 구현
 */
#include "pch.h"
#include "FileUtil.h"
#include "Core/Common/CommonDefines.h"

#if defined( SW_PLATFORM_LINUX )
	#include "Core/Utility/File/Linux/LinuxFileDialog.h"
	#include "Core/Utility/Log/Logger.h"
#endif

namespace sw
{

	std::string FileUtil::getCurrentDateTimeAsString()
	{
		time_t t = std::time( nullptr );
		tm	   time_info{};
#if defined( SW_PLATFORM_WINDOWS )
		localtime_s( &time_info, &t );
#else
		localtime_r( &t, &time_info );
#endif
		char buffer[constant::kMaxBuffer128];
		std::strftime( buffer, sizeof( buffer ), "%d-%m-%Y %H-%M-%S", &time_info );
		return std::string{ buffer };
	}

	void FileUtil::splitPath( const std::string_view fullPath, std::string& outDirectoryPath, std::string& outFileName )
	{
		const size_t found = fullPath.find_last_of( "/\\" );
		if ( found == std::string_view::npos )
		{
			outDirectoryPath.clear();
			outFileName = std::string{ fullPath };
			return;
		}
		outDirectoryPath = std::string{ fullPath.substr( 0, found + 1 ) };
		outFileName		 = std::string{ fullPath.substr( found + 1 ) };
	}

	std::string FileUtil::getFileNamePart( const std::string_view fullPath )
	{
		if ( fullPath.empty() )
			return std::string{ fullPath };

		std::string fileName{};
		std::string directory{};
		splitPath( fullPath, directory, fileName );
		return fileName;
	}

	std::string FileUtil::getDirectoryPart( const std::string_view fullPath )
	{
		if ( fullPath.empty() )
			return std::string{ fullPath };

		std::string normalized{ fullPath };
		while ( normalized.size() > 1 && ( normalized.back() == '/' || normalized.back() == '\\' ) )
			normalized.pop_back();

		const size_t found = normalized.find_last_of( "/\\" );
		if ( found == std::string::npos )
			return "";

		return normalized.substr( 0, found );
	}

	void FileUtil::getExtensionPart( const std::string_view fileName, std::vector<std::string>& outPartList )
	{
		const string_splitter splitter{ fileName, { "." } };
		outPartList.reserve( splitter.getCount() );
		for ( std::string_view part : splitter.getSplitList() )
		{
			outPartList.push_back( std::string{ part } );
		}
	}

	std::string FileUtil::replaceExtension( const std::string_view fileName, const std::string_view extension )
	{
		std::filesystem::path p{ fileName };
		p.replace_extension( extension );
		return p.generic_string();
	}

	std::string FileUtil::forceExtension( const std::string_view fileName, const std::string_view extension )
	{
		SW_ASSERT( extension.empty() == false );

		const bool bContainsDot = ( extension[0] == '.' );
		if ( bContainsDot )
		{
			if ( fileName.length() < extension.length() )
				return std::string{ fileName } + std::string{ extension };
		}
		else
		{
			if ( fileName.length() < ( extension.length() + 1 ) )
				return std::string{ fileName } + "." + std::string{ extension };
		}

		if ( fileName.substr( fileName.length() - extension.length() ).compare( extension ) != 0 )
		{
			const std::string ext = bContainsDot ? std::string{ extension } : "." + std::string{ extension };
			return std::string{ fileName } + ext;
		}
		return std::string{ fileName };
	}

	std::string FileUtil::removeExtension( const std::string_view fileName )
	{
		std::filesystem::path p{ fileName };
		if ( p.has_parent_path() )
			return ( p.parent_path() / p.stem() ).generic_string();
		return p.stem().generic_string();
	}

	bool FileUtil::makePathRelative( const std::string_view rootDir, const std::string_view path, std::string& outResult )
	{
		if ( rootDir.empty() || path.empty() )
			return false;

		const std::filesystem::path filepath{ path };
		if ( filepath.is_absolute() )
		{
			const std::filesystem::path rootpath{ rootDir };
			const std::filesystem::path relative = std::filesystem::relative( filepath, rootpath );
			if ( relative.empty() == false )
			{
				outResult = relative.generic_string();
				return true;
			}
			return false;
		}
		return false;
	}

	bool FileUtil::makePathAbsolute( const std::string_view path, std::string& outResult )
	{
		const std::filesystem::path absolute = std::filesystem::absolute( path );
		if ( absolute.empty() == false )
		{
			outResult = absolute.generic_string();
			return true;
		}
		return false;
	}

	std::string FileUtil::normalizeSeparators( const std::string_view path )
	{
		std::string outResult{ path };
		for ( utf8& c : outResult )
		{
			if ( c == '\\' )
				c = '/';
		}
		return outResult;
	}

	std::string FileUtil::normalizePath( const std::string_view path )
	{
		std::string outResult{ path };
		for ( utf8& c : outResult )
		{
			if ( c == '\\' )
				c = '/';
			else
				c = StringUtil::toLowerChar( c );
		}
		return outResult;
	}

	bool FileUtil::pathsEqualNormalized( const std::string_view lhs, const std::string_view rhs )
	{
		std::string a = normalizePath( lhs );
		std::string b = normalizePath( rhs );
		while ( a.size() > 1 && a.back() == '/' )
			a.pop_back();
		while ( b.size() > 1 && b.back() == '/' )
			b.pop_back();
		return a == b;
	}

	void FileUtil::createDirectory( const std::string_view path )
	{
		const std::string directoryPart = getDirectoryPart( path );
		if ( isDirectoryExist( directoryPart ) )
			return;

		std::filesystem::create_directories( directoryPart );
	}

	bool FileUtil::isFileExist( const std::string_view fileName )
	{
		return std::filesystem::exists( fileName );
	}

	bool FileUtil::isDirectoryExist( const std::string_view fileName )
	{
		return std::filesystem::exists( fileName ) && std::filesystem::is_directory( fileName );
	}

	std::string FileUtil::getCurrentPath()
	{
		const std::filesystem::path path = std::filesystem::current_path();
		return path.generic_string();
	}

	std::string FileUtil::getExecutablePath()
	{
#if defined( SW_PLATFORM_WINDOWS )
		fixed_wstring<constant::kMaxPathSize> path;
		GetModuleFileNameW( nullptr, path.data(), path.capacity() );
		return StringUtil::utf16ToUtf8( path.c_str() );
#elif defined( SW_PLATFORM_MACOS )
		char   pathBuf[1024];
		uint32 bufSize = sizeof( pathBuf );
		if ( _NSGetExecutablePath( pathBuf, &bufSize ) == 0 )
		{
			std::error_code ec;
			auto			pathObj = std::filesystem::canonical( pathBuf, ec );
			if ( !ec )
				return pathObj.string();
			return std::string( pathBuf );
		}
		return std::string{};
#else
		std::error_code ec;
		auto			pathObj = std::filesystem::canonical( "/proc/self/exe", ec );
		if ( !ec )
			return pathObj.string();
		return std::string{};
#endif
	}

	bool FileUtil::copyFile( const std::string_view source, const std::string_view destination )
	{
		std::error_code ec;
		bool			result = std::filesystem::copy_file( source, destination, std::filesystem::copy_options::overwrite_existing, ec );
		if ( ec )
		{
			SW_LOG_ERROR( "copyFile failed: %#", ec.message().c_str() );
			return false;
		}
		return result;
	}

	uint32 FileUtil::getFileSize( const std::string_view fileName )
	{
		const std::string filePath = normalizeSeparators( fileName );
		std::ifstream	  input{ filePath, std::ios::binary | std::ios::ate };
		if ( input.is_open() )
		{
			const size_t dataSize = static_cast<size_t>( input.tellg() );
			input.close();
			return static_cast<uint32>( dataSize );
		}
		return 0;
	}

	bool FileUtil::writeFile( const std::string_view fileName, const uint8* data, const uint64 size )
	{
		if ( size == 0 )
			return false;

		const std::string filePath = normalizeSeparators( fileName );
		std::ofstream	  output{ filePath, std::ios::binary | std::ios::trunc };
		if ( output.is_open() )
		{
			output.write( reinterpret_cast<const utf8*>( data ), static_cast<std::streamsize>( size ) );
			output.close();
			return true;
		}

		return false;
	}

	bool FileUtil::readFile( const std::string_view fileName, std::vector<uint8>& outData, const uint32 offset, const uint32 maxReadCount )
	{
		const std::string filePath = normalizeSeparators( fileName );
		std::ifstream	  input{ filePath, std::ios::binary | std::ios::ate };
		if ( input.is_open() )
		{
			size_t dataSize = static_cast<size_t>( input.tellg() ) - offset;
			dataSize		= std::min( dataSize, static_cast<size_t>( maxReadCount ) );
			input.seekg( offset );
			outData.resize( dataSize );
			input.read( reinterpret_cast<utf8*>( outData.data() ), static_cast<std::streamsize>( dataSize ) );
			input.close();
			return true;
		}

		SW_LOG_ERROR( "File not found: %#", fileName );
		return false;
	}

	uint64 FileUtil::getFileTimestamp( const std::string_view fileName )
	{
		if ( isFileExist( fileName ) == false )
			return 0;

		const std::string					  filePath = normalizeSeparators( fileName );
		const std::filesystem::file_time_type tim	   = std::filesystem::last_write_time( filePath );
		return std::chrono::duration_cast<std::chrono::duration<uint64>>( tim.time_since_epoch() ).count();
	}

	void FileUtil::openFileDialog( const FileDialogParams& params, FileDialogDelegate onSuccess )
	{
#if defined( SW_PLATFORM_WINDOWS )
		std::thread(
			[delegateCallback = std::move( onSuccess ), params]
			{
				fixed_wstring<constant::kMaxBuffer8192> szFile;

				OPENFILENAMEW ofn{};
				ofn.lStructSize = sizeof( ofn );
				ofn.hwndOwner	= nullptr;
				ofn.lpstrFile	= szFile.data();
				ofn.nMaxFile	= static_cast<DWORD>( szFile.capacity() );
				ofn.nFilterIndex = 1;

				std::wstring titleW;
				if ( params._title.empty() == false )
				{
					titleW		   = StringUtil::utf8ToUtf16( params._title );
					ofn.lpstrTitle = titleW.c_str();
				}

				std::wstring initialDirW;
				if ( params._initialDirectory.empty() == false )
				{
					initialDirW			= StringUtil::utf8ToUtf16( normalizePath( params._initialDirectory ) );
					ofn.lpstrInitialDir = initialDirW.c_str();
				}

				fixed_string<constant::kMaxBuffer4096> filter;
				const char*							   desc = params._description.empty() ? "All Files" : params._description.c_str();
				filter.append( desc );
				filter.push_back( 0 );

				if ( params._filterExtensionList.empty() )
				{
					filter.append( "*.*" );
					filter.push_back( 0 );
				}
				else
				{
					for ( size_t i = 0; i < params._filterExtensionList.size(); ++i )
					{
						const std::string& filterExtension = params._filterExtensionList[i];
						if ( i > 0 )
							filter.push_back( ';' );
						filter.push_back( '*' );
						if ( filterExtension.empty() == false && filterExtension[0] != '.' )
							filter.push_back( '.' );
						filter.append( filterExtension.c_str() );
					}
					filter.push_back( 0 );
				}
				filter.push_back( 0 );

				const std::wstring filterW = StringUtil::utf8ToUtf16( filter.c_str() );
				ofn.lpstrFilter			   = filterW.c_str();

				BOOL result = FALSE;
				switch ( params._type )
				{
					case FileDialogParams::Type::Open:
						ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
						if ( params._bEnableMultiselect )
							ofn.Flags |= OFN_ALLOWMULTISELECT;
						result = GetOpenFileNameW( &ofn );
						break;
					case FileDialogParams::Type::Save:
						ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_EXPLORER | OFN_NOCHANGEDIR;
						result	  = GetSaveFileNameW( &ofn );
						break;
				}

				if ( result == FALSE || delegateCallback.isBound() == false )
					return;

				std::vector<std::string> filePathList;
				const wchar_t*			 p = szFile.data();
				if ( p == nullptr || *p == L'\0' )
					return;

				// Multi-select: "dir\0file1\0file2\0\0" / Single: "full\path\file\0"
				const std::wstring first( p );
				p += first.size() + 1;
				if ( *p == L'\0' )
				{
					filePathList.push_back( normalizePath( StringUtil::utf16ToUtf8( first ) ) );
				}
				else
				{
					const std::string directoryPath = normalizePath( StringUtil::utf16ToUtf8( first ) );
					while ( *p != L'\0' )
					{
						const std::wstring fileNameW( p );
						filePathList.push_back( normalizePath( directoryPath + "/" + StringUtil::utf16ToUtf8( fileNameW ) ) );
						p += fileNameW.size() + 1;
					}
				}

				if ( filePathList.empty() == false )
					delegateCallback( filePathList );
			} )
			.detach();
#elif defined( SW_PLATFORM_LINUX )
		std::thread(
			[delegateCallback = std::move( onSuccess ), params]
			{
				if ( delegateCallback.isBound() == false )
					return;

				std::vector<std::string> results;
				if ( LinuxFileDialog::open( params, results ) == false || results.empty() )
					return;

				delegateCallback( results );
			} )
			.detach();
#elif defined( SW_PLATFORM_MACOS )
		std::thread(
			[delegateCallback = std::move( onSuccess ), params]
			{
				if ( delegateCallback.isBound() == false )
					return;

				std::string cmd = "osascript -e 'choose file ";
				if ( params._type == FileDialogParams::Type::Save )
					cmd = "osascript -e 'choose file name ";
				if ( params._description.empty() == false )
					cmd += "with prompt \"" + params._description + "\" ";
				if ( params._bEnableMultiselect && params._type == FileDialogParams::Type::Open )
					cmd += "with multiple selections allowed ";
				cmd += "'";

				FILE* pipe = popen( cmd.c_str(), "r" );
				if ( pipe == nullptr )
					return;

				char		buf[1024];
				std::string output;
				while ( fgets( buf, sizeof( buf ), pipe ) != nullptr )
					output += buf;
				pclose( pipe );

				while ( output.empty() == false && ( output.back() == '\n' || output.back() == '\r' ) )
					output.pop_back();
				if ( output.empty() )
					return;

				delegateCallback( std::vector<std::string>{ normalizePath( output ) } );
			} )
			.detach();
#else
		(void)params;
		(void)onSuccess;
		SW_LOG_WARNING( "[FileUtil] openFileDialog is not supported on this platform." );
#endif
	}

	bool FileUtil::collectFiles( const std::string_view directory, const std::string_view filterExtension, std::vector<std::string>& outFilePathList, const bool bRecursive, const bool bNormalizePath )
	{
		if ( isDirectoryExist( directory ) == false )
			return false;

		const std::filesystem::path directoryPath{ directory };
		const std::string			lowerFilterExt = StringUtil::toLower( filterExtension );

		if ( bRecursive )
		{
			for ( const auto& entry : std::filesystem::recursive_directory_iterator{ directoryPath } )
			{
				if ( entry.is_directory() )
					continue;

				if ( lowerFilterExt.empty() == false )
				{
					const std::string fileExtension{ entry.path().extension().generic_string() };
					if ( StringUtil::toLower( fileExtension ) != lowerFilterExt )
						continue;
				}
				std::string filePath = ( bNormalizePath ) ? normalizePath( entry.path().generic_string() ) : entry.path().generic_string();
				outFilePathList.push_back( filePath );
			}
		}
		else
		{
			for ( const auto& entry : std::filesystem::directory_iterator{ directoryPath } )
			{
				if ( entry.is_directory() )
					continue;

				if ( lowerFilterExt.empty() == false )
				{
					const std::string fileExtension{ entry.path().extension().generic_string() };
					if ( StringUtil::toLower( fileExtension ) != lowerFilterExt )
						continue;
				}
				std::string filePath = ( bNormalizePath ) ? normalizePath( entry.path().generic_string() ) : entry.path().generic_string();
				outFilePathList.push_back( filePath );
			}
		}

		return true;
	}

	bool FileUtil::collectFolders( const std::string_view directory, std::vector<std::string>& outFolderList, const bool bRecursive, const bool bNormalizePath )
	{
		if ( isDirectoryExist( directory ) == false )
			return false;

		const std::filesystem::path directoryPath{ directory };

		if ( bRecursive )
		{
			for ( const auto& entry : std::filesystem::recursive_directory_iterator{ directoryPath } )
			{
				if ( entry.is_directory() == false )
					continue;

				std::string filePath = ( bNormalizePath ) ? normalizePath( entry.path().generic_string() ) : entry.path().generic_string();
				outFolderList.push_back( filePath );
			}
		}
		else
		{
			for ( const auto& entry : std::filesystem::directory_iterator{ directoryPath } )
			{
				if ( entry.is_directory() == false )
					continue;

				std::string filePath = ( bNormalizePath ) ? normalizePath( entry.path().generic_string() ) : entry.path().generic_string();
				outFolderList.push_back( filePath );
			}
		}

		return true;
	}

	std::string FileUtil::getClipboardText()
	{
#if defined( SW_PLATFORM_WINDOWS )
		if ( OpenClipboard( nullptr ) == FALSE )
			return std::string{};

		const HANDLE handle = GetClipboardData( CF_UNICODETEXT );
		if ( handle == nullptr )
		{
			CloseClipboard();
			return std::string{};
		}

		std::wstring wstr{};
		if ( const WCHAR* globalStr = static_cast<const WCHAR*>( GlobalLock( handle ) ) )
			wstr = globalStr;

		GlobalUnlock( handle );
		CloseClipboard();

		return StringUtil::utf16ToUtf8( wstr );
#elif defined( SW_PLATFORM_MACOS )
		FILE* pipe = popen( "pbpaste", "r" );
		if ( pipe == nullptr )
			return std::string{};
		char		buf[512];
		std::string result;
		while ( fgets( buf, sizeof( buf ), pipe ) != nullptr )
		{
			result += buf;
		}
		pclose( pipe );
		return result;
#elif defined( SW_PLATFORM_LINUX )
		FILE* pipe = popen( "xclip -selection clipboard -o 2>/dev/null", "r" );
		if ( pipe == nullptr )
			return std::string{};
		char		buf[512];
		std::string result;
		while ( fgets( buf, sizeof( buf ), pipe ) != nullptr )
		{
			result += buf;
		}
		pclose( pipe );
		return result;
#else
		return std::string{};
#endif
	}

	bool FileUtil::setClipboardText( const std::string& str )
	{
#if defined( SW_PLATFORM_WINDOWS )
		if ( OpenClipboard( nullptr ) == FALSE )
			return false;

		const std::wstring wstr			= StringUtil::utf8ToUtf16( str );
		const uint32	   stringLength = static_cast<uint32>( wstr.length() ) + 1;
		const HGLOBAL	   handle		= GlobalAlloc( GMEM_MOVEABLE, stringLength * sizeof( WCHAR ) );
		if ( handle == nullptr )
		{
			CloseClipboard();
			return false;
		}

		WCHAR* globalStr = static_cast<WCHAR*>( GlobalLock( handle ) );
		std::memcpy( globalStr, wstr.c_str(), stringLength * sizeof( WCHAR ) );
		GlobalUnlock( handle );
		EmptyClipboard();
		if ( SetClipboardData( CF_UNICODETEXT, handle ) == nullptr )
		{
			GlobalFree( handle );
			return false;
		}
		CloseClipboard();
		return true;
#elif defined( SW_PLATFORM_MACOS )
		FILE* pipe = popen( "pbcopy", "w" );
		if ( pipe == nullptr )
			return false;
		fwrite( str.c_str(), 1, str.size(), pipe );
		pclose( pipe );
		return true;
#elif defined( SW_PLATFORM_LINUX )
		FILE* pipe = popen( "xclip -selection clipboard 2>/dev/null", "w" );
		if ( pipe == nullptr )
			return false;
		fwrite( str.c_str(), 1, str.size(), pipe );
		pclose( pipe );
		return true;
#else
		return false;
#endif
	}

	std::string_view FileUtil::getSharedLibraryPrefix()
	{
#if defined( SW_PLATFORM_WINDOWS )
		return "";
#else
		return "lib";
#endif
	}

	std::string_view FileUtil::getSharedLibraryExtension()
	{
#if defined( SW_PLATFORM_WINDOWS )
		return ".dll";
#elif defined( SW_PLATFORM_MACOS )
		return ".dylib";
#else
		return ".so";
#endif
	}

	std::string FileUtil::formatSharedLibraryName( const std::string& baseName )
	{
		return std::string( getSharedLibraryPrefix() ) + baseName + std::string( getSharedLibraryExtension() );
	}

	std::string FileUtil::getDebugSymbolPath( const std::string_view libraryPath )
	{
#if defined( SW_PLATFORM_WINDOWS )
		return replaceExtension( libraryPath, ".pdb" );
#elif defined( SW_PLATFORM_MACOS ) || defined( SW_PLATFORM_APPLE )
		return std::string( libraryPath ) + ".dSYM";
#else
		return replaceExtension( libraryPath, ".debug" );
#endif
	}

	void* FileUtil::loadDynamicLibrary( const std::string& libraryName )
	{
		if ( libraryName.empty() == true )
			return nullptr;

#if defined( SW_PLATFORM_WINDOWS )
		std::string normalized = normalizePath( libraryName );
		std::string absPath;
		if ( makePathAbsolute( normalized, absPath ) && isFileExist( absPath ) )
		{
			HMODULE hMod = LoadLibraryExA( absPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH );
			if ( hMod != nullptr )
				return reinterpret_cast<void*>( hMod );
		}
		return reinterpret_cast<void*>( LoadLibraryA( libraryName.c_str() ) );
#else
		return dlopen( libraryName.c_str(), RTLD_LAZY );
#endif
	}

	void* FileUtil::getDynamicSymbol( void* handle, const std::string& symbolName )
	{
		if ( handle == nullptr || symbolName.empty() == true )
			return nullptr;

#if defined( SW_PLATFORM_WINDOWS )
		return reinterpret_cast<void*>( GetProcAddress( static_cast<HMODULE>( handle ), symbolName.c_str() ) );
#else
		return dlsym( handle, symbolName.c_str() );
#endif
	}

	void FileUtil::freeDynamicLibrary( void* handle )
	{
		if ( handle == nullptr )
			return;

#if defined( SW_PLATFORM_WINDOWS )
		FreeLibrary( static_cast<HMODULE>( handle ) );
#else
		dlclose( handle );
#endif
	}
} // namespace sw
