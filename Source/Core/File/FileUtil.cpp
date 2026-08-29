#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/string_splitter.h"

#if defined( SW_PLATFORM_WINDOWS )
	#include "Core/File/Windows/WindowsFileDialog.h"
#elif defined( SW_PLATFORM_LINUX )
	#include "Core/File/Linux/LinuxFileDialog.h"
#elif defined( SW_PLATFORM_MACOS )
	#include "Core/File/Mac/MacFileDialog.h"
#endif

namespace sw
{
	SW_LOG_CALLER( "FileUtil" );

	string FileUtil::getCurrentDateTimeAsString()
	{
		time_t t = std::time( nullptr );
		tm	   time_info{};
#if defined( SW_PLATFORM_WINDOWS )
		localtime_s( &time_info, &t );
#else
		localtime_r( &t, &time_info );
#endif
		utf8 arrBuffer[constant::kMaxBuffer128];
		std::strftime( arrBuffer, sizeof( arrBuffer ), "%d-%m-%Y %H-%M-%S", &time_info );
		return string{ arrBuffer };
	}

	void FileUtil::splitPath( string_view fullPath, string& outDirectoryPath, string& outFileName )
	{
		const size_t found = fullPath.find_last_of( "/\\" );
		if ( found == string_view::npos )
		{
			outDirectoryPath.clear();
			outFileName = string{ fullPath };
			return;
		}
		outDirectoryPath = string{ fullPath.substr( 0, found + 1 ) };
		outFileName		 = string{ fullPath.substr( found + 1 ) };
	}

	string FileUtil::getFileNamePart( string_view fullPath )
	{
		if ( fullPath.empty() )
			return {};

		const size_t found = fullPath.find_last_of( "/\\" );
		if ( found == string_view::npos )
			return string{ fullPath };

		return string{ fullPath.substr( found + 1 ) };
	}

	string FileUtil::getDirectoryPart( string_view fullPath )
	{
		if ( fullPath.empty() )
			return {};

		string_view v = fullPath;
		while ( v.size() > 1 && ( v.back() == '/' || v.back() == '\\' ) )
		{
			v.remove_suffix( 1 );
		}

		if ( v.empty() )
			return {};

		const size_t found = v.find_last_of( "/\\" );
		if ( found == string_view::npos )
			return {};

		return string{ v.substr( 0, found ) };
	}

	void FileUtil::getExtensionPart( string_view fileName, vector<string>& outPartList )
	{
		const string_splitter splitter{ fileName, { "." } };
		outPartList.reserve( splitter.getCount() );
		for ( string_view part : splitter.getSplitList() )
		{
			outPartList.push_back( string{ part } );
		}
	}

	string FileUtil::getExtension( string_view fileName )
	{
		const size_t slash = fileName.find_last_of( "/\\" );
		const size_t start = ( slash == string_view::npos ) ? 0 : slash + 1;
		const size_t dot   = fileName.find_last_of( '.' );
		if ( dot == string_view::npos || dot < start )
			return {};
		return string{ fileName.substr( dot ) };
	}

	bool FileUtil::hasExtension( string_view fileName, string_view extension )
	{
		if ( extension.empty() || fileName.empty() )
			return false;

		string_view want = extension;
		if ( want.front() == '.' )
			want.remove_prefix( 1 );

		const size_t slash = fileName.find_last_of( "/\\" );
		const size_t start = ( slash == string_view::npos ) ? 0 : slash + 1;
		const size_t dot   = fileName.find_last_of( '.' );
		if ( dot == string_view::npos || dot < start )
			return false;

		string_view have = fileName.substr( dot + 1 );
		if ( have.size() != want.size() )
			return false;

		return StringUtil::strnicmp( have.data(), want.data(), static_cast<uint32>( want.size() ) ) == 0;
	}

	bool FileUtil::hasAnyExtension( string_view fileName, std::initializer_list<string_view> listExtension )
	{
		for ( string_view extension : listExtension )
		{
			if ( hasExtension( fileName, extension ) )
				return true;
		}
		return false;
	}

