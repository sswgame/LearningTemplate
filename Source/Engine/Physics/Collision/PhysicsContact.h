/**
 * @file PhysicsContact.h
 * @brief 접촉 · 트리거 이벤트(시작 · 유지 · 끝)와, 백엔드가 알리는 셰이프 쌍 단위 접촉을 바디 쌍 단위 이벤트로 바꾸는 추적기입니다.
 * @details 백엔드마다 알려 주는 것이 다릅니다 — Jolt 는 서브 셰이프 쌍마다 추가 · 유지 · 제거를 여러 잡 스레드에서, Box2D 는 셰이프 쌍마다 시작 · 끝만
 *          step 뒤에 줍니다. 추적기가 그 차이를 덮습니다: 바디 쌍마다 닿은 셰이프 쌍의 수를 세어 0 → 1 이면 시작, 1 → 0 이면 끝, 계속 닿아 있으면
 *          step 마다 유지를 냅니다(유니티 Enter · Stay · Exit). 한 step 안에 닿았다 떨어진 쌍은 시작과 끝을 둘 다 냅니다. 바디를 지우면
 *          그 바디의 쌍이 다음 이벤트 묶음에서 끝납니다(백엔드가 알리지 않아도).
 *
 *          이벤트 순서는 결정적입니다 — 바디 쌍(작은 핸들이 A) 순, 한 쌍 안에서는 시작 · 유지 · 끝 순입니다. 해시 표를 도는 순서에 기대지 않습니다.
 *          추적기는 잠그지 않습니다. 여러 스레드에서 오는 백엔드 알림은 백엔드가 모아 두었다가 step 뒤에 한 스레드에서 넣습니다.
 */
#pragma once
#include "Core/Common/HashUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"

#include "Engine/Physics/PhysicsDesc.h"
#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    /**
     * @brief 접촉 · 트리거 이벤트 하나입니다. A 의 핸들이 B 보다 작습니다. 법선은 A 에서 B 를 향합니다.
     * @details `_impulse` 는 접촉 충격량의 크기(뉴턴초)입니다 — 시작은 부딪힌 충격(다가오던 상대 속도 × 유효 질량 × (1 + 반발), Jolt
     *          `EstimateCollisionResponse` · Box2D 부딪힘 이벤트), 유지는 그 스텝의 접촉 충격량(가만히 쉬는 상자는 무게 × 스텝), 끝은 0 입니다.
     *          트리거가 낀 쌍은 0 입니다. 파괴 · 피해 · 소리 세기가 이 값을 씁니다.
     */
    template <typename TDimension>
    struct PhysicsContactEvent
    {
        using Vector = typename TDimension::Vector;

        PhysicsBodyHandle   _bodyA{};
        PhysicsBodyHandle   _bodyB{};
        uint64              _userDataA{ 0 };
        uint64              _userDataB{ 0 };
        Vector              _point{};         ///< 대표 접촉점(월드). 끝 이벤트는 마지막으로 안 점
        Vector              _normal{};        ///< A 에서 B 쪽 법선(월드)
        float32             _impulse{ 0.0f }; ///< 접촉 충격량 크기(뉴턴초)
        PhysicsContactPhase _phase{ PhysicsContactPhase::Begin };
        uint8               _bTriggerA{ SW_FALSE }; ///< A 가 트리거인지
        uint8               _bTriggerB{ SW_FALSE }; ///< B 가 트리거인지

        /** @brief 어느 쪽이든 트리거면 true 입니다 — 막는 접촉이 아니라 감지입니다. */
        bool involvesTrigger() const { return _bTriggerA == SW_TRUE || _bTriggerB == SW_TRUE; }
    };

    using PhysicsContactEvent3D = PhysicsContactEvent<PhysicsDimension3D>;
    using PhysicsContactEvent2D = PhysicsContactEvent<PhysicsDimension2D>;
} // namespace sw

