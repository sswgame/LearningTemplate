#include "pch.h"

#include "ModuleManager/ModuleShadowCopy.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Module/ModuleLibraryUtil.h"
#include "Core/Process/Process.h"

#include "Engine/Module/EngineABIStamp.h"

#include "ModuleManager/ModuleLibraryPatch.h"
#include "ModuleManager/ShadowCopyName.h"

namespace sw
{
    SW_LOG_CALLER( "LiveReloadManager" );

    namespace
    {
        struct ModuleShadowCopyInternal
        {
            /** @brief 복사본 이름의 일련번호입니다(`ShadowCopyName::make`). 프로세스 하나에 하나씩 오릅니다. */
            inline static uint32 s_reloadCount{ 0 };
            /** @brief 라이브러리 이름 세대입니다(`rewriteLibraryNames`). 모듈과 무관하게 하나씩 오르므로 앞부분이 같은 두 모듈도 같은 이름을 받지 않습니다. */
            inline static uint32 s_libraryNameGeneration{ 0 };

            static void tryDeleteFile( string_view path )
            {
                // 아직 매핑된 그림자 사본은 지금 지울 수 없다. 남은 것은 다음 시작 · 종료의 ShadowCopyName::removeStaleCopies 가 지운다.
                (void)FileUtil::tryRemoveFile( path );
            }

            static void copyDebugSymbolsIfPresent( string_view originalModulePath, string_view shadowModulePath )
            {
                // 원본 심볼은 Bin/Symbols 에, 섀도 복사본의 심볼은 그 복사본 옆에 둔다.
                const string originalDebugPath = ModuleLibraryUtil::findBuiltDebugSymbolPath( originalModulePath );
                const string shadowDebugPath   = ModuleLibraryUtil::getDebugSymbolPath( shadowModulePath );
                if ( originalDebugPath.empty() )
                    return;

                if ( FileUtil::copyFile( originalDebugPath, shadowDebugPath ) == false )
                    SW_LOG_WARNING( "Failed to copy debug symbols: %#", shadowDebugPath.c_str() );
            }

