#include "pch.h"

#include "Core/File/Windows/WindowsFileDialog.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
	bool WindowsFileDialog::open( const FileDialogParams& params, vector<string>& outPaths )
	{
		fixed_wstring<constant::kMaxBuffer8192> szFile;

		OPENFILENAMEW ofn{};
		ofn.lStructSize	 = sizeof( ofn );
		ofn.hwndOwner	 = nullptr;
		ofn.lpstrFile	 = szFile.data();
		ofn.nMaxFile	 = static_cast<DWORD>( szFile.capacity() );
		ofn.nFilterIndex = 1;

		wstring titleW;
		if ( params._title.empty() == false )
		{
			titleW		   = StringUtil::utf8ToUtf16( params._title.c_str() );
			ofn.lpstrTitle = titleW.c_str();
		}

		wstring initialDirW;
		if ( params._initialDirectory.empty() == false )
		{
			const string initialDirNt = FileUtil::normalizePath( params._initialDirectory );
			initialDirW				  = StringUtil::utf8ToUtf16( initialDirNt.c_str() );
			ofn.lpstrInitialDir		  = initialDirW.c_str();
		}

		fixed_string<constant::kMaxBuffer4096> filter;
		const utf8*							   desc = params._description.empty() ? "All Files" : params._description.c_str();
		filter.append( desc );
		filter.push_back( 0 );

		if ( params._listFilterExtension.empty() )
		{
			filter.append( "*.*" );
			filter.push_back( 0 );
		}
		else
		{
			for ( size_t filterIndex = 0; filterIndex < params._listFilterExtension.size(); ++filterIndex )
			{
				const string_view filterExtension = params._listFilterExtension[filterIndex];
				if ( filterIndex > 0 )
					filter.push_back( ';' );
				filter.push_back( '*' );
				if ( filterExtension.empty() == false && filterExtension[0] != '.' )
					filter.push_back( '.' );
				filter.append( filterExtension );
			}
			filter.push_back( 0 );
		}
		filter.push_back( 0 );

		const wstring filterW = StringUtil::utf8ToUtf16( filter.c_str() );
		ofn.lpstrFilter		  = filterW.c_str();

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

		if ( result == FALSE )
			return false;

		const utf16* p = szFile.data();
		if ( p == nullptr || *p == L'\0' )
			return false;

		// Multi-select: "dir\0file1\0file2\0\0" / Single: "full\path\file\0"
		const wstring first( p );
		p += first.size() + 1;
		if ( *p == L'\0' )
			outPaths.push_back( FileUtil::normalizePath( StringUtil::utf16ToUtf8( first.c_str() ) ) );
		else
		{
			const string directoryPath = FileUtil::normalizePath( StringUtil::utf16ToUtf8( first.c_str() ) );
			while ( *p != L'\0' )
			{
				const wstring fileNameW( p );
				outPaths.push_back( FileUtil::normalizePath( FileUtil::joinPath( directoryPath, StringUtil::utf16ToUtf8( fileNameW.c_str() ) ) ) );
				p += fileNameW.size() + 1;
			}
		}

		return true;
	}
} // namespace sw

#endif
