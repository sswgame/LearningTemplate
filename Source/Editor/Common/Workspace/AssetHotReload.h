/**
 * @file AssetHotReload.h
 * @brief 실행 중 `Resource/` 변경을 감지해 엔진 캐시가 다시 읽게 합니다. 에디터 전용 개발 기능이며 배포본에는 감시가 없습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/FileWatchDispatcher.h"

#include "Engine/Serialization/Base/ReloadOutcome.h"

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
     *          엔진 캐시는 자기 런타임 파일을 그 즉시 다시 읽을 뿐이고(로드), 소스 임포트, 감시, 씬 알림은 여기(에디터)에 있다.
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

        /** @brief 동시에 기억하는 바뀐 파일 수의 한도입니다. 넘으면 개별 파일을 알 수 없는 것으로 치고 전체를 다시 읽게 합니다. */
        static constexpr size_t kMaxTrackedChangedPaths = 256;

        /**
         * @brief 마지막으로 꺼낸(또는 버린) 뒤 바뀐 파일의 리소스 id(소문자)를 꺼내 비웁니다. 디스크 목록을 가진 화면이 바뀐 파일만 다시 읽게 합니다(언리얼 에셋 레지스트리의 증분 갱신).
         * @return 개별 파일을 모두 알면 true 입니다. 폴더가 생기거나 사라지거나 이름이 바뀌었거나, 수가 `kMaxTrackedChangedPaths` 를 넘었거나, 경로를 리소스 id 로 못 바꾼 변경이 있으면
         *         false 이며 호출한 쪽이 전체를 다시 읽어야 합니다(@p outListPath 는 비어 있다). 어느 쪽이든 기억은 비워진다.
         * @details 파일 추가 · 삭제도 id 로 올라온다 — 그것이 색인의 구조를 바꾸는지는 받는 쪽이 판단한다(저장은 새 이름으로 오므로 이름만으로는 알 수 없다).
         */
        [[nodiscard]] bool takeChangedPaths( vector<string>& outListPath );
        /** @brief 바뀐 파일의 기억을 비웁니다(전체를 다시 읽기 시작할 때 — 그 읽기가 이전 변경을 모두 담는다). */
        void discardChangedPaths();

        /**
         * @brief 감시 울타리를 돌립니다 — 자동화 시나리오의 사본 폴더(`AutomationScratch::prepare`)를 새로 만들었으면, 감시 트리에 표지 파일을 쓰고 감시가 그 사건을 알릴 때까지 참을 돌려줍니다.
         * @details 에디터 밖의 쓰기는 OS 파일 감시가 알려야 변경 번호가 오른다. 알림 전에는 낡은 색인이 "맞다" 고 보인다(`ContentBrowserReferenceSearch::isCurrent`). 표지 사건이 왔으면 그 앞의 사본 쓰기도
         *          알려졌다(같은 감시 흐름의 순서). 시나리오 시작 차단 조건(`SW_AUTOMATION_START_BLOCKER`)이 매 프레임 부른다. 5 초 안에 안 오면 오류를 남기고 풀어 시나리오가 멈추지 않게 한다.
         * @return 아직 기다리는 중이면 true 입니다. 사본을 새로 만든 적이 없거나 감시가 꺼져 있으면 false 입니다.
         */
        [[nodiscard]] bool pollContentWatchFence();

        /**
         * @brief 바뀐 에셋 하나를 다시 읽습니다. 임포터(있으면), 엔진 캐시의 `reload`, 활성 씬의 `notifyAssetUsers` 순서로 처리합니다. 게임 스레드에서 틱 밖에서 호출합니다.
         * @return 처리했으면 `Reloaded`, 핫 리로드 대상이 아니거나 아무 캐시도 들지 않은 파일이면 `NotTracked`,
         *         경로표가 가리키는 캐시가 등록되지 않았으면(오류를 남겼다) `Failed` 입니다.
         */
        [[nodiscard]] static ReloadOutcome reloadChangedAsset( string_view relativePath );

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

        /**
         * @brief `Resource/` 아래의 어떤 변경이든 변경 번호를 올리고 바뀐 파일을 기억합니다(`getContentChangeSerial` · `takeChangedPaths`). 감시 대리자가 부르며, 테스트가 이벤트를 직접 넣는다.
         * @details 울타리 표지 파일(`pollContentWatchFence`)은 번호만 올리고 바뀐 파일로 세지 않는다.
         */
        void onResourceTreeChanged( const FileChangeEvent& changeEvent );

    private:
        /** @brief 바뀐 파일 하나를 `reloadChangedAsset` 으로 보냅니다. 수정 이벤트만 처리합니다. */
        void onResourceFileChanged( const FileChangeEvent& changeEvent );

        unique_ptr<FileWatchDispatcher> _pFileWatchDispatcher;
        FileWatchHandle                 _resourceWatchHandle;
        FileWatchHandle                 _contentWatchHandle; ///< 확장자와 무관한 감시다. 변경 번호와 바뀐 파일을 모은다
        uint64                          _contentChangeSerial;
        vector<string>                  _listChangedPath;          ///< 마지막 `takeChangedPaths` 뒤 바뀐 파일의 리소스 id(소문자, 중복 없음)
        bool                            _bChangedPathsUnknown;     ///< 개별 파일을 모르는 변경이 있었다(폴더 변경 · 한도 초과 · id 변환 실패)
        uint64                          _fencePrepareCount;        ///< 울타리를 마친(또는 포기한) 때의 `AutomationScratch::getPrepareCount`
        uint64                          _fenceArmedCount;          ///< 지금 세운 울타리가 본 `AutomationScratch::getPrepareCount`
        int64                           _fenceDeadlineNanoseconds; ///< 울타리를 포기하는 단조 시계 시각
        bool                            _bFenceWaiting;            ///< 표지 사건을 기다리는 중이다
        bool                            _bFenceSeen;               ///< 표지 파일의 사건이 왔다
    };
} // namespace sw::editor
