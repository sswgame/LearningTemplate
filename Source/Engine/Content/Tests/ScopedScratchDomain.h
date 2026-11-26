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
#include "Core/Foundation/Constants.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineSubsystems.h"

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
            : _folder{ makeScratchPath( kDomain ) }
            , _bMounted{ SW_FALSE }
        {
            _bMounted = FileUtil::ensureDirectoryExists( _folder ) && engine::getSubsystem<AssetPaths>().mountDiskDomain( kDomain, _folder );
            if ( _bMounted == false )
            {
                fixed_string<constant::buffer::kMax512> text;
                formatstring( text.data(), text.capacity(), "could not mount the scratch domain at %#", _folder.str() );
                reportFailure( __FILE__, __LINE__, text );
            }
        }
        ~ScopedScratchDomain()
        {
            if ( _bMounted )
                (void)engine::getSubsystem<AssetPaths>().unmountDiskDomain( kDomain ); // 붙인 것만 떼므로 실패하면 AssetPaths 가 오류로 남긴다
        }
        ScopedScratchDomain( const ScopedScratchDomain& )            = delete;
        ScopedScratchDomain& operator=( const ScopedScratchDomain& ) = delete;

        /** @brief 도메인 폴더(디스크 경로 형식 — I/O 에 그대로 넘긴다, 글이 필요하면 `.str()`)입니다. */
        [[nodiscard]] const AbsolutePath& getFolder() const { return _folder; }
        /** @brief 도메인 폴더 아래 @p relativePath 의 디스크 경로입니다(`AbsolutePath::join` — I/O 에 그대로 넘긴다, 글이 필요하면 `.str()`). */
        [[nodiscard]] AbsolutePath makeDiskPath( string_view relativePath ) const { return _folder.join( relativePath ); }
        /** @brief 도메인 폴더 아래 @p relativePath 폴더를 만들고 그 디스크 경로를 돌려줍니다(못 만들면 케이스 실패). */
        [[nodiscard]] AbsolutePath makeDiskFolder( string_view relativePath ) const
        {
            const AbsolutePath folder = makeDiskPath( relativePath );
            if ( FileUtil::ensureDirectoryExists( folder ) == false )
            {
                fixed_string<constant::buffer::kMax512> text;
                formatstring( text.data(), text.capacity(), "could not create %#", folder.str() );
                reportFailure( __FILE__, __LINE__, text );
            }
            return folder;
        }
        /** @brief 도메인 폴더 아래 @p relativePath 의 리소스 id(`testproject/<relativePath>`)입니다. */
        [[nodiscard]] static AssetID makeID( string_view relativePath )
        {
            StringBuilder<constant::buffer::kMax256> builder;
            builder.appendFormat( "%#/%#", kDomain, relativePath );
            return AssetID{ builder.view() };
        }
        /** @brief 도메인 폴더 아래 디스크 경로 @p diskPath 의 리소스 id 입니다(밖이면 빈 id). */
        [[nodiscard]] static AssetID toID( const AbsolutePath& diskPath ) { return AssetID{ toIDText( diskPath ) }; }
        /** @brief `toID` 의 글입니다(경로 글을 받는 컴포넌트 칸 — 클립 폴더 · 그래프 경로 — 에 넣는다). 밖이면 케이스 실패이고 빈 글입니다. */
        [[nodiscard]] static string toIDText( const AbsolutePath& diskPath )
        {
            string id = engine::getSubsystem<AssetPaths>().toAssetID( diskPath );
            if ( id.empty() )
            {
                fixed_string<constant::buffer::kMax512> text;
                formatstring( text.data(), text.capacity(), "not under a mounted domain: %#", diskPath.str() );
                reportFailure( __FILE__, __LINE__, text );
            }
            return id;
        }

    private:
        AbsolutePath _folder;
        uint8        _bMounted : 1;
    };
} // namespace sw::test
