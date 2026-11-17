/**
 * @file DebrisBudget.h
 * @brief 한 씬의 떨어진 파편 바디 장부입니다. 씬 전체의 상한을 넘으면 씬 전체에서 (작은 것 먼저, 오래된 것 먼저) 고른 바디를 그 주인의 몫으로 넘깁니다.
 * @details 예전에는 상한 검사를 오브젝트마다 했다 — 먼저 틱한 오브젝트가 자기 파편을 지워 값을 치렀고, 더 오래된 다른 오브젝트의 파편은 남았다(감사 S-11).
 *          장부는 씬(`ScenePhysics`)이 하나 들고 프레임 끝 훅 앞에 한 번 고르며(`planRemovals`), 주인(파괴 컴포넌트)은 자기 몫만 꺼내 지운다(`takeRemovals`).
 *          언리얼 Chaos 의 씬 단위 잔해 정리(제거 타이머 · 최대 수)와 같은 자리다. 상한은 물리 설정의 `_maxDebrisBodyCount` 다. 게임 스레드에서만 만진다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 장부에 바디를 올리는 주인 하나의 번호입니다. `None` 은 아직 올린 적이 없다. */
    enum class DebrisOwnerID : uint32
    {
        None = 0,
    };

    /** @class DebrisBudget @brief 파일 머리말 참고. */
    class SW_API DebrisBudget
    {
    public:
        /** @brief 새 주인 번호를 냅니다. */
        [[nodiscard]] DebrisOwnerID addOwner();
        /** @brief 주인의 바디와 넘긴 몫을 모두 지웁니다(주인이 바디를 모두 놓을 때). */
        void removeOwner( DebrisOwnerID owner );
        /** @brief 주인 @p owner 의 그룹 @p groupID 바디를 올립니다. @p bLarge 면(충돌을 남기는 큰 덩어리) 작은 것을 모두 고른 뒤에 고른다. 올린 차례가 나이다. */
        void addBody( DebrisOwnerID owner, uint32 groupID, bool bLarge );
        /** @brief 바디를 내립니다(주인이 지웠다). 넘긴 몫에 있었으면 거기서도 빠진다. */
        void removeBody( DebrisOwnerID owner, uint32 groupID );
        /** @brief 올라 있는 바디 수입니다. */
        uint32 getBodyCount() const { return static_cast<uint32>( _listRecord.size() ); }
        /**
         * @brief 바디 수가 @p maxBodyCount 를 넘으면 넘친 만큼을 씬 전체에서 고릅니다 — 작은 것 먼저, 그다음 오래된 것 먼저. 이미 넘긴 몫은 지워질 것으로 셉니다.
         * @details 고른 바디는 주인이 `takeRemovals` 로 꺼내 지울 때까지 장부에 남는다(그동안 바디가 아직 있다).
         */
        void planRemovals( uint32 maxBodyCount );
        /** @brief 주인 @p owner 에게 넘긴 그룹 번호를 @p outListGroupID 에 더합니다. 주인은 그 바디를 지우고 `removeBody` 를 부른다 — 그때 장부에서 빠진다(부르지 않으면 다음에 또 넘긴다). */
        void takeRemovals( DebrisOwnerID owner, vector<uint32>& outListGroupID );

    private:
        struct Record
        {
            DebrisOwnerID _owner{ DebrisOwnerID::None };
            uint32        _groupID{ 0 };
            uint64        _age{ 0 };                 ///< 올린 차례(씬 전체에서 하나 — 주인이 달라도 견줄 수 있다)
            bool          _bLarge{ false };          ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            bool          _bRemovalPending{ false }; ///< 주인에게 넘겼다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };

    private:
        vector<Record> _listRecord;
        vector<uint32> _listOrder; ///< 고르기의 작업 목록(재사용)
        uint64         _nextAge{ 0 };
        uint32         _nextOwner{ 0 };
        uint32         _pendingRemovalCount{ 0 };
    };
} // namespace sw