namespace sw
{
    /** @brief 추적기가 바디 하나에 대해 기억하는 것입니다(바디가 사라진 뒤에도 끝 이벤트를 낼 수 있게). */
    struct PhysicsContactBody
    {
        PhysicsBodyHandle _body{};
        uint64            _userData{ 0 };
        bool              _bTrigger{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 셰이프 쌍 단위 알림을 바디 쌍 단위 시작 · 유지 · 끝으로 바꿉니다. 파일 머리말 참고. */
    template <typename TDimension>
    class PhysicsContactTracker
    {
    public:
        using Vector = typename TDimension::Vector;
        using Event  = PhysicsContactEvent<TDimension>;

        PhysicsContactTracker()
            : _mapPair{}
        {
        }

        /** @brief 셰이프 쌍 하나가 닿기 시작했습니다. @p normal 은 @p bodyA 에서 @p bodyB 쪽입니다. */
        void beginSubContact( const PhysicsContactBody& bodyA, const PhysicsContactBody& bodyB, const Vector& point, const Vector& normal, float32 impulse )
        {
            const bool bSwap        = bodyB._body < bodyA._body;
            PairState& state        = _mapPair[makeKey( bodyA._body, bodyB._body )];
            state._first            = bSwap ? bodyB : bodyA;
            state._second           = bSwap ? bodyA : bodyB;
            state._point            = point;
            state._normal           = bSwap ? -normal : normal;
            state._impulse          = state._impulse > impulse ? state._impulse : impulse;
            state._bTouchedThisStep = true;
            ++state._subContactCount;
        }

        /** @brief 계속 닿아 있는 셰이프 쌍의 이번 step 값입니다. 모르는 쌍이면 무시합니다. */
        void persistSubContact( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB, const Vector& point, const Vector& normal, float32 impulse )
        {
            typename PairMap::iterator iter = _mapPair.find( makeKey( bodyA, bodyB ) );
            if ( iter == _mapPair.end() )
                return;
            PairState& state = iter->second;
            const bool bSwap = bodyB < bodyA;
            state._point     = point;
            state._normal    = bSwap ? -normal : normal;
            state._impulse   = state._impulse > impulse ? state._impulse : impulse;
        }

        /** @brief 셰이프 쌍 하나가 떨어졌습니다. 모르는 쌍(이미 끝낸 쌍)이면 무시합니다. */
        void endSubContact( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB )
        {
            typename PairMap::iterator iter = _mapPair.find( makeKey( bodyA, bodyB ) );
            if ( iter == _mapPair.end() || iter->second._subContactCount <= 0 )
                return;
            --iter->second._subContactCount;
        }

        /** @brief 바디들이 사라졌습니다. 그 바디가 낀 쌍은 다음 `finishStep` 에서 끝납니다. 한 번 훑습니다(대량 파괴에 바디마다 훑지 않는다). */
        void removeBodies( span<const PhysicsBodyHandle> listBody )
        {
            if ( listBody.empty() || _mapPair.empty() )
                return;
            unordered_set<uint64> uniqueRemoved;
            uniqueRemoved.reserve( listBody.size() );
            for ( const PhysicsBodyHandle& body : listBody )
            {
                uniqueRemoved.insert( body.packed() );
            }
            for ( auto& [key, state] : _mapPair )
            {
                const bool bFirstGone  = uniqueRemoved.find( state._first._body.packed() ) != uniqueRemoved.end();
                const bool bSecondGone = uniqueRemoved.find( state._second._body.packed() ) != uniqueRemoved.end();
                if ( bFirstGone || bSecondGone )
                {
                    state._subContactCount = 0;
                    state._bBodyRemoved    = true;
                }
            }
        }

        /** @brief 이번 step 의 이벤트를 @p outListEvent 끝에 붙입니다. 끝난 쌍은 잊습니다. */
        void finishStep( vector<Event>& outListEvent )
        {
            const size_t firstNew = outListEvent.size();
            for ( typename PairMap::iterator iter = _mapPair.begin(); iter != _mapPair.end(); )
            {
                PairState& state   = iter->second;
                const bool bActive = state._subContactCount > 0 && state._bBodyRemoved == false;
                if ( state._bWasActive && bActive )
                {
                    outListEvent.push_back( makeEvent( state, PhysicsContactPhase::Stay ) );
                }
                else if ( state._bWasActive == false && bActive )
                {
                    outListEvent.push_back( makeEvent( state, PhysicsContactPhase::Begin ) );
                }
                else if ( state._bWasActive && bActive == false )
                {
                    outListEvent.push_back( makeEvent( state, PhysicsContactPhase::End ) );
                }
                else if ( state._bTouchedThisStep )
                {
                    // 한 step 안에 닿았다 떨어졌다 — 시작과 끝을 둘 다 낸다(빠른 공이 트리거를 스치고 지나간 경우).
                    outListEvent.push_back( makeEvent( state, PhysicsContactPhase::Begin ) );
                    outListEvent.push_back( makeEvent( state, PhysicsContactPhase::End ) );
                }
                if ( bActive == false )
                {
                    iter = _mapPair.erase( iter );
                    continue;
                }
                state._bWasActive       = true;
                state._bTouchedThisStep = false;
                state._impulse          = 0.0f;
                ++iter;
            }
            std::sort( outListEvent.begin() + static_cast<std::ptrdiff_t>( firstNew ), outListEvent.end(), &isEarlierEvent );
        }

        /** @brief 모든 쌍을 잊습니다(끝 이벤트 없이). */
        void clear() { _mapPair.clear(); }
        /** @brief 지금 닿아 있다고 아는 바디 쌍의 수입니다. */
        uint32 getActivePairCount() const { return static_cast<uint32>( _mapPair.size() ); }

    private:
        struct PairKey
        {
            uint64 _first{ 0 };
            uint64 _second{ 0 };

            bool operator==( const PairKey& other ) const noexcept { return _first == other._first && _second == other._second; }
        };

        struct PairKeyHash
        {
            size_t operator()( const PairKey& key ) const noexcept
            {
                size_t hash = std::hash<uint64>{}( key._first );
                hash        = HashUtil::combine( hash, std::hash<uint64>{}( key._second ) );
                return hash;
            }
        };

        struct PairState
        {
            PhysicsContactBody _first{};
            PhysicsContactBody _second{};
            Vector             _point{};
            Vector             _normal{};
            float32            _impulse{ 0.0f };
            int32              _subContactCount{ 0 };
            bool               _bWasActive{ false };       ///< 지난 step 끝에 닿아 있었는지
            bool               _bTouchedThisStep{ false }; ///< 이번 step 에 새로 닿은 셰이프 쌍이 있었는지
            bool               _bBodyRemoved{ false };
        };

        using PairMap = unordered_map<PairKey, PairState, PairKeyHash>;

        static PairKey makeKey( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB )
        {
            const bool bSwap = bodyB < bodyA;
            return PairKey{ bSwap ? bodyB.packed() : bodyA.packed(), bSwap ? bodyA.packed() : bodyB.packed() };
        }

        static Event makeEvent( const PairState& state, PhysicsContactPhase phase )
        {
            Event event;
            event._bodyA      = state._first._body;
            event._bodyB      = state._second._body;
            event._userDataA  = state._first._userData;
            event._userDataB  = state._second._userData;
            event._point      = state._point;
            event._normal     = state._normal;
            event._phase      = phase;
            event._bTriggerA  = state._first._bTrigger ? SW_TRUE : SW_FALSE;
            event._bTriggerB  = state._second._bTrigger ? SW_TRUE : SW_FALSE;
            const bool bSolid = state._first._bTrigger == false && state._second._bTrigger == false;
            event._impulse    = ( bSolid && phase != PhysicsContactPhase::End ) ? state._impulse : 0.0f;
            return event;
        }

        static bool isEarlierEvent( const Event& lhs, const Event& rhs )
        {
            if ( lhs._bodyA != rhs._bodyA )
                return lhs._bodyA < rhs._bodyA;
            if ( lhs._bodyB != rhs._bodyB )
                return lhs._bodyB < rhs._bodyB;
            return static_cast<uint8>( lhs._phase ) < static_cast<uint8>( rhs._phase );
        }

        PairMap _mapPair;
    };
} // namespace sw
