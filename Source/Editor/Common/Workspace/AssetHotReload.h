/**
 * @file AssetHotReload.h
 * @brief 실행 중 `Resource/` 변경을 감지해 엔진 캐시를 다시 읽게 합니다 — **에디터 전용 개발 기능**.
 *
 * @details 예전에는 `ResourceManager` 가 이 감시를 직접 붙였다. 그래서 에디터가 없는 배포본도
 *          감시 스레드를 띄우고 이벤트를 큐에 쌓았는데, 정작 그 이벤트로 할 일(에셋을 고쳐서
 *          바로 보는 것)은 에디터에만 있다. 소유를 에디터로 내리면 배포본에는 감시가 아예 없고,
 *          "무엇을 언제 다시 읽을지" 는 개발 도구 쪽 결정으로 남는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/Workspace/ReloadFileManager.h"

namespace sw::editor
{
    /**
     * @class AssetHotReload
     * @brief `Resource/` 하위 변경을 폴링해 확장자별 리로드 처리기로 보냅니다.
     */
    class AssetHotReload
    {
    public:
        /** @brief 감시를 시작하지 않은 채 만듭니다. */
        AssetHotReload();
        /** @brief 감시를 정리합니다. */
        ~AssetHotReload();

        AssetHotReload( const AssetHotReload& )            = delete;
        AssetHotReload& operator=( const AssetHotReload& ) = delete;

        /**
         * @brief 파일 감시를 시작하고 `Resource/` 워치를 겁니다.
         * @return 감시자를 세웠으면 true. 리소스 루트가 없으면(팩만 있는 실행) false.
         */
        bool initialize();

        /** @brief 워치를 해제하고 감시 스레드를 멈춥니다. */
        void shutdown();

        /** @brief 에디터 프레임마다 불러 변경 이벤트를 꺼냅니다. */
        void update();

    private:
        /** @brief 바뀐 파일 하나를 확장자에 맞는 처리기로 보냅니다. */
        void onResourceFileChanged( const FileChangeEvent& changeEvent );

        unique_ptr<ReloadFileManager> _pReloadFileManager;
        FileWatchHandle               _resourceWatchHandle;
    };
} // namespace sw::editor
