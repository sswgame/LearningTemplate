/**
 * @file ContentFolderWatchService.h
 * @brief 콘텐츠 폴더 감시 확장점(`IContentFolderWatch`)의 개발 도구 구현입니다 — 감시마다 파일 감시 분배기 하나를 든다.
 */
#pragma once
#include "Core/Container/IDAllocator.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/SubscriptionList.h"
#include "Core/Memory/Memory.h"

#include "DevTools/DevToolsAPI.h"

#include "Engine/FileSystem/ContentFolderWatch.h"

namespace sw
{
    class FileWatchDispatcher;
    struct FileChangeEvent;

    /**
     * @class ContentFolderWatchService
     * @brief 개발 도구 진입점(`DevToolsHost`)이 값으로 들고 호스트 범위에 `IContentFolderWatch` 로 건다. 프레임 끝에 `update` 가 쌓인 변경을 알린다.
     */
    class SW_DEVTOOLS_API ContentFolderWatchService final : public IContentFolderWatch
    {
    public:
        ContentFolderWatchService();
        ~ContentFolderWatchService();
        ContentFolderWatchService( const ContentFolderWatchService& )            = delete;
        ContentFolderWatchService& operator=( const ContentFolderWatchService& ) = delete;

        [[nodiscard]] Subscription watchDiskFolder( string_view absoluteFolder, const vector<string>& listExtension, const ChangedDelegate& onChanged ) override;
        /** @brief 감시마다 OS 변경을 꺼내고, 바뀐 감시의 알림을 부릅니다(게임 스레드, 프레임 끝). */
        void update();

    private:
        struct Watch;
        void releaseWatch( uint64 id );

        vector<unique_ptr<Watch>> _listWatch;
        IDAllocator<uint64>       _watchIDAllocator;
        SubscriptionList          _subscriptions{ SubscriptionList::ReleaseFunction::create<&ContentFolderWatchService::releaseWatch>( this ) }; ///< 맨 끝 — 먼저 사라져 남은 구독을 끊는다
    };
} // namespace sw
