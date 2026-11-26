/**
 * @file DevSourcePath.h
 * @brief 소스 파일 경로가 개발 전용 소스(배포 구성이 짓지 않는 자리)인지 컴파일 시간에 가립니다(`isDevelopmentSourcePath`).
 * @details 자리 표는 `Config/Rules/Layers/DevFolders.json` 하나다(생성 헤더 `sw/config/DevSourceFolders.gen.h`). 배포에서 `Dev/` 를 빼는 CMake 도 같은 표를 읽는다.
 *          개발 등록 매크로(개발 명령 · 감시 값 · 시나리오 단계 · 디버그 HUD)와 테스트 전역 변수가 `__FILE__` 으로 부른다 — 개발 구성에서도 그 밖 파일에 두면 컴파일되지 않는다.
 *          같은 DLL 안의 폴더라 include 게이트로는 가르지 못하고, 유니티 빌드는 파일마다 정의를 달리할 수 없어 경로를 상수로 본다.
 */
#pragma once
#include "sw/config/DevSourceFolders.gen.h"

namespace sw
{
    namespace DevSourcePathInternal
    {
        constexpr bool isSeparator( char ch )
        {
            return ch == '/' || ch == '\\';
        }

        /** @brief [@p pBegin, @p pEnd) 가 @p pName 과 같으면 true 입니다. */
        constexpr bool equalsSegment( const char* pBegin, const char* pEnd, const char* pName )
        {
            for ( ; pBegin != pEnd; ++pBegin, ++pName )
            {
                if ( *pName == '\0' || *pBegin != *pName )
                    return false;
            }
            return *pName == '\0';
        }

        /** @brief 마지막 `Source` 폴더 바로 아래를 가리킵니다(체크아웃 경로에 든 같은 이름은 세지 않는다). 없으면 nullptr 입니다. */
        constexpr const char* findSourceRelative( const char* pPath )
        {
            const char* pFound   = nullptr;
            const char* pSegment = pPath;
            for ( const char* p = pPath; *p != '\0'; ++p )
            {
                if ( isSeparator( *p ) )
                {
                    if ( equalsSegment( pSegment, p, "Source" ) )
                        pFound = p + 1;
                    pSegment = p + 1;
                }
            }
            return pFound;
        }

        /** @brief @p pPath 가 @p pPrefix(`/` 구분)로 시작하면 그 뒤를, 아니면 nullptr 을 돌려줍니다. */
        constexpr const char* matchPrefix( const char* pPath, const char* pPrefix )
        {
            for ( ; *pPrefix != '\0'; ++pPath, ++pPrefix )
            {
                const bool bSame = *pPrefix == '/' ? isSeparator( *pPath ) : *pPath == *pPrefix;
                if ( bSame == false )
                    return nullptr;
            }
            return pPath;
        }

        /** @brief @p pPath 의 폴더(마지막 조각인 파일 이름은 빼고) 가운데 이름이 @p pName 인 것이 있으면 true 입니다. */
        constexpr bool hasFolder( const char* pPath, const char* pName )
        {
            const char* pSegment = pPath;
            for ( const char* p = pPath; *p != '\0'; ++p )
            {
                if ( isSeparator( *p ) )
                {
                    if ( equalsSegment( pSegment, p, pName ) )
                        return true;
                    pSegment = p + 1;
                }
            }
            return false;
        }

        /** @brief 접두어가 `Dev/` 폴더 하나를 가리키면(`…/Dev/`) true 입니다. */
        constexpr bool isSingleDevFolder( const char* pPrefix )
        {
            const char* pEnd = pPrefix;
            while ( *pEnd != '\0' )
            {
                ++pEnd;
            }
            return pEnd - pPrefix >= 5 && matchPrefix( pEnd - 5, "/Dev/" ) == pEnd;
        }
    } // namespace DevSourcePathInternal

