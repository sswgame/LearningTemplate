/**
 * @file EditorScratchDomain.h
 * @brief 에디터 테스트가 케이스 폴더를 리소스 도메인으로 붙이는 픽스처입니다.
 * @details 엔진 쪽 픽스처(`ScopedScratchDomain`)는 `EngineServices.h` 를 봐 에디터가 include 하지 못한다 — 에디터 창구(`editor::getSubsystem`)를 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetPaths.h"

namespace sw::test
{
    /** @brief 케이스 폴더 `<scratch>/<domain>` 을 도메인으로 붙이고 사라질 때 뗍니다. */
    class EditorScratchDomain
    {
    public:
        static constexpr const utf8* kDomain = "editortestdomain";

        EditorScratchDomain()
            : _folder{ FileUtil::normalizeSeparators( makeScratchPath( kDomain ) ) }
            , _bMounted{ SW_FALSE }
        {
            _bMounted = FileUtil::ensureDirectoryExists( _folder ) && editor::getSubsystem<AssetPaths>().mountDiskDomain( kDomain, _folder );
            SW_EXPECT_TRUE( _bMounted );
        }
        ~EditorScratchDomain()
        {
            if ( _bMounted )
                (void)editor::getSubsystem<AssetPaths>().unmountDiskDomain( kDomain );
        }
        EditorScratchDomain( const EditorScratchDomain& )            = delete;
        EditorScratchDomain& operator=( const EditorScratchDomain& ) = delete;

        /** @brief 도메인 폴더 아래 @p relativePath 의 디스크 경로입니다. */
        [[nodiscard]] string makeDiskPath( string_view relativePath ) const { return FileUtil::joinPath( _folder, relativePath ); }
        /** @brief 도메인 폴더 아래 @p relativePath 의 리소스 id 입니다. */
        [[nodiscard]] static string makeID( string_view relativePath ) { return string( kDomain ) + "/" + string( relativePath ); }

    private:
        string _folder;
        uint8  _bMounted : 1;
    };
} // namespace sw::test
