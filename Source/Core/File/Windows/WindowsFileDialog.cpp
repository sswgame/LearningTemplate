#include "pch.h"

#include "Core/File/Windows/WindowsFileDialog.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    bool WindowsFileDialog::open( const FileDialogParams& params, vector<string>& outListPath )
    {
        fixed_wstring<constant::kMaxBuffer8192> szFile;

        OPENFILENAMEW ofn{};
        ofn.lStructSize  = sizeof( ofn );
        ofn.hwndOwner    = nullptr;
        ofn.lpstrFile    = szFile.data();
        ofn.nMaxFile     = static_cast<DWORD>( szFile.capacity() );
        ofn.nFilterIndex = 1;

        wstring titleW;
        if ( params._title.empty() == false )
        {
            titleW         = StringUtil::utf8ToUtf16( params._title.c_str() );
            ofn.lpstrTitle = titleW.c_str();
        }

        wstring initialDirW;
        if ( params._initialDirectory.empty() == false )
        {
            const string initialDirNt = FileUtil::normalizePath( params._initialDirectory );
            initialDirW               = StringUtil::utf8ToUtf16( initialDirNt.c_str() );
            ofn.lpstrInitialDir       = initialDirW.c_str();
        }

        fixed_string<constant::kMaxBuffer4096> filter;
        const utf8*                            desc = params._description.empty() ? "All Files" : params._description.c_str();
        filter.append( desc );
        filter.push_back( 0 );

        if ( params._listFilterExtension.empty() )
        {
            filter.append( "*.*" );
            filter.push_back( 0 );
        }
        else
        {
            for ( const string& ext : params._listFilterExtension )
            {
                if ( ext.empty() )
                    continue;
                if ( ext[0] == '.' )
                    filter.append( "*" );
                filter.append( ext );
                filter.append( ";" );
            }
            filter.push_back( 0 );
        }
        filter.push_back( 0 );

        const wstring filterW = StringUtil::utf8ToUtf16( filter.c_str() );
        ofn.lpstrFilter       = filterW.c_str();
        ofn.Flags             = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if ( params._bEnableMultiselect && params._type == FileDialogParams::Type::Open )
            ofn.Flags |= OFN_ALLOWMULTISELECT;

        BOOL result = FALSE;
        switch ( params._type )
        {
            case FileDialogParams::Type::Open:
                result = GetOpenFileNameW( &ofn );
                break;
            case FileDialogParams::Type::Save:
                result = GetSaveFileNameW( &ofn );
                break;
            default:
                break;
        }

        if ( result == FALSE )
            return false;

        const utf16* pCurrent = szFile.data();
        if ( pCurrent == nullptr || *pCurrent == L'\0' )
            return false;

        // Multi-select: "dir\0file1\0file2\0\0" / Single: "full\path\file\0"
        const wstring first( pCurrent );
        pCurrent += first.size() + 1;
        if ( *pCurrent == L'\0' )
            outListPath.push_back( FileUtil::normalizePath( StringUtil::utf16ToUtf8( first.c_str() ) ) );
        else
        {
            const string directoryPath = FileUtil::normalizePath( StringUtil::utf16ToUtf8( first.c_str() ) );
            while ( *pCurrent != L'\0' )
            {
                const wstring fileNameW( pCurrent );
                outListPath.push_back( FileUtil::normalizePath( FileUtil::joinPath( directoryPath, StringUtil::utf16ToUtf8( fileNameW.c_str() ) ) ) );
                pCurrent += fileNameW.size() + 1;
            }
        }

        return true;
    }
} // namespace sw

#endif
