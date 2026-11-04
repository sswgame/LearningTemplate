/**
 * @file ScopedScratchDomain.h
 * @brief 케이스 폴더(`test::makeScratchPath`) 아래 임시 프로젝트를 리소스 도메인으로 붙이는 테스트 픽스처입니다.
 * @details 리소스 입구(로더 · 캐시 · `LocalizationManager::mountProject`)는 리소스 id 만 받는다. 케이스가 디스크에 쓴 파일을 그 입구로 읽으려면
 *          폴더를 도메인으로 붙여야 한다(`AssetPaths::mountDiskDomain`). 옛 하네스(`makeTempResourceID`)는 프로세스 임시 폴더를 한 번 붙이고 떼지 않았다 —
 *          이 픽스처는 케이스 폴더만 붙이고 소멸자에서 뗀다(케이스 순서가 섞여도 앞 케이스의 도메인이 남지 않는다).
 *          언리얼 자동화 테스트가 임시 마운트 지점(`FPackageName::RegisterMountPoint` / `UnRegisterMountPoint`)을 짝으로 쓰는 것과 같다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"

namespace sw::test
{
    /**
     * @class ScopedScratchDomain
     * @brief 케이스 폴더 `<scratch>/<domain>` 을 도메인 `<domain>` 으로 붙이고, 사라질 때 뗍니다.
     */
    class ScopedScratchDomain
    {
    public:
        /** @brief 이 픽스처가 쓰는 도메인 이름입니다(리소스 트리의 도메인과 겹치지 않는 이름). */
        static constexpr const utf8* kDomain = "testproject";

        ScopedScratchDomain()
            : _folder{ FileUtil::normalizeSeparators( makeScratchPath( kDomain ) ) }
            , _bMounted{ SW_FALSE }
        {
            _bMounted = FileUtil::ensureDirectoryExists( _folder ) && engine::getSubsystem<AssetPaths>().mountDiskDomain( kDomain, _folder );
            if ( _bMounted == false )
                reportFailure( __FILE__, __LINE__, "could not mount the scratch domain at " + _folder );
        }
        ~ScopedScratchDomain()
        {
            if ( _bMounted )
                (void)engine::getSubsystem<AssetPaths>().unmountDiskDomain( kDomain ); // 붙인 것만 떼므로 실패하면 AssetPaths 가 오류로 남긴다
        }
        ScopedScratchDomain( const ScopedScratchDomain& )            = delete;
        ScopedScratchDomain& operator=( const ScopedScratchDomain& ) = delete;

        /** @brief 도메인 폴더(디스크 절대 경로)입니다. */
        [[nodiscard]] const string& getFolder() const { return _folder; }
        /** @brief 도메인 폴더 아래 @p relativePath 의 디스크 경로입니다. */
        [[nodiscard]] string makeDiskPath( string_view relativePath ) const { return FileUtil::joinPath( _folder, relativePath ); }
        /** @brief 도메인 폴더 아래 @p relativePath 폴더를 만들고 그 디스크 경로를 돌려줍니다(못 만들면 케이스 실패). */
        [[nodiscard]] string makeDiskFolder( string_view relativePath ) const
        {
            const string folder = makeDiskPath( relativePath );
            if ( FileUtil::ensureDirectoryExists( folder ) == false )
                reportFailure( __FILE__, __LINE__, "could not create " + folder );
            return folder;
        }
        /** @brief 도메인 폴더 아래 @p relativePath 의 리소스 id(`testproject/<relativePath>`)입니다. */
        [[nodiscard]] static AssetID makeID( string_view relativePath ) { return AssetID{ string( kDomain ) + "/" + string( relativePath ) }; }
        /** @brief 도메인 폴더 아래 디스크 경로 @p diskPath 의 리소스 id 입니다(밖이면 빈 id). */
        [[nodiscard]] static AssetID toID( string_view diskPath ) { return AssetID{ toIDText( diskPath ) }; }
        /** @brief `toID` 의 글입니다(경로 글을 받는 컴포넌트 칸 — 클립 폴더 · 그래프 경로 — 에 넣는다). 밖이면 케이스 실패이고 빈 글입니다. */
        [[nodiscard]] static string toIDText( string_view diskPath )
        {
            string id = engine::getSubsystem<AssetPaths>().toAssetID( diskPath );
            if ( id.empty() )
                reportFailure( __FILE__, __LINE__, "not under a mounted domain: " + string( diskPath ) );
            return id;
        }

    private:
        string _folder;
        uint8  _bMounted : 1;
    };
} // namespace sw::test