	bool FileUtil::endsWithIgnoreCase( string_view path, string_view suffix )
	{
		if ( suffix.empty() || path.size() < suffix.size() )
			return false;

		string_view tail = path.substr( path.size() - suffix.size() );
		return StringUtil::strnicmp( tail.data(), suffix.data(), static_cast<uint32>( suffix.size() ) ) == 0;
	}

	bool FileUtil::endsWithAnyIgnoreCase( string_view path, std::initializer_list<string_view> listSuffix )
	{
		for ( string_view suffix : listSuffix )
		{
			if ( endsWithIgnoreCase( path, suffix ) )
				return true;
		}
		return false;
	}

	string FileUtil::replaceExtension( string_view fileName, string_view extension )
	{
		if ( fileName.empty() )
			return string( extension );

		const size_t slash = fileName.find_last_of( "/\\" );
		const size_t start = ( slash == string_view::npos ) ? 0 : slash + 1;
		const size_t dot   = fileName.find_last_of( '.' );

		string_view base = fileName;
		if ( dot != string_view::npos && dot >= start )
			base = fileName.substr( 0, dot );

		string_view ext = extension;
		if ( ext.empty() )
			return string( base );

		StringBuilder<constant::kMaxBuffer256> sb;
		sb.append( base );
		if ( ext.front() != '.' )
			sb.append( '.' );
		sb.append( ext );
		return string( sb.view() );
	}

	string FileUtil::removeExtension( string_view fileName )
	{
		if ( fileName.empty() )
			return {};

		const size_t slash = fileName.find_last_of( "/\\" );
		const size_t start = ( slash == string_view::npos ) ? 0 : slash + 1;
		const size_t dot   = fileName.find_last_of( '.' );

		if ( dot != string_view::npos && dot >= start )
			return string( fileName.substr( 0, dot ) );

		return string( fileName );
	}

	bool FileUtil::makePathRelative( string_view rootDir, string_view path, string& outResult )
	{
		if ( rootDir.empty() || path.empty() )
			return false;

		const std::filesystem::path filepath{ path };
		if ( filepath.is_absolute() )
		{
			const std::filesystem::path rootpath{ rootDir };
			std::error_code				ec;
			const std::filesystem::path relative = std::filesystem::relative( filepath, rootpath, ec );
			if ( ec.value() == 0 && relative.empty() == false )
			{
				outResult = string( relative.generic_string().c_str() );
				return true;
			}
			return false;
		}
		return false;
	}

	bool FileUtil::makePathAbsolute( string_view path, string& outResult )
	{
		std::error_code				ec;
		const std::filesystem::path absolute = std::filesystem::absolute( path, ec );
		if ( ec.value() == 0 && absolute.empty() == false )
		{
			outResult = string( absolute.generic_string().c_str() );
			return true;
		}
		return false;
	}

	string FileUtil::normalizePath( string_view path )
	{
		string outResult{ path };
		for ( utf8& ch : outResult )
		{
			if ( ch == '\\' )
				ch = '/';
			else
				ch = StringUtil::toLowerChar( ch );
		}
		return outResult;
	}

	string FileUtil::normalizeSeparators( string_view path )
	{
		string outResult{ path };
		for ( utf8& ch : outResult )
		{
			if ( ch == '\\' )
				ch = '/';
		}
		return outResult;
	}

	bool FileUtil::pathsEqualNormalized( string_view lhs, string_view rhs )
	{
		const string aNorm = normalizePath( trimTrailingSlashes( lhs ) );
		const string bNorm = normalizePath( trimTrailingSlashes( rhs ) );
		return aNorm == bNorm;
	}

	string FileUtil::trimTrailingSlashes( string_view path )
	{
		string_view v = path;
		while ( v.size() > 1 && ( v.back() == '/' || v.back() == '\\' ) )
		{
			v.remove_suffix( 1 );
		}
		return string{ v };
	}

