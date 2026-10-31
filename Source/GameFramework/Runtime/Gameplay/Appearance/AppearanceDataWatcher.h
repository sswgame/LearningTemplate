/**
 * @file AppearanceDataWatcher.h
 * @brief 외형 데이터 폴더(`<게임>/data/appearance/`)를 감시해, 파일을 고치면 실행 중인 `AppearanceDatabase` 를 다시 읽습니다(핫 리로드 — 개발 빌드만).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AppearanceDatabase;
    class ItemCatalog;

    /**
     * @class AppearanceDataWatcher
     * @brief 게임 인스턴스가 하나 들고 `onUpdate` 에서 `update` 를 부릅니다. 바뀐 `.xml` 이 있으면 프레임에 한 번 `AppearanceDatabase::reloadFromFolder` 를 부릅니다.
     * @details 다시 읽은 데이터는 구독한 외형 컴포넌트가 다시 조립한다(`CharacterAppearanceComponent`). 검사를 통과하지 못한 다시 읽기는 오류로 알리고 지금 데이터를 둔다.
     *          팩 빌드처럼 폴더가 디스크에 없으면 감시할 것이 없다 — `initialize` 가 false 를 돌려주고 그 까닭을 정보로 남긴다(오류가 아니다).
     *          감시는 범위의 콘텐츠 폴더 감시 창구(`IContentFolderWatch` — 개발 도구가 건다)로 한다. 창구가 없으면(배포) 감시하지 않는다(`initialize` 가 false).
     */
    class SW_GF_API AppearanceDataWatcher
    {
    public:
        AppearanceDataWatcher();
        ~AppearanceDataWatcher();

        AppearanceDataWatcher( const AppearanceDataWatcher& )            = delete;
        AppearanceDataWatcher& operator=( const AppearanceDataWatcher& ) = delete;

        /**
         * @brief 리소스 폴더 @p folder(로드에 쓴 것과 같은 리소스 경로)를 감시합니다. 데이터베이스와 아이템 카탈로그는 **기억하지 않는다** — 다시 읽을 때마다 `update` 가 부르는 쪽이 넘긴다(소유자보다 오래 사는 포인터가 남지 않는다).
         * @return 감시를 시작했으면 true. 폴더가 디스크에 없거나(팩) 이 플랫폼에 감시자가 없으면 false 입니다.
         */
        [[nodiscard]] bool initialize( string_view folder );
        void               shutdown();
        /** @brief 쌓인 변경을 꺼내 @p database 를 다시 읽습니다(@p pItemCatalog 는 아이템 외형 확인용). 게임 스레드에서 부릅니다. */
        void update( AppearanceDatabase& database, const ItemCatalog* pItemCatalog );
        bool isWatching() const { return _watch.isActive(); }

    private:
        void onFolderChanged();

        Subscription _watch;             ///< 콘텐츠 폴더 감시 구독(창구가 없으면 비었다)
        string       _folder;            ///< 다시 읽을 리소스 경로
        bool         _bChanged{ false }; ///< 지난 update 뒤로 .xml 이 바뀌었다
    };
} // namespace sw
