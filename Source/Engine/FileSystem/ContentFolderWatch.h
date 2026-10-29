/**
 * @file ContentFolderWatch.h
 * @brief 디스크의 콘텐츠 폴더가 바뀌면 알리는 확장점(`IContentFolderWatch`)입니다. 개발 도구가 호스트 범위에 건다 — 배포 빌드에는 없다(찾으면 nullptr).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"

namespace sw
{
    /**
     * @class IContentFolderWatch
     * @brief 기능 · 게임이 자기 데이터 폴더의 다시 읽기를 걸 때 찾는 감시 창구입니다(`game::findSubsystem<IContentFolderWatch>()`).
     * @details 감시 자체(OS 파일 감시 · 분배)는 개발 도구(`DevTools`)의 것이다 — 기능 쪽은 `#if` 없이 찾아보고, 없으면 감시하지 않는다(배포 · 팩 데이터).
     *          알림은 게임 스레드의 프레임 끝에 바뀐 감시마다 한 번이다.
     */
    class IContentFolderWatch
    {
    public:
        using ChangedDelegate = Delegate<void()>;

        /**
         * @brief 디스크 폴더 @p absoluteFolder 아래 확장자 @p listExtension 의 파일이 바뀌면 @p onChanged 를 부르게 합니다. 구독이 사라지면 멈춘다.
         * @return 감시를 시작하지 못하면(폴더 없음 · 이 플랫폼에 감시자 없음) 빈 구독입니다(이유는 로그).
         */
        [[nodiscard]] virtual Subscription watchDiskFolder( string_view absoluteFolder, const vector<string>& listExtension, const ChangedDelegate& onChanged ) = 0;

    protected:
        ~IContentFolderWatch() = default;
    };
} // namespace sw