	string FileUtil::joinPath( string_view root, string_view relative )
	{
		if ( root.empty() )
			return {};

		string_view r = root;
		while ( r.size() > 1 && ( r.back() == '/' || r.back() == '\\' ) )
		{
			r.remove_suffix( 1 );
		}

		if ( relative.empty() )
			return normalizeSeparators( r );

		string_view rel = relative;
		while ( rel.empty() == false && ( rel.front() == '/' || rel.front() == '\\' ) )
		{
			rel.remove_prefix( 1 );
		}

		StringBuilder<constant::kMaxBuffer512> sb;
		sb.ensureCapacity( static_cast<uint32>( r.size() + 1 + rel.size() ) );
		for ( const utf8 ch : r )
			sb.append( ( ch == '\\' ) ? '/' : ch );

		if ( rel.empty() == false )
		{
			if ( sb.view().empty() == false && sb.view().back() != '/' )
				sb.append( '/' );
			for ( const utf8 ch : rel )
				sb.append( ( ch == '\\' ) ? '/' : ch );
		}

		return string( sb.view() );
	}

	bool FileUtil::startsWithPathComponent( string_view path, string_view component )
	{
		if ( path.size() < component.size() )
			return false;
		if ( path.compare( 0, component.size(), component ) != 0 )
			return false;
		return path.size() == component.size() || path[component.size()] == '/' || path[component.size()] == '\\';
	}

	string FileUtil::suffixAfterPathComponent( string_view path, string_view component )
	{
		if ( path.size() <= component.size() )
			return {};
		// skip "component/"
		return string{ path.substr( component.size() + 1 ) };
	}

	void FileUtil::createDirectory( string_view path )
	{
		const string directoryPart = getDirectoryPart( path );
		if ( directoryPart.empty() || directoryExists( directoryPart ) )
			return;

		std::error_code ec;
		std::filesystem::create_directories( directoryPart.c_str(), ec );
	}

	void FileUtil::ensureDirectoryExists( string_view directoryPath )
	{
		if ( directoryPath.empty() || directoryExists( directoryPath ) )
			return;

		std::error_code ec;
		std::filesystem::create_directories( normalizeSeparators( directoryPath ).c_str(), ec );
	}

	bool FileUtil::fileExists( string_view fileName )
	{
		return std::filesystem::exists( fileName );
	}

	bool FileUtil::directoryExists( string_view path )
	{
		return std::filesystem::exists( path ) && std::filesystem::is_directory( path );
	}

	string FileUtil::getCurrentPath()
	{
		const std::filesystem::path path = std::filesystem::current_path();
		return string( path.generic_string().c_str() );
	}

	string FileUtil::getExecutablePath()
	{
#if defined( SW_PLATFORM_WINDOWS )
		fixed_wstring<constant::kMaxPathSize> path;
		GetModuleFileNameW( nullptr, path.data(), path.capacity() );
		return StringUtil::utf16ToUtf8( path.c_str() );
#elif defined( SW_PLATFORM_MACOS )
		utf8   pathBuf[constant::kMaxBuffer1024];
		uint32 bufSize = sizeof( pathBuf );
		if ( _NSGetExecutablePath( pathBuf, &bufSize ) == 0 )
		{
			std::error_code ec;
			auto			pathObj = std::filesystem::canonical( pathBuf, ec );
			if ( ec.value() == 0 )
				return string{ pathObj.generic_string().c_str() };
			return string{ pathBuf };
		}
		return string{};
#else
		std::error_code ec;
		auto			pathObj = std::filesystem::canonical( "/proc/self/exe", ec );
		if ( ec.value() == 0 )
			return string{ pathObj.generic_string().c_str() };
		return string{};
#endif
	}

	uint64 FileUtil::getFileTimestamp( string_view fileName )
	{
		if ( fileExists( fileName ) == false )
			return 0;

		const string						  filePath = normalizeSeparators( fileName );
		const std::filesystem::file_time_type tim	   = std::filesystem::last_write_time( filePath.c_str() );
		return std::chrono::duration_cast<std::chrono::duration<uint64>>( tim.time_since_epoch() ).count();
	}

