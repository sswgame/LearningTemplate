/**
 * @file SaveLock.h
 * @brief 세이브 막기 문입니다 — 씬 전환 · 연출 · 깨진 복원이 범위형 막기(`Subscription`)를 들고, 하나라도 들려 있으면 세이브 목적 캡처가 거절됩니다.
 * @details 막기를 거는 쪽이 구독을 들고 있는 동안만 막힌다. 구독이 사라지면(소멸 · `reset`) 풀리므로 "풀기를 잊은 막기" 가 형식으로 나올 수 없다 —
 *          예전 `AutosaveManager::setBlocked` · `clearBlocked` 짝은 프로덕션 호출처가 0 이었고, 짝을 맞추는 일은 부르는 쪽 기억에 맡겨져 있었다.
 *          문이 먼저 사라지면(게임 인스턴스가 내려가고 씬의 연출 컴포넌트가 남는 핫 리로드) `SubscriptionSource` 가 남은 구독을 무효화한다.
 *          언리얼 · 유니티에는 같은 자리의 엔진 기능이 없고 게임마다 "세이브 가능?" 깃발을 둔다 — 여기서는 깃발 대신 까닭 하나당 구독 하나다.
 *          스레드: 게임 스레드에서만 쓴다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class SaveLock
     * @brief 세이브 막기의 까닭 목록입니다. 게임 인스턴스가 하나 들고 게임 범위에 건다(`game::findSubsystem<SaveLock>()`).
     * @details 수동 세이브 · 간격 자동 저장 · 종료 저장이 모두 `GameInstance::captureSnapshot( SaveGame )` 을 지나므로 거기서 이 문을 본다.
     *          자동 저장 관리자는 막힌 동안 요청을 버리지 않고 기다렸다가 풀린 뒤 저장한다.
     */
    class SW_GF_API SaveLock final : public SubscriptionSource
    {
    public:
        SaveLock();
        ~SaveLock() override;

        SaveLock( const SaveLock& )            = delete;
        SaveLock& operator=( const SaveLock& ) = delete;
        SaveLock( SaveLock&& )                 = delete;
        SaveLock& operator=( SaveLock&& )      = delete;

        /** @brief 까닭 @p reason 으로 막기를 겁니다. 돌려준 구독을 든 동안 막히고, 구독이 사라지면 풀립니다. 같은 까닭을 여럿이 걸어도 각자 풉니다. */
        [[nodiscard]] Subscription block( const hashed_string& reason );
        /** @brief 막기가 하나라도 들려 있으면 true 입니다. */
        bool isBlocked() const { return _listBlock.empty() == false; }
        /** @brief 가장 먼저 건 막기의 까닭입니다(로그용). 막기가 없으면 nullptr 입니다. */
        const hashed_string* findBlockReason() const;

    private:
        void releaseSubscription( uint64 id ) override;

    private:
        /** @brief 들려 있는 막기 하나입니다. */
        struct Block
        {
            uint64        _id{ 0 };
            hashed_string _reason{};
        };

        vector<Block> _listBlock; ///< 건 순서대로
        uint64        _nextID;    ///< 다음 막기의 구독 id(0 은 쓰지 않는다)
    };
} // namespace sw
