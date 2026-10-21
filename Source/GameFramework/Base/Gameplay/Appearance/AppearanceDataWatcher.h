/**
 * @file AppearanceDataWatcher.h
 * @brief 외형 데이터 폴더(`<게임>/data/appearance/`)를 감시해, 파일을 고치면 실행 중인 `AppearanceDatabase` 를 다시 읽습니다(핫 리로드).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AppearanceDatabase;
    class IFileWatcher;
    class ItemCatalog;

    /**
     * @class AppearanceDataWatcher
     * @brief 게임 인스턴스가 하나 들고 `onUpdate` 에서 `update` 를 부릅니다. 바뀐 `.xml` 이 있으면 프레임에 한 번 `AppearanceDatabase::reloadFromFolder` 를 부릅니다.
     * @details 다시 읽은 데이터는 구독한 외형 컴포넌트가 다시 조립한다(`CharacterAppearanceComponent`). 검사를 통과하지 못한 다시 읽기는 오류로 알리고 지금 데이터를 둔다.
     *          팩 빌드처럼 폴더가 디스크에 없으면 감시할 것이 없다 — `initialize` 가 false 를 돌려주고 그 까닭을 정보로 남긴다(오류가 아니다).
     *          감시가 끊기면(`FileWatchPollResult::_bWatchLost`) 오류로 한 번 알리고 감시를 내린다.
     */
    class SW_GF_API AppearanceDataWatcher
    {
    public:
        AppearanceDataWatcher();
        ~AppearanceDataWatcher();

        AppearanceDataWatcher( const AppearanceDataWatcher& )            = delete;
        AppearanceDataWatcher& operator=( const AppearanceDataWatcher& ) = delete;

        /**
         * @brief 리소스 폴더 @p folder(로드에 쓴 것과 같은 리소스 경로)를 감시합니다. 두 포인터는 소유하지 않고, 이 객체보다 오래 살아야 합니다.
         * @return 감시를 시작했으면 true. 폴더가 디스크에 없거나(팩) 이 플랫폼에 감시자가 없으면 false 입니다.
         */
        [[nodiscard]] bool initialize( AppearanceDatabase& database, string_view folder, const ItemCatalog* pItemCatalog );
        void               shutdown();
        /** @brief 쌓인 변경을 꺼내 외형 데이터를 다시 읽습니다. 게임 스레드에서 부릅니다. */
        void update();
        bool isWatching() const { return _pWatcher != nullptr; }

    private:
        unique_ptr<IFileWatcher> _pWatcher;
        AppearanceDatabase*      _pDatabase;
        const ItemCatalog*       _pItemCatalog;
        string                   _folder; ///< 다시 읽을 리소스 경로
    };
} // namespace sw