	uint64 FileUtil::getFileSize( string_view fileName )
	{
		if ( fileName.empty() )
			return 0;

		const string filePath = normalizeSeparators( fileName );
		FILE*		 pFile{ nullptr };
#if defined( SW_PLATFORM_WINDOWS )
		fopen_s( &pFile, filePath.c_str(), "rb" );
#else
		pFile = fopen( filePath.c_str(), "rb" );
#endif
		if ( pFile == nullptr )
			return 0;

#if defined( SW_PLATFORM_WINDOWS )
		_fseeki64( pFile, 0, SEEK_END );
		const int64 size = _ftelli64( pFile );
#else
		fseeko( pFile, 0, SEEK_END );
		const off_t size = ftello( pFile );
#endif
		std::fclose( pFile );
		return ( size > 0 ) ? static_cast<uint64>( size ) : 0;
	}

	bool FileUtil::copyFile( string_view source, string_view destination )
	{
		std::error_code ec;
		bool			result = std::filesystem::copy_file( source, destination, std::filesystem::copy_options::overwrite_existing, ec );
		if ( ec.value() != 0 )
		{
			SW_LOG_ERROR( "copyFile failed: %#", ec.message().c_str() );
			return false;
		}
		return result;
	}

	bool FileUtil::removeFile( string_view path )
	{
		if ( path.empty() )
			return true;
		const string	normalized = normalizeSeparators( path );
		std::error_code ec;
		std::filesystem::remove( normalized.c_str(), ec );
		return fileExists( normalized ) == false;
	}

	string FileUtil::getTempDirectory()
	{
		std::error_code				ec;
		const std::filesystem::path p = std::filesystem::temp_directory_path( ec );
		if ( ec.value() != 0 )
			return {};
		return string( normalizeSeparators( p.generic_string().c_str() ).c_str() );
	}

	bool FileUtil::writeFile( string_view fileName, const uint8* pData, const uint64 size )
	{
		if ( size == 0 || pData == nullptr )
			return false;

		const string filePath = normalizeSeparators( fileName );
		FILE*		 pFile{ nullptr };
#if defined( SW_PLATFORM_WINDOWS )
		fopen_s( &pFile, filePath.c_str(), "wb" );
#else
		pFile = fopen( filePath.c_str(), "wb" );
#endif
		if ( pFile == nullptr )
			return false;

		const size_t written = std::fwrite( pData, 1, static_cast<size_t>( size ), pFile );
		std::fclose( pFile );
		return written == static_cast<size_t>( size );
	}

	bool FileUtil::readFile( string_view fileName, vector<uint8>& outData, const uint32 offset, const uint32 maxReadCount )
	{
		const string filePath = normalizeSeparators( fileName );
		FILE*		 pFile{ nullptr };
#if defined( SW_PLATFORM_WINDOWS )
		fopen_s( &pFile, filePath.c_str(), "rb" );
#else
		pFile = fopen( filePath.c_str(), "rb" );
#endif
		if ( pFile == nullptr )
		{
			SW_LOG_ERROR( "File not found: %#", fileName );
			return false;
		}

#if defined( SW_PLATFORM_WINDOWS )
		_fseeki64( pFile, 0, SEEK_END );
		const int64 fileSize = _ftelli64( pFile );
#else
		fseeko( pFile, 0, SEEK_END );
		const off_t fileSize = ftello( pFile );
#endif
		if ( fileSize < 0 )
		{
			std::fclose( pFile );
			SW_LOG_ERROR( "Failed to query size of: %#", fileName );
			return false;
		}

		const uint64 uFileSize = static_cast<uint64>( fileSize );
		if ( offset > uFileSize )
		{
			std::fclose( pFile );
			SW_LOG_ERROR( "Read offset %# exceeds size %# of: %#", offset, uFileSize, fileName );
			return false;
		}

		const uint64 dataSize = MathUtil::min( uFileSize - offset, static_cast<uint64>( maxReadCount ) );
#if defined( SW_PLATFORM_WINDOWS )
		_fseeki64( pFile, static_cast<int64>( offset ), SEEK_SET );
#else
		fseeko( pFile, static_cast<off_t>( offset ), SEEK_SET );
#endif
		outData.resize( dataSize );
		if ( dataSize > 0 )
		{
			const size_t readBytes = std::fread( outData.data(), 1, static_cast<size_t>( dataSize ), pFile );
			if ( readBytes != static_cast<size_t>( dataSize ) )
				outData.resize( readBytes );
		}
		std::fclose( pFile );
		return true;
	}

