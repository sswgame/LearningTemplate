/**
 * @file TagIndex.h
 * @brief 태그 → 오브젝트 색인입니다. 태그 컴포넌트가 붙고 떨어지고 태그가 바뀔 때 맞추고, 질의는 씬을 훑지 않습니다.
 *
 * [왜 필요한가]
 * 기믹(포탑 · 능력 문 · 회로의 근접 노드)이 고정 스텝마다 "이 태그를 가진 것"을 찾느라 씬 전체를 돌았습니다. 유니티 DOTS 는 태그를 컴포넌트로 두어
 * 질의가 그 청크만 돌고, 언리얼 Mass 도 태그별 아키타입을 듭니다. 여기서는 태그 컴포넌트가 등록 · 해제 · 태그 쓰기마다 색인을 맞추고,
 * 질의는 요구 태그 중 가장 작은 칸 하나만 돕니다.
 *
 * [계층 태그]
 * `hasTag( tag, false )` 는 `Faction` 으로 `Faction.Player` 도 찾는다. 색인은 오브젝트의 태그마다 **자기와 모든 조상**(점 앞 접두어)의 id 칸에
 * 오브젝트를 넣어 같은 답을 낸다. id 는 `TagID::computeID` 하나로 구하므로 대소문자 무시도 같다.
 *
 * [세대 스냅샷]
 * 질의는 잠그지 않고 마지막에 낸 스냅샷(칸 표의 사본과 그 세대)을 읽는다. 쓰기는 잠금 아래 작업 표를 고치고 세대를 올리며, 세대가 지난 스냅샷을 본 첫 질의가
 * 잠금 아래 새 스냅샷을 내고 다음 질의들은 다시 잠그지 않는다(기믹이 고정 스텝 워커에서 함께 묻는다 — 예전에는 질의마다 공유 잠금을 잡았다). 옛 스냅샷은
 * 읽는 쪽이 아직 들고 있을 수 있어 바로 지우지 않고, 틱이 없는 프레임 자리(`Destroy` 단계)에서 `releaseRetiredSnapshots` 가 놓는다.
 * 언리얼 Mass 의 아키타입 버전 · 유니티 `EntityQuery` 의 순서 버전처럼 세대가 같으면 지난 답의 바탕을 그대로 쓴다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    class TagComponent;

    /**
     * @class TagIndex
     * @brief 씬 하나의 태그 → 오브젝트 색인입니다. `GameObjectManager` 가 소유하고 `TagComponent` 만 고칩니다.
     * @note 쓰기는 태그 컴포넌트의 등록(`onRegister`) · 해제(`onUnregister`) · 태그 쓰기(`addTag` · `removeTag` · `clearTags` · `onPropertyChanged` ·
     *       `onPostLoad`)뿐이다. 직렬화기가 PROPERTY 를 직접 쓰는 길도 뒤의 두 알림을 부르므로 색인이 어긋나지 않는다(`Component::notifyStateWritten`).
     */
    class SW_API TagIndex
    {
    public:
        /** @brief 빈 색인을 만듭니다. */
        TagIndex();
        ~TagIndex() = default;

        TagIndex( const TagIndex& )            = delete;
        TagIndex& operator=( const TagIndex& ) = delete;

        /**
         * @brief @p tagComponent 의 칸을 지금 태그 @p tags 에 맞춥니다. 처음이면 넣고, 태그가 비면 뺍니다. 칸이 그대로면 아무것도 하지 않습니다.
         * @param pOwner 칸에 넣을 소유 오브젝트(태그 컴포넌트의 소유자)
         */
        void update( const TagComponent& tagComponent, GameObject* pOwner, const TagContainer& tags );
        /** @brief @p tagComponent 의 칸을 모두 뺍니다. 없으면 할 일이 없습니다. */
        void remove( const TagComponent& tagComponent );

        /**
         * @brief @p required 의 태그를 모두 가진(`TagContainer::hasAllTags` — 하위 태그도 인정) 살아 있는 오브젝트를 @p outListObject 에 채웁니다(비우고 채운다).
         *        @p required 가 비면 비어 있습니다.
         * @details 요구 태그마다 칸을 찾아 가장 작은 칸 하나만 돈다. 요구 태그가 둘 이상이면 그 칸의 오브젝트마다 나머지를 컨테이너로 확인한다.
         *          삭제 대기 오브젝트는 건너뛴다(오브젝트 순회와 같은 답). 활성 여부는 부르는 쪽이 본다. **순서는 약속하지 않는다.**
         *          **콜백을 받지 않고 목록을 돌려주는 까닭**: 잠금을 쥔 채 부르는 쪽 코드를 돌리면, 그 코드가 태그를 바꿀 때 같은 스레드가 자기 공유 잠금을
         *          기다리며 멈춘다(잠금은 재진입하지 않는다). 목록을 복사하고 잠금을 놓은 뒤 돌게 하면 그 모양이 생길 수 없다. 버퍼는 부르는 쪽이 들고 재사용한다.
         */
        void findObjectsWithTags( const TagContainer& required, vector<GameObject*>& outListObject ) const;

        /** @brief 칸이 바뀔 때마다 오르는 세대입니다. 태그 필터 결과를 캐시하는 쪽(계층 창)이 마지막 값과 비교합니다. 읽기는 잠금이 없습니다. */
        uint64 getGeneration() const { return _generation.load( std::memory_order_acquire ); }

        /** @brief 지금 낸 것 말고 옛 스냅샷을 놓습니다. 질의가 도는 동안 부르지 않습니다(게임 스레드, 틱 밖 — 매니저의 `Destroy` 단계). */
        void releaseRetiredSnapshots();

    private:
        /** @brief 질의가 읽는 칸 표의 사본입니다. 내면 고치지 않습니다. */
        struct Snapshot
        {
            unordered_map<uint64, vector<GameObject*>> _mapKeyToObject{};
            uint64                                     _generation{ 0 };
        };

        /** @brief 지금 세대의 스냅샷입니다. 낡았으면 잠금 아래 새로 냅니다. */
        const Snapshot& acquireSnapshot() const;

        /** @brief 태그 컴포넌트 하나가 넣어 둔 칸입니다. 뺄 때 이 목록만 돕니다. */
        struct Record
        {
            GameObject*    _pOwner;
            vector<uint64> _listKey; ///< 정렬 · 중복 없음. 태그마다 자기와 조상의 id 다
        };

        /** @brief 잠금을 쥔 채 @p record 의 칸에서 소유 오브젝트를 뺍니다. */
        void removeFromBucketsUnlocked( const Record& record ) SW_REQUIRES_LOCK( _mutex );

    private:
        mutable shared_mutex                                             _mutex;
        unordered_map<uint64, vector<GameObject*>> _mapKeyToObject       SW_GUARDED_BY( _mutex ); ///< 태그 id(조상 포함) → 오브젝트. 빈 칸도 용량을 위해 남긴다
        unordered_map<const TagComponent*, Record> _mapComponentToRecord SW_GUARDED_BY( _mutex );
        vector<uint64> _listScratchKey                                   SW_GUARDED_BY( _mutex ); ///< `update` 가 새 칸을 모으는 버퍼
        mutable vector<unique_ptr<Snapshot>> _listSnapshot               SW_GUARDED_BY( _mutex ); ///< 낸 스냅샷 전부(끝이 지금 것). 옛 것은 `releaseRetiredSnapshots` 가 놓는다
        mutable atomic<const Snapshot*>                                  _pPublished;             ///< 질의가 읽는 스냅샷(늘 차 있다)
        atomic<uint64>                                                   _generation;
    };
} // namespace sw