    /**
     * @brief @p pPath(`__FILE__`)가 개발 전용 소스면 true 입니다 — 마지막 `Source` 폴더 아래에서 `Tests/` 폴더 안, 모듈 뿌리(DevTools · Editor …) 아래,
     *        표의 자리 아래 `Dev/` 안, 또는 표의 자리 아래 `Editor/` 안이다. 구분자는 `/` · `\` 둘 다 받는다. `Source` 가 경로에 없으면 폴백 없이 false 다.
     * @details 상수식이고 경로를 표의 줄 수만큼 앞에서 훑는다 — 매크로 하나에 수백 글자 비교다.
     */
    constexpr bool isDevelopmentSourcePath( const char* pPath )
    {
        using namespace DevSourcePathInternal;
        const char* pRelative = findSourceRelative( pPath );
        if ( pRelative == nullptr )
            return false;
        if ( hasFolder( pRelative, "Tests" ) )
            return true;
        for ( const char* pRoot : kArrDevModuleRoot )
        {
            if ( matchPrefix( pRelative, pRoot ) != nullptr )
                return true;
        }
        for ( const char* pPrefix : kArrDevFolderPrefix )
        {
            const char* pRest = matchPrefix( pRelative, pPrefix );
            if ( pRest != nullptr && ( isSingleDevFolder( pPrefix ) || hasFolder( pRest, "Dev" ) ) )
                return true;
        }
        for ( const char* pPrefix : kArrEditorFolderPrefix )
        {
            const char* pRest = matchPrefix( pRelative, pPrefix );
            if ( pRest != nullptr && hasFolder( pRest, "Editor" ) )
                return true;
        }
        return false;
    }

    static_assert( isDevelopmentSourcePath( "D:/Repo/Source/DevTools/Network/NetEmulationSubsystem.cpp" ) );
    static_assert( isDevelopmentSourcePath( "D:\\Repo\\Source\\Editor\\SelfTest\\EditorWatchState.h" ) );
    static_assert( isDevelopmentSourcePath( "/home/u/Repo/Source/Games/Playground/Dev/PlaygroundAutoplay.cpp" ) );
    static_assert( isDevelopmentSourcePath( "Source/Engine/Config/Tests/TestGlobalVariableMacro.cpp" ) );
    static_assert( isDevelopmentSourcePath( "D:/Repo/Source/GameFramework/Developer/GameSettingsLaunchOverride.cpp" ) );
    static_assert( isDevelopmentSourcePath( "D:/Repo/Source/GameFramework/Kits/Genre/RPG/ClassicJRPG/Dev/JRPGDevTools.cpp" ) );
    static_assert( isDevelopmentSourcePath( "D:/Repo/Source/GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Editor/X.cpp" ) );
    static_assert( isDevelopmentSourcePath( "D:/Repo/Source/Engine/Network/Dev/FileserveClient.cpp" ) );
    static_assert( isDevelopmentSourcePath( "D:/Repo/Source/GameFramework/Runtime/Developer/X.cpp" ) == false );
    static_assert( isDevelopmentSourcePath( "D:/Dev/Repo/Source/Engine/Audio/AudioEngine.cpp" ) == false );
    static_assert( isDevelopmentSourcePath( "D:/Repo/Source/Games/Playground/DevilComponent.cpp" ) == false );
    static_assert( isDevelopmentSourcePath( "D:/Repo/Source/Engine/Audio/Dev/X.cpp" ) == false ); // 표 밖 Dev/ — CMake 도 configure 를 멈춘다
    static_assert( isDevelopmentSourcePath( "D:/Repo/Source/GameFramework/Runtime/Editor/X.cpp" ) == false );
    static_assert( isDevelopmentSourcePath( "D:/Repo/Source/Engine/Tests.cpp" ) == false );
    static_assert( isDevelopmentSourcePath( "Engine/Config/Tests/X.cpp" ) == false );
} // namespace sw

/**
 * @brief 개발 등록 매크로(`pMacroName`)를 쓴 파일이 개발 전용 소스인지 컴파일에서 봅니다. 등록 매크로의 개발 구성 정의가 맨 앞에 둔다.
 * @details 배포 구성은 그 매크로 자체가 오류다(배포는 개발 전용 소스를 짓지 않는다). 개발 구성도 이 검사로 같은 자리를 지킨다 — 개발 빌드에서 바로 드러난다.
 */
#define SW_DEV_REGISTRATION_PATH_CHECK( pMacroName ) static_assert( ::sw::isDevelopmentSourcePath( __FILE__ ), pMacroName " belongs in a development-only source (a folder of Config/Rules/Layers/DevFolders.json or Tests/)" )