	bool FileUtil::readTextFile( string_view fileName, string& outText )
	{
		const string filePath = normalizeSeparators( fileName );
		FILE*		 pFile{ nullptr };
#if defined( SW_PLATFORM_WINDOWS )
		fopen_s( &pFile, filePath.c_str(), "rb" );
#else
		pFile = fopen( filePath.c_str(), "rb" );
#endif
		if ( pFile == nullptr )
		{
			SW_LOG_ERROR( "File not found: %#", fileName );
			return false;
		}

#if defined( SW_PLATFORM_WINDOWS )
		_fseeki64( pFile, 0, SEEK_END );
		const int64 fileSize = _ftelli64( pFile );
		_fseeki64( pFile, 0, SEEK_SET );
#else
		fseeko( pFile, 0, SEEK_END );
		const off_t fileSize = ftello( pFile );
		fseeko( pFile, 0, SEEK_SET );
#endif
		if ( fileSize < 0 )
		{
			std::fclose( pFile );
			return false;
		}

		const size_t dataSize = static_cast<size_t>( fileSize );
		outText.resize( dataSize );
		if ( dataSize > 0 )
		{
			const size_t readBytes = std::fread( outText.data(), 1, dataSize, pFile );
			if ( readBytes != dataSize )
				outText.resize( readBytes );
		}
		std::fclose( pFile );

		if ( outText.size() >= 3 &&
			 static_cast<uint8>( outText[0] ) == 0xEF &&
			 static_cast<uint8>( outText[1] ) == 0xBB &&
			 static_cast<uint8>( outText[2] ) == 0xBF )
		{
			outText.erase( 0, 3 );
		}

		return true;
	}

	bool FileUtil::writeTextFile( string_view fileName, string_view text )
	{
		const string filePath = normalizeSeparators( fileName );
		FILE*		 pFile{ nullptr };
#if defined( SW_PLATFORM_WINDOWS )
		fopen_s( &pFile, filePath.c_str(), "wb" );
#else
		pFile = fopen( filePath.c_str(), "wb" );
#endif
		if ( pFile == nullptr )
			return false;

		if ( text.empty() == false )
			std::fwrite( text.data(), 1, text.size(), pFile );
		std::fclose( pFile );
		return true;
	}

	string_view FileUtil::skipUtf8Bom( string_view text )
	{
		if ( text.size() >= 3 &&
			 static_cast<uint8>( text[0] ) == 0xEF &&
			 static_cast<uint8>( text[1] ) == 0xBB &&
			 static_cast<uint8>( text[2] ) == 0xBF )
		{
			return text.substr( 3 );
		}
		return text;
	}

	void FileUtil::skipUtf8Bom( const uint8*& pData, size_t& size )
	{
		if ( pData != nullptr && size >= 3 &&
			 pData[0] == 0xEF &&
			 pData[1] == 0xBB &&
			 pData[2] == 0xBF )
		{
			pData += 3;
			size -= 3;
		}
	}

	void FileUtil::openFileDialog( const FileDialogParams& params, FileDialogDelegate onSuccess )
	{
		std::thread(
			[delegateCallback = std::move( onSuccess ), params]
		{
			if ( delegateCallback.isBound() == false )
				return;

			vector<string> listResults;
			bool		   bSuccess{ false };

#if defined( SW_PLATFORM_WINDOWS )
			bSuccess = WindowsFileDialog::open( params, listResults );
#elif defined( SW_PLATFORM_LINUX )
			bSuccess = LinuxFileDialog::open( params, listResults );
#elif defined( SW_PLATFORM_MACOS )
			bSuccess = MacFileDialog::open( params, listResults );
#else
			(void)params;
			SW_LOG_WARNING( "openFileDialog is not supported on this platform." );
#endif

			if ( bSuccess && listResults.empty() == false )
				delegateCallback( listResults );
		} )
			.detach();
	}

