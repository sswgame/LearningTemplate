#include "pch.h"

#include "Core/File/Windows/WindowsFileDialog.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    wstring WindowsFileDialog::makeFilterText( const FileDialogParams& params )
    {
        // 널이 구분자인 다중 문자열이다 — 널을 데이터로 담으므로 길이를 따로 드는 `wstring` 에 짓는다(`fixed_string` 은 첫 널에서 끝난다).
        wstring filterText = StringUtil::utf8ToUtf16( params._description.empty() ? "All Files" : params._description.c_str() );
        filterText.push_back( L'\0' );

        bool bHasPattern{ false };
        for ( const string& ext : params._listFilterExtension )
        {
            if ( ext.empty() )
                continue;
            if ( bHasPattern )
                filterText.push_back( L';' );
            filterText.append( ext[0] == '.' ? L"*" : L"*." );
            filterText.append( StringUtil::utf8ToUtf16( ext.c_str() ) );
            bHasPattern = true;
        }
        if ( bHasPattern == false )
            filterText.append( L"*.*" );

        filterText.push_back( L'\0' );
        filterText.push_back( L'\0' );
        return filterText;
    }

    bool WindowsFileDialog::open( const FileDialogParams& params, vector<string>& outListPath )
    {
        // 다중 선택 결과는 "dir\0file1\0file2\0\0" 처럼 널이 구분자라 맨 배열로 받는다.
        utf16 arrFileBuffer[constant::kMaxBuffer8192]{};

        OPENFILENAMEW ofn{};
        ofn.lStructSize  = sizeof( ofn );
        ofn.hwndOwner    = nullptr;
        ofn.lpstrFile    = arrFileBuffer;
        ofn.nMaxFile     = static_cast<DWORD>( constant::kMaxBuffer8192 );
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

        const wstring filterText = makeFilterText( params );
        ofn.lpstrFilter          = filterText.c_str();
        ofn.Flags                = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if ( params._bEnableMultiselect && params._type == FileDialogParams::Type::Open )
            ofn.Flags |= OFN_ALLOWMULTISELECT;

        BOOL result = FALSE;
        switch ( params._type )
        {
            case FileDialogParams::Type::Open:
            {
                result = GetOpenFileNameW( &ofn );
                break;
            }
            case FileDialogParams::Type::Save:
            {
                result = GetSaveFileNameW( &ofn );
                break;
            }
        }

        if ( result == FALSE )
            return false;

        const utf16* pCurrent = arrFileBuffer;
        if ( pCurrent == nullptr || *pCurrent == L'\0' )
            return false;

        // 다중 선택: "dir\0file1\0file2\0\0" / 단일 선택: "full\path\file\0"
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
