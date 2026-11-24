/**
 * @file SceneMoveDispatch.h
 * @brief 움직인 씬 컴포넌트를 구독자(공간 색인 · 물리의 바닥 묻기)마다 모아 주는 곳입니다 — 구독자는 프레임마다 모두 다시 묻지 않고 움직인 것만 봅니다.
 * @details 유니티 `TransformChangeDispatch`(관심 비트 · 바뀐 것만 꺼내기), 언리얼 컴포넌트 이동이 물리 · 겹침 구조에 더티를 올리는 것, Godot `NOTIFICATION_TRANSFORM_CHANGED` 의 자리다.
 *          - 관심은 트랜스폼 칸의 비트다(`SceneTransformPage::_arrMoveInterest`). 월드를 합성한 자리(`SceneTransformHierarchy::notifyWorldUpdated`)가 비트만 보고, 관심 있는 것만
 *            구독자 줄에 칸 번호를 세운다. 관심 없는 컴포넌트(메시 대부분)에는 비트 검사 하나뿐이고 간접 호출이 없다.
 *          - 줄은 스레드 스크래치마다 따로라 플러시 워커끼리 잠그지 않는다. 한 구독자의 줄에 한 컴포넌트는 꺼내기 전까지 한 번만 선다(`_arrMoveQueued`).
 *          - 줄은 칸 번호다. 구독을 끊거나 컴포넌트가 사라지면 칸의 비트가 내려가 꺼낼 때 건너뛴다 — 줄에서 찾아 빼지 않는다.
 *          구독자 수는 매니저마다 `kMaxConsumerCount` 까지다.
 */
#pragma once
#include "Core/Concurrency/SpinLock.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class GameObjectManager;
    class SceneComponent;

    /** @class SceneMoveDispatch @brief 매니저 하나의 움직임 줄입니다(`GameObjectManager` 가 소유). 구독은 `SceneMoveConsumer` 로만 합니다. 파일 머리말 참고. */
    class SW_API SceneMoveDispatch
    {
        friend class SceneMoveConsumer;

    public:
        /** @brief 매니저마다의 구독자 수 상한입니다(칸의 관심 비트 하나에 구독자 하나). */
        static constexpr uint32 kMaxConsumerCount = 8;

        SceneMoveDispatch();

        SceneMoveDispatch( const SceneMoveDispatch& )            = delete;
        SceneMoveDispatch& operator=( const SceneMoveDispatch& ) = delete;

        /**
         * @brief 월드를 막 합성한 @p component 를 관심 있는 구독자의 줄에 세웁니다 — `SceneTransformHierarchy::notifyWorldUpdated` 만 부른다(플러시 워커 여럿이 함께).
         * @param consumerMask 아직 서지 않은 구독자의 비트입니다.
         */
        static void queueMoved( SceneComponent& component, uint8 consumerMask );

    private:
        /** @brief 구독자 하나의 줄입니다 — 스레드 스크래치마다 칸 번호 목록 하나와, 스크래치 칸을 받지 못한 스레드가 잠그고 쓰는 목록 하나. */
        struct ConsumerQueue
        {
            vector<vector<uint32>> _listScratch{};
            vector<uint32>         _listShared{};   ///< `_sharedLock` 아래에서만
            bool                   _bUsed{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };

        uint32 addConsumer();
        void   removeConsumer( uint32 consumerIndex );
        void   push( uint32 slot, uint8 consumerMask );

    private:
        SpinLock      _sharedLock;
        ConsumerQueue _arrQueue[kMaxConsumerCount];
    };

    /**
     * @class SceneMoveConsumer
     * @brief 움직임 구독자 하나입니다 — 만들 때 매니저에 자리를 받고 사라질 때 돌려줍니다(매니저보다 먼저 사라져야 한다). 게임 스레드, 트랜스폼 플러시 밖에서만 부릅니다.
     */
    class SW_API SceneMoveConsumer
    {
    public:
        explicit SceneMoveConsumer( GameObjectManager& manager );
        ~SceneMoveConsumer();

        SceneMoveConsumer( const SceneMoveConsumer& )            = delete;
        SceneMoveConsumer& operator=( const SceneMoveConsumer& ) = delete;

        /** @brief @p component 의 움직임을 듣습니다. 막 들은 것은 움직인 것으로 친다(다음 `takeMoved` 에 든다). 이 매니저에 붙은 컴포넌트여야 합니다. */
        void track( const SceneComponent& component );
        /**
         * @brief 트랜스폼 칸 @p slot 의 @p pComponent 를 그만 듣습니다(줄에 선 것도 버린다).
         * @details @p pComponent 는 칸의 주인인지 견주기만 한다 — 소스가 듣던 쪽보다 먼저 사라졌어도(같은 오브젝트의 해체 차례) 부를 수 있다.
         */
        void untrack( uint32 slot, const SceneComponent* pComponent );
        /** @brief 트랜스폼 칸 @p slot 의 주인이 아직 @p pComponent 이면 true 입니다(@p pComponent 는 견주기만 한다). */
        static bool isAlive( uint32 slot, const SceneComponent* pComponent );
        /** @brief 지난번 뒤로 움직인 것을 @p outListMoved 에 꺼냅니다(비우고 채운다, 하나씩 한 번). 차례는 정해져 있지 않습니다. */
        void takeMoved( vector<SceneComponent*>& outListMoved );

    private:
        SceneMoveDispatch* _pDispatch;
        GameObjectManager* _pManager;
        uint32             _index;
    };
} // namespace sw