	bool FileUtil::collectFiles( string_view directory, string_view filterExtension, vector<string>& outFilePathList, const bool bRecursive, const bool bNormalizePath )
	{
		if ( directoryExists( directory ) == false )
			return false;

		const std::filesystem::path directoryPath{ directory };
		const bool					bHasFilter = filterExtension.empty() == false;

		if ( bRecursive )
		{
			for ( const auto& entry : std::filesystem::recursive_directory_iterator{ directoryPath } )
			{
				if ( entry.is_directory() )
					continue;

				const string genericStd = entry.path().generic_string().c_str();
				string_view	 genericView{ genericStd };
				if ( bHasFilter && hasExtension( genericView, filterExtension ) == false )
					continue;

				outFilePathList.push_back( bNormalizePath ? normalizePath( genericView ) : string( genericView ) );
			}
		}
		else
		{
			for ( const auto& entry : std::filesystem::directory_iterator{ directoryPath } )
			{
				if ( entry.is_directory() )
					continue;

				const string genericStd = entry.path().generic_string().c_str();
				string_view	 genericView{ genericStd };
				if ( bHasFilter && hasExtension( genericView, filterExtension ) == false )
					continue;

				outFilePathList.push_back( bNormalizePath ? normalizePath( genericView ) : string( genericView ) );
			}
		}

		return true;
	}

	bool FileUtil::collectFolders( string_view directory, vector<string>& outFolderList, const bool bRecursive, const bool bNormalizePath )
	{
		if ( directoryExists( directory ) == false )
			return false;

		const std::filesystem::path directoryPath{ directory };

		if ( bRecursive )
		{
			for ( const auto& entry : std::filesystem::recursive_directory_iterator{ directoryPath } )
			{
				if ( entry.is_directory() == false )
					continue;

				const string genericStd = entry.path().generic_string().c_str();
				string_view	 genericView{ genericStd };
				outFolderList.push_back( bNormalizePath ? normalizePath( genericView ) : string( genericView ) );
			}
		}
		else
		{
			for ( const auto& entry : std::filesystem::directory_iterator{ directoryPath } )
			{
				if ( entry.is_directory() == false )
					continue;

				const string genericStd = entry.path().generic_string().c_str();
				string_view	 genericView{ genericStd };
				outFolderList.push_back( bNormalizePath ? normalizePath( genericView ) : string( genericView ) );
			}
		}

		return true;
	}

	string FileUtil::getClipboardText()
	{
#if defined( SW_PLATFORM_WINDOWS )
		if ( OpenClipboard( nullptr ) == FALSE )
			return string{};

		const HANDLE handle = GetClipboardData( CF_UNICODETEXT );
		if ( handle == nullptr )
		{
			CloseClipboard();
			return string{};
		}

		wstring		 wstr{};
		const WCHAR* pGlobalStr = static_cast<const WCHAR*>( GlobalLock( handle ) );
		if ( pGlobalStr != nullptr )
			wstr = pGlobalStr;

		GlobalUnlock( handle );
		CloseClipboard();

		return StringUtil::utf16ToUtf8( wstr.c_str() );
#elif defined( SW_PLATFORM_MACOS )
		FILE* pipe = popen( "pbpaste", "r" );
		if ( pipe == nullptr )
			return string{};
		utf8   buf[constant::kMaxBuffer512];
		string result;
		while ( fgets( buf, sizeof( buf ), pipe ) != nullptr )
		{
			result += buf;
		}
		pclose( pipe );
		return result;
#elif defined( SW_PLATFORM_LINUX )
		FILE* pipe = popen( "xclip -selection clipboard -o 2>/dev/null", "r" );
		if ( pipe == nullptr )
			return string{};
		utf8   buf[constant::kMaxBuffer512];
		string result;
		while ( fgets( buf, sizeof( buf ), pipe ) != nullptr )
		{
			result += buf;
		}
		pclose( pipe );
		return result;
#else
		return string{};
#endif
	}

