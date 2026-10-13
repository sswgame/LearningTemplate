/**
 * @file AssetHotReload.h
 * @brief 실행 중 `Resource/` 변경을 감지해 엔진 캐시가 다시 읽게 합니다. 에디터 전용 개발 기능이며 배포본에는 감시가 없습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/FileWatchDispatcher.h"

namespace sw
{
    class AssetManager;
    class GameObjectManager;
    class IRHIDevice;
} // namespace sw

namespace sw::editor
{
    /**
     * @class AssetHotReload
     * @brief `Resource/` 하위 변경을 폴링해, 그 파일을 가진 엔진 캐시(`IAssetCache`)가 다시 읽게 합니다.
     * @details 종류별 코드가 없다. 어느 캐시가 읽을지, 먼저 쿠킹할지는 `EditorAssetTypeRegistry::findReloadRoute` 의 테이블이 정한다.
     *          엔진 캐시는 자기 런타임 파일을 그 자리에서 다시 읽을 뿐이고(로드), 소스 임포트, 감시, 씬 알림은 여기(에디터)에 있다.
     *          새 에셋 종류를 추가해도 이 파일은 고치지 않는다.
     */
    class AssetHotReload : public EditorSubsystem
    {
    public:
        /** @brief 감시를 시작하지 않은 채 만듭니다. */
        AssetHotReload();
        /** @brief 감시를 정리합니다. */
        ~AssetHotReload() override;

        // EditorSubsystem — EditorContext 가 의존 순서로 초기화하고 역순으로 종료 · 소멸한다.
        SubsystemInitResult initializeSubsystem() override
        {
            (void)initialize(); // 리소스 루트가 없으면(팩만 실린 실행) 꺼진 채로 둔다
            return SubsystemInitResult::Succeeded;
        }
        void shutdownSubsystem() override { shutdown(); }
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "AssetHotReload";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const>      getSubsystemDependencies() const override;

        AssetHotReload( const AssetHotReload& )            = delete;
        AssetHotReload& operator=( const AssetHotReload& ) = delete;

        /**
         * @brief 파일 감시를 시작하고 `Resource/` 워치를 등록합니다.
         * @return 감시자를 만들었으면 true 입니다. 리소스 루트가 없으면(팩만 있는 실행) false 입니다.
         */
        bool initialize();

        /** @brief 워치를 해제하고 감시 스레드를 멈춥니다. */
        void shutdown();

        /** @brief 에디터 프레임마다 호출해 변경 이벤트를 꺼냅니다. */
        void update();

        /**
         * @brief `Resource/` 아래의 파일이나 폴더가 바뀔 때마다 하나씩 올라가는 번호입니다. 종류와 무관하게 만들기, 지우기, 이름 바꾸기, 고치기가 모두 해당합니다.
         * @details 디스크 목록을 가진 화면(콘텐츠 브라우저)은 지난번 번호와 다르면 목록을 다시 읽는다. 에디터 바깥(탐색기, git)의 변경도 센다.
         */
        uint64 getContentChangeSerial() const { return _contentChangeSerial; }

        /**
         * @brief 바뀐 에셋 하나를 다시 읽습니다. 임포터(있으면), 엔진 캐시의 `reload`, 활성 씬의 `notifyAssetUsers` 순서로 처리합니다. 게임 스레드에서 틱 밖에서 호출합니다.
         * @return 핫 리로드 대상이고 처리했으면 true 입니다. 대상이 아니거나 그 이름의 캐시가 등록되지 않았으면 false 입니다.
         */
        [[nodiscard]] static bool reloadChangedAsset( string_view relativePath );

        /**
         * @brief @p resources 의 레지스트리에서 그 경로를 가진 캐시(`IAssetCache::isCached`)마다 `reload` 를 호출합니다. 이름을 모르는 모듈 캐시를 처리하는 경로입니다.
         * @return 다시 읽게 한 캐시 수입니다.
         */
        static uint32 reloadInCachesHolding( AssetManager& resources, string_view relativePath, IRHIDevice* pDevice );

        /**
         * @brief @p objects 에서 그 에셋을 쓰는 컴포넌트를 찾아, 인스펙터에서 그 프로퍼티를 고쳤을 때처럼 `onPropertyChanged` 를 호출합니다. 언리얼의 PostEditChange 에 해당합니다.
         * @details 찾는 기준은 리플렉션이다. 값이 그 경로인 에셋 경로 프로퍼티(`PROPERTY( AssetPath )`, `AssetType = …`, 문자열이나 hashed_string)를 찾는다.
         *          컴포넌트에는 리로드 전용 코드가 필요 없다. 에셋에서 계산한 상태는 `onPropertyChanged` 가 값이 같아도 다시 맞춰야 한다.
         * @return `onPropertyChanged` 를 호출한 프로퍼티 수입니다.
         */
        static uint32 notifyAssetUsers( GameObjectManager& objects, string_view relativePath );

    private:
        /** @brief 바뀐 파일 하나를 `reloadChangedAsset` 으로 보냅니다. 수정 이벤트만 처리합니다. */
        void onResourceFileChanged( const FileChangeEvent& changeEvent );
        /** @brief `Resource/` 아래의 어떤 변경이든 변경 번호를 올립니다(`getContentChangeSerial`). */
        void onResourceTreeChanged( const FileChangeEvent& changeEvent );

        unique_ptr<FileWatchDispatcher> _pFileWatchDispatcher;
        FileWatchHandle                 _resourceWatchHandle;
        FileWatchHandle                 _contentWatchHandle; ///< 확장자와 무관한 감시다. 변경 번호만 올린다
        uint64                          _contentChangeSerial;
    };
} // namespace sw::editor