            /**
             * @brief 섀도 복사본에 세대 이름을 주고(리눅스 SONAME, Windows 파일 이름), 복사본이 부르는 의존 모듈 이름을 그 의존의 **지금** 이름으로 바꿉니다.
             * @details 로더는 의존을 이름으로 풀고 같은 이름의 라이브러리가 이미 로드돼 있으면 그것을 쓴다. 복사본이 원본 이름을 그대로 가지고 있으면
             *          리눅스는 SONAME 이 같은 **먼저 로드된** 라이브러리(연쇄 리로드의 prepare 에서는 아직 언로드되지 않은 옛 라이브러리)에 바인딩되고,
             *          Windows 는 원본 DLL 을 한 벌 더 로드한다. 이름을 세대마다 고유하게 하면 이름이 가리키는 라이브러리가 하나뿐이다(`ModuleLibraryPatch`).
             *          그래서 모듈끼리는 지연 로드 없이 일반 import 로 바인딩된다. 첫 호출이 썽크를 지나는 일이 없다.
             * @return Windows 에서 세대 이름을 만들 수 없거나(이름이 너무 짧다), 복사본이 등록된 모듈을 의존 목록에 없이 원본 이름으로 부르면 false 입니다.
             *         리눅스는 늘 true 입니다(SONAME 이 없는 라이브러리는 바꾸지 않는다).
             */
            [[nodiscard]] static bool rewriteLibraryNames( const ShadowCopyParams& params, ShadowLibraryNameState& inoutLibraryName, vector<uint8>& inoutBytes )
            {
#if defined( SW_PLATFORM_WINDOWS )
                // 로더가 import 를 맞추는 이름은 DLL 파일 이름이다. 복사본에 세대 이름을 주고(파일 이름 — `writeShadowCopy`), 등록된 의존 모듈을 부르는
                // import 를 그 의존의 지금 이름으로 바꾼다. 같은 길이라야 import 표의 문자열을 제자리에서 덮는다.
                if ( inoutLibraryName._original.empty() )
                    inoutLibraryName._original = ModuleLibraryUtil::formatSharedLibraryName( params._moduleName );
                const string generationName = ModuleLibraryPatch::makeGenerationName( inoutLibraryName._original, ++s_libraryNameGeneration );
                if ( generationName.empty() )
                {
                    SW_LOG_ERROR( "Module %# cannot get a same-length generation name for its shadow copy (the name needs at least five characters before .dll)",
                                  params._moduleName );
                    return false;
                }
                inoutLibraryName._current = generationName;

                vector<string> listImportName;
                if ( ModuleLibraryPatch::readImportDLLNames( inoutBytes, ModuleLibraryPatch::ImportTable::Load, listImportName ) == false )
                {
                    SW_LOG_ERROR( "Module %# is not a PE32+ image this loader can read - refusing its shadow copy", params._moduleName );
                    return false;
                }
                // 이 매니저가 올리지 않은 의존(공용 GameFramework · Engine)은 목록에 없다 — 원래 이름 그대로 이미 올라온 라이브러리에 묶인다.
                for ( const ShadowCopyModuleLibrary& dependencyLibrary : params._listDependencyLibrary )
                {
                    const ShadowLibraryNameState& dependency = *dependencyLibrary._pLibraryName;
                    if ( dependency._original.empty() || dependency._current.empty() )
                        continue;
                    ModuleLibraryPatch::replaceImportDLLName( inoutBytes, dependency._original, dependency._current );
                }

                // 의존 목록에 없이 등록된 모듈을 원본 이름으로 부르면 OS 로더가 원본 DLL 을 한 벌 더 올린다(같은 모듈 두 벌 — 정적 상태 · 타입 등록이 갈린다).
                if ( ModuleLibraryPatch::readImportDLLNames( inoutBytes, ModuleLibraryPatch::ImportTable::Load, listImportName ) == false )
                    return false;
                for ( const string& importName : listImportName )
                {
                    for ( const ShadowCopyModuleLibrary& registeredLibrary : params._listRegisteredLibrary )
                    {
                        const ShadowLibraryNameState& registered   = *registeredLibrary._pLibraryName;
                        const bool                    bOtherModule = registeredLibrary._moduleName != params._moduleName && registered._original.empty() == false;
                        if ( bOtherModule && StringUtil::equals( importName, registered._original, true ) )
                        {
                            SW_LOG_ERROR( "Module %# imports %# but does not list %# as a dependency - its shadow copy would load a second image of it", params._moduleName,
                                          importName, registeredLibrary._moduleName );
                            return false;
                        }
                    }
                }
                return true;
#else
                // 복사본은 원본에서 막 복사했으므로 SONAME 은 늘 원본 이름이다. 처음 한 번 읽어 둔다(의존 모듈의 NEEDED 가 이 이름을 적고 있다).
                if ( inoutLibraryName._original.empty() )
                    (void)ModuleLibraryPatch::readSoname( inoutBytes, inoutLibraryName._original ); // 못 읽으면(ELF 가 아님) 이름이 비어 아래가 건너뛴다

                if ( inoutLibraryName._original.empty() == false )
                {
                    const string generationName = ModuleLibraryPatch::makeGenerationName( inoutLibraryName._original, ++s_libraryNameGeneration );
                    const bool   bRenamed       = generationName.empty() == false &&
                                          ModuleLibraryPatch::replaceDynamicString( inoutBytes, ModuleLibraryPatch::kTagSoname, inoutLibraryName._original, generationName ) > 0;
                    if ( bRenamed )
                        inoutLibraryName._current = generationName;
                }

                // 의존 모듈의 NEEDED 를 그 의존의 지금 이름으로. 연쇄 리로드는 의존 순서로 prepare 하므로, 같은 연쇄에서 바뀌는 의존은 이미
                // 새 이름을 가지고 있다(`_current`). 이 매니저가 로드하지 않은 의존(Engine 등)은 목록에 없어 원래 이름 그대로 둔다.
                for ( const ShadowCopyModuleLibrary& dependencyLibrary : params._listDependencyLibrary )
                {
                    const ShadowLibraryNameState& dependency = *dependencyLibrary._pLibraryName;
                    if ( dependency._original.empty() || dependency._current.empty() )
                        continue;
                    ModuleLibraryPatch::replaceDynamicString( inoutBytes, ModuleLibraryPatch::kTagNeeded, dependency._original, dependency._current );
                }
                return true;
#endif
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool ModuleShadowCopy::readModuleFile( string_view path, vector<uint8>& outBytes )
    {
        for ( int32 retryIndex = 0; retryIndex < 10; ++retryIndex )
        {
            if ( FileUtil::readFile( path, outBytes ) )
                return true;
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        }
        return false;
    }

    bool ModuleShadowCopy::rejectsDelayImports( string_view moduleName, string_view modulePath, const vector<uint8>& bytes )
    {
#if defined( SW_PLATFORM_WINDOWS )
        // 지연 import 디렉터리가 있으면 서술자를 읽든 못 읽든 올리지 않는다 — 지연 import 가 없다고 말할 수 없다.
        if ( ModuleLibraryPatch::hasImportDirectory( bytes, ModuleLibraryPatch::ImportTable::Delay ) == false )
            return false;
        vector<string> listDLLName;
        (void)ModuleLibraryPatch::readImportDLLNames( bytes, ModuleLibraryPatch::ImportTable::Delay, listDLLName ); // 로그의 이름만 — 못 읽으면 비어 있다
        SW_LOG_ERROR( "Module %# (%#) has delay-loaded imports (first: %#, %# DLL(s)) - module images must bind every import when they load, because the "
                      "lld delay-load thunk corrupts the float arguments of the first call. Rebuild it without /DELAYLOAD; refusing to load it",
                      moduleName, modulePath, listDLLName.empty() ? "unreadable import table" : listDLLName.front().c_str(),
                      static_cast<uint32>( listDLLName.size() ) );
        SW_ASSERT( false && "A module image with delay-loaded imports must not be loaded" );
        return true;
#else
        (void)moduleName;
        (void)modulePath;
        (void)bytes;
        return false;
#endif
    }

    bool ModuleShadowCopy::writeShadowCopy( const ShadowCopyParams& params, ShadowLibraryNameState& inoutLibraryName, string& outShadowPath )
    {
        ++ModuleShadowCopyInternal::s_reloadCount;
        const string tempName = ShadowCopyName::make( params._moduleName, Process::getCurrentProcessID(), ModuleShadowCopyInternal::s_reloadCount, params._sourceMtime );
        const string execDir  = FileUtil::getDirectoryPart( params._originalModulePath );

        // 원본을 한 번 읽어 대조하고(리눅스는 고치고) 복사본으로 쓴다. 대조에 걸리면 복사본을 만들기 전이라 치울 것이 없다.
        vector<uint8> bytes;
        if ( readModuleFile( params._originalModulePath, bytes ) == false )
        {
            SW_LOG_ERROR( "Failed to read the module for a shadow copy (locked): %#", params._originalModulePath );
            return false;
        }

        // 스탬프가 **없는** 모듈도 거절한다. 스탬프를 박지 않는 빌드 규칙으로 만든 모듈이 대조 없이 로드되지 않게 하기 위해서다(동적 모듈은 모두
        // `sw_registerDynamicModule` 이 스탬프를 박는다).
        string     moduleStamp;
        const bool bEngineABIMismatch = ModuleLibraryPatch::findEngineABIStamp( bytes, moduleStamp ) == false || moduleStamp != engine::getEngineABIStamp();
        if ( bEngineABIMismatch )
        {
            // 첫 로드면 지킬 옛 모듈이 없다. 엔진과 모듈 중 한쪽만 다시 빌드된 것이므로 둘을 함께 빌드해야 한다.
            const utf8* pHint = params._bReplacingLoadedLibrary ? "keeping the old module; restart to pick up the engine change"
                                                                : "rebuild the engine and its modules together";
            SW_LOG_ERROR( "Module %# was built against different Core/Engine headers than the running engine (module %#, engine %#) — %#",
                          params._moduleName, moduleStamp, engine::getEngineABIStamp(), pHint );
            return false;
        }
        if ( rejectsDelayImports( params._moduleName, params._originalModulePath, bytes ) )
            return false;
        if ( ModuleShadowCopyInternal::rewriteLibraryNames( params, inoutLibraryName, bytes ) == false )
        {
            inoutLibraryName._current = inoutLibraryName._loaded;
            return false;
        }

#if defined( SW_PLATFORM_WINDOWS )
        // 로더는 import 를 파일 이름으로 맞춘다 — 복사본 파일 이름이 곧 세대 이름이다. 다른 프로세스의 같은 세대 이름과 겹치지 않게 복사본마다 제 폴더에 둔다.
        const string shadowDirectory = FileUtil::joinPath( execDir, tempName );
        const string shadowPath      = FileUtil::joinPath( shadowDirectory, inoutLibraryName._current );
        const bool   bWritten        = FileUtil::ensureDirectoryExists( shadowDirectory ) && FileUtil::writeFile( shadowPath, bytes.data(), bytes.size() );
#else
        const string shadowPath = FileUtil::joinPath( execDir, ModuleLibraryUtil::formatSharedLibraryName( tempName ) );
        const bool   bWritten   = FileUtil::writeFile( shadowPath, bytes.data(), bytes.size() );
#endif
        if ( bWritten == false )
        {
            inoutLibraryName._current = inoutLibraryName._loaded;
            SW_LOG_ERROR( "Failed to write the shadow copy: %#", shadowPath.c_str() );
            removeArtifacts( shadowPath );
            return false;
        }

        ModuleShadowCopyInternal::copyDebugSymbolsIfPresent( params._originalModulePath, shadowPath );
        outShadowPath = shadowPath;
        return true;
    }

    void ModuleShadowCopy::removeArtifacts( string_view shadowPath )
    {
        if ( shadowPath.empty() )
            return;
        ModuleShadowCopyInternal::tryDeleteFile( shadowPath );
        ModuleShadowCopyInternal::tryDeleteFile( ModuleLibraryUtil::getDebugSymbolPath( shadowPath ) );
#if defined( SW_PLATFORM_WINDOWS )
        // Windows 복사본은 제 폴더(`<모듈>_temp_p<pid>_…/<세대 이름>.dll`)에 있다 — 비었으면 폴더도 지운다.
        string_view directoryPath;
        FileUtil::getDirectoryPart( shadowPath, directoryPath );
        int32 ownerProcessID{ 0 };
        if ( ShadowCopyName::parse( directoryPath, ownerProcessID ) )
            (void)ShadowCopyName::removeStaleCopies( FileUtil::getDirectoryPart( directoryPath ) ); // 아직 매핑된 것은 다음 정리가 지운다
#endif
    }
} // namespace sw
