#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Process/Process.h"

#include "Engine/Foundation/Foundation.h"

#include "Programs/ReflectionParser/ParserConfig.h"
#include "Programs/ReflectionParser/ParserDefines.h"

namespace sw
{
    void ParserConfig::appendToolchainIncludes( string llvmPath, [[maybe_unused]] const string& msvcToolsDir, [[maybe_unused]] const string& winSdkDir,
                                                [[maybe_unused]] const string& winSdkVer, [[maybe_unused]] const string_view platformParserKey )
    {
        BLOCK( "Locate LLVM and Clang Resource Directory" )
        {
            if ( llvmPath.empty() )
            {
                sw::string  pEnvLlvmText;
                const utf8* pEnvLlvm = sw::Process::tryReadEnvironment( jsonkey::kEnvLlvmDir, pEnvLlvmText ) ? pEnvLlvmText.c_str() : nullptr;
                if ( pEnvLlvm == nullptr )
                    pEnvLlvm = sw::Process::tryReadEnvironment( jsonkey::kEnvLlvmHome, pEnvLlvmText ) ? pEnvLlvmText.c_str() : nullptr;
                if ( pEnvLlvm != nullptr )
                    llvmPath = pEnvLlvm;
            }

            const AbsolutePath llvmClangDir = FileUtil::toDiskPath( llvmPath ).join( _llvmClangRel );
            if ( FileUtil::isDirectory( llvmClangDir ) )
            {
                vector<AbsolutePath> listClangSubFolder;
                if ( FileUtil::collectFolders( llvmClangDir, listClangSubFolder, false ) == false )
                    SW_LOG_ERROR_ID( "ParserConfig.ClangResourceFoldersUnder", "clang resource folders under '%#' could not all be listed - the built-in headers may be left out", llvmClangDir.str() );
                for ( const AbsolutePath& folder : listClangSubFolder )
                {
                    const string       resourceDir = FileUtil::normalizeSeparators( folder.str() );
                    const AbsolutePath clangInc    = folder.join( _clangIncludeRel );
                    if ( FileUtil::isDirectory( clangInc ) == false )
                        continue;

                    _listBaseArg.emplace_back( _flagResourceDir );
                    _listBaseArg.emplace_back( resourceDir );
                    _listBaseArg.emplace_back( _flagIsystem );
                    _listBaseArg.emplace_back( clangInc.str() );
                    break;
                }
            }
        }

#if defined( SW_PLATFORM_WINDOWS )
        if ( platformParserKey == "windows" ) // 대상이 Windows 일 때만 MSVC · SDK 헤더를 읽는다(교차 대상은 자기 sysroot 를 `--target-arg` 로 받는다)
            BLOCK( "Locate MSVC and Windows SDK Includes" )
            {
                const AbsolutePath msvcInc = FileUtil::toDiskPath( msvcToolsDir ).join( _msvcIncludeRel );
                if ( msvcToolsDir.empty() == false && FileUtil::isDirectory( msvcInc ) )
                {
                    _listBaseArg.emplace_back( _flagIsystem );
                    _listBaseArg.emplace_back( msvcInc.str() );
                }

                if ( winSdkDir.empty() == false && winSdkVer.empty() == false )
                {
                    const AbsolutePath ucrtPath = FileUtil::toDiskPath( winSdkDir ).join( _winSdkIncludeRel ).join( winSdkVer ).join( _winSdkUcrtRel );
                    if ( FileUtil::isDirectory( ucrtPath ) )
                    {
                        _listBaseArg.emplace_back( _flagIsystem );
                        _listBaseArg.emplace_back( ucrtPath.str() );
                    }
                }
            }
#endif
    }
} // namespace sw