	bool FileUtil::setClipboardText( string_view str )
	{
#if defined( SW_PLATFORM_WINDOWS )
		if ( OpenClipboard( nullptr ) == FALSE )
			return false;

		const string  strNt( str );
		const wstring wstr		   = StringUtil::utf8ToUtf16( strNt.c_str() );
		const uint32  stringLength = static_cast<uint32>( wstr.length() ) + 1;
		const HGLOBAL handle	   = GlobalAlloc( GMEM_MOVEABLE, stringLength * sizeof( WCHAR ) );
		if ( handle == nullptr )
		{
			CloseClipboard();
			return false;
		}

		WCHAR* pGlobalStr = static_cast<WCHAR*>( GlobalLock( handle ) );
		Memory::copy( pGlobalStr, wstr.c_str(), stringLength * sizeof( WCHAR ) );
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
		FILE* pPipe = popen( "pbcopy", "w" );
		if ( pPipe == nullptr )
			return false;
		fwrite( str.data(), 1, str.size(), pPipe );
		pclose( pPipe );
		return true;
#elif defined( SW_PLATFORM_LINUX )
		FILE* pPipe = popen( "xclip -selection clipboard 2>/dev/null", "w" );
		if ( pPipe == nullptr )
			return false;
		fwrite( str.data(), 1, str.size(), pPipe );
		pclose( pPipe );
		return true;
#else
		return false;
#endif
	}

	string_view FileUtil::getSharedLibraryPrefix()
	{
#if defined( SW_PLATFORM_WINDOWS )
		return "";
#else
		return "lib";
#endif
	}

	string_view FileUtil::getSharedLibraryExtension()
	{
#if defined( SW_PLATFORM_WINDOWS )
		return ".dll";
#elif defined( SW_PLATFORM_MACOS )
		return ".dylib";
#else
		return ".so";
#endif
	}

	string FileUtil::formatSharedLibraryName( string_view baseName )
	{
		StringBuilder<constant::kMaxBuffer128> sb;
		sb.append( getSharedLibraryPrefix() ).append( baseName ).append( getSharedLibraryExtension() );
		return string( sb.view() );
	}

	string FileUtil::getDebugSymbolPath( string_view libraryPath )
	{
#if defined( SW_PLATFORM_WINDOWS )
		return replaceExtension( libraryPath, ".pdb" );
#elif defined( SW_PLATFORM_MACOS ) || defined( SW_PLATFORM_APPLE )
		StringBuilder<constant::kMaxBuffer256> sb;
		sb.append( libraryPath ).append( ".dSYM" );
		return string( sb.view() );
#else
		return replaceExtension( libraryPath, ".debug" );
#endif
	}

	void* FileUtil::loadDynamicLibrary( string_view libraryName )
	{
		if ( libraryName.empty() )
			return nullptr;

#if defined( SW_PLATFORM_WINDOWS )
		string normalized = normalizePath( libraryName );
		string absPath;
		if ( makePathAbsolute( normalized, absPath ) && fileExists( absPath ) )
		{
			HMODULE hMod = LoadLibraryExA( absPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH );
			if ( hMod != nullptr )
				return hMod;
		}
		const string libraryNameNt( libraryName );
		return LoadLibraryA( libraryNameNt.c_str() );
#else
		string normalized = normalizePath( libraryName );
		string absPath;
		if ( makePathAbsolute( normalized, absPath ) && fileExists( absPath ) )
		{
			void* pHandle = dlopen( absPath.c_str(), RTLD_NOW | RTLD_LOCAL );
			if ( pHandle != nullptr )
				return pHandle;
		}
		const string libraryNameNt( libraryName );
		return dlopen( libraryNameNt.c_str(), RTLD_NOW | RTLD_LOCAL );
#endif
	}

	void* FileUtil::getDynamicSymbol( void* pHandle, string_view symbolName )
	{
		if ( pHandle == nullptr || symbolName.empty() )
			return nullptr;

		const string symbolNameNt( symbolName );
#if defined( SW_PLATFORM_WINDOWS )
		return reinterpret_cast<void*>( GetProcAddress( static_cast<HMODULE>( pHandle ), symbolNameNt.c_str() ) );
#else
		return dlsym( pHandle, symbolNameNt.c_str() );
#endif
	}

	void FileUtil::unloadDynamicLibrary( void* pHandle )
	{
		if ( pHandle == nullptr )
			return;

#if defined( SW_PLATFORM_WINDOWS )
		FreeLibrary( static_cast<HMODULE>( pHandle ) );
#else
		dlclose( pHandle );
#endif
	}
} // namespace sw
