/**
 * @file PhysicsContact.h
 * @brief 접촉과 트리거 이벤트(시작, 유지, 끝), 그리고 백엔드가 알리는 셰이프 쌍 단위 접촉을 바디 쌍 단위 이벤트로 바꾸는 추적기입니다.
 * @details 백엔드마다 알려 주는 것이 다르다. Jolt 는 서브 셰이프 쌍마다 추가, 유지, 제거를 여러 잡 스레드에서 알리고, Box2D 는 셰이프 쌍마다 시작과 끝만
 *          step 뒤에 준다. 추적기가 그 차이를 덮는다. 바디 쌍마다 닿은 셰이프 쌍의 수를 세어 0 에서 1 이 되면 시작, 1 에서 0 이 되면 끝, 계속 닿아 있으면
 *          step 마다 유지를 낸다(유니티의 Enter, Stay, Exit). 한 step 안에 닿았다 떨어진 쌍은 시작과 끝을 둘 다 낸다. 바디를 지우면
 *          그 바디의 쌍이 다음 이벤트 배치에서 끝난다(백엔드가 알리지 않아도 그렇다).
 *
 *          유지는 opt-in 이다(감사 S-1). 사용자 값 하나가 유지를 듣겠다고 올려야(`setStayListener`) 그 사용자 값이 낀 쌍만 유지를 낸다 — 언리얼
 *          `bNotifyRigidBodyCollision` · Godot `contact_monitor` 처럼 켠 바디만 알리고, 유니티처럼 듣는 쪽이 없으면 만들지 않는다. 바디가 아니라
 *          사용자 값(엔진은 오브젝트 id)에 거는 까닭은 바디를 다시 지어도(배율 · 타일 청크) 듣기가 따라가게 하려는 것이다. 백엔드는 유지 쌍의 값
 *          (Jolt 유지 충격량 어림 · Box2D 쌍 충격량)을 `isStayReported` 가 참인 쌍만 구한다 — 그래서 듣지 않는 쌍의 끝 점은 시작 때 점이다.
 *
 *          이벤트 순서는 결정적이다. 바디 쌍(작은 핸들이 A) 순이고, 한 쌍 안에서는 시작, 유지, 끝 순이다. 해시 테이블을 도는 순서에 의존하지 않는다.
 *          추적기는 잠그지 않는다. 여러 스레드에서 오는 백엔드 알림은 백엔드가 모아 두었다가 step 뒤에 한 스레드에서 넣는다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"

#include "Engine/Physics/PhysicsDesc.h"
#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    /**
     * @brief 접촉과 트리거 이벤트 하나입니다. A 의 핸들이 B 보다 작습니다. 법선은 A 에서 B 를 향합니다.
     * @details `_impulse` 는 접촉 충격량의 크기(뉴턴초)다. 시작은 부딪힌 충격(다가오던 상대 속도 × 유효 질량 × (1 + 반발). Jolt 는
     *          `EstimateCollisionResponse`, Box2D 는 부딪힘 이벤트)이고, 유지는 그 스텝의 접촉 충격량(가만히 쉬는 상자는 무게 × 스텝)이며, 끝은 0 이다.
     *          트리거가 낀 쌍은 0 이다. 파괴, 피해, 소리 세기가 이 값을 쓴다.
     */
    template <typename TDimension>
    struct PhysicsContactEvent
    {
        using Vector = typename TDimension::Vector;

        PhysicsBodyHandle   _bodyA{};
        PhysicsBodyHandle   _bodyB{};
        uint64              _userDataA{ 0 };
        uint64              _userDataB{ 0 };
        Vector              _point{};         ///< 대표 접촉점이다(월드). 끝 이벤트는 마지막으로 알려진 점이다.
        Vector              _normal{};        ///< A 에서 B 쪽 법선(월드)
        float32             _impulse{ 0.0f }; ///< 접촉 충격량 크기(뉴턴초)
        PhysicsContactPhase _phase{ PhysicsContactPhase::Begin };
        uint8               _bTriggerA{ SW_FALSE }; ///< A 가 트리거인지 나타낸다.
        uint8               _bTriggerB{ SW_FALSE }; ///< B 가 트리거인지 나타낸다.
        uint8               _bOverlap{ SW_FALSE };  ///< 막지 않는 겹침인지 나타낸다(어느 쪽이 트리거이거나 겹침 전용 바디 — `PhysicsBodyDesc::_bOverlapOnly`).

        /** @brief 어느 쪽이든 트리거면 true 를 반환합니다. */
        bool usesTrigger() const { return _bTriggerA == SW_TRUE || _bTriggerB == SW_TRUE; }
        /** @brief 막는 접촉이 아니라 감지(겹침)면 true 를 반환합니다. 받는 쪽은 `onOverlap*` 을 받습니다. */
        bool isOverlap() const { return _bOverlap == SW_TRUE; }
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
        bool              _bTrigger{ false };     ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool              _bOverlapOnly{ false }; ///< 막지 않는 겹침 전용 바디다(트리거가 아닌 센서, `PhysicsBodyDesc::_bOverlapOnly`). NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };
} // namespace sw

namespace sw
{
    /** @brief 셰이프 쌍 단위 알림을 바디 쌍 단위의 시작, 유지, 끝으로 바꿉니다. 파일 머리말을 참고하세요. */
    template <typename TDimension>
    class PhysicsContactTracker
    {
    public:
        using Vector = typename TDimension::Vector;
        using Event  = PhysicsContactEvent<TDimension>;

        PhysicsContactTracker()
            : _mapPair{}
            , _listRestartEnd{}
            , _setStayUserData{}
            , _bStayDirty{ SW_FALSE }
        {
        }

        /**
         * @brief 사용자 값 @p userData 가 낀 쌍의 유지를 낼지 정합니다(파일 머리말 — 유지는 opt-in). 이미 닿아 있는 쌍에도 다음 `refreshStayPairs` 부터 적용됩니다.
         * @note step 사이에만 부릅니다. 백엔드 잡 스레드가 step 동안 `isStayReported` 를 읽는다.
         */
        void setStayListener( uint64 userData, bool bListen )
        {
            const bool bChanged = bListen ? _setStayUserData.insert( userData ).second : _setStayUserData.erase( userData ) > 0;
            if ( bChanged )
                _bStayDirty = SW_TRUE;
        }

        /** @brief `setStayListener` 가 바꾼 듣기를 닿아 있는 쌍에 다시 적용합니다. 바뀐 것이 없으면 아무것도 하지 않습니다. step 앞(백엔드)과 `finishStep` 이 부릅니다. */
        void refreshStayPairs()
        {
            if ( _bStayDirty == SW_FALSE )
                return;
            _bStayDirty = SW_FALSE;
            for ( auto& [key, state] : _mapPair )
            {
                state._bReportStay = isStayListener( state._first ) || isStayListener( state._second );
            }
        }

        /**
         * @brief 바디 쌍이 유지를 내는지 반환합니다. 모르는 쌍이면 false 입니다.
         * @note 백엔드 잡 스레드가 step 동안 함께 읽는다(Jolt `OnContactPersisted`) — 쓰기는 step 사이(`flush` · `finishStep` · `refreshStayPairs`)에만 난다.
         */
        bool isStayReported( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB ) const
        {
            typename PairMap::const_iterator iter = _mapPair.find( makeKey( bodyA, bodyB ) );
            return iter != _mapPair.end() && iter->second._bReportStay;
        }

        /** @brief 셰이프 쌍 하나가 닿기 시작했습니다. @p normal 은 @p bodyA 에서 @p bodyB 쪽입니다. */
        void beginSubContact( const PhysicsContactBody& bodyA, const PhysicsContactBody& bodyB, const Vector& point, const Vector& normal, float32 impulse )
        {
            const bool bSwap = bodyB._body < bodyA._body;
            PairState& state = _mapPair[makeKey( bodyA._body, bodyB._body )];
            if ( state._bBodyRemoved )
            {
                // 지운 바디가 같은 step 간격 안에서 다시 켜져 닿았다 — 옛 접촉은 끝내고 새로 시작한다. 지운 표시가 남으면 이 쌍을 끝내고 잊어
                // 백엔드가 다시 알리지 않는 접촉을 영영 잃는다(감사 S-13).
                state._bBodyRemoved    = false;
                state._bRestarted      = state._bWasActive;
                state._subContactCount = 0;
                state._suspendedCount  = 0;
            }
            state._first            = bSwap ? bodyB : bodyA;
            state._second           = bSwap ? bodyA : bodyB;
            state._point            = point;
            state._normal           = bSwap ? -normal : normal;
            state._impulse          = state._impulse > impulse ? state._impulse : impulse;
            state._bTouchedThisStep = true;
            state._bReportStay      = isStayListener( state._first ) || isStayListener( state._second );
            if ( state._suspendedCount > 0 )
                --state._suspendedCount; // 잠들며 쉬던 셰이프 쌍이 깨어 다시 닿았다 — 같은 접촉이다
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

        /**
         * @brief 셰이프 쌍 하나가 잠들어 백엔드가 알림을 멈췄습니다(Jolt 는 바디가 잠들면 그 접촉마다 제거를 알린다). 쌍은 끝나지 않고 쉽니다.
         * @details 쉬는 셰이프 쌍은 닿아 있는 것으로 센다 — 끝 · 시작을 내지 않고, 셰이프 쌍이 모두 쉬면 유지도 내지 않는다(유니티 · 언리얼도 잠든 바디의 접촉을
         *          끝내지 않는다). 깨어 다시 닿으면 `beginSubContact` 가 쉼을 접촉으로 돌리고, 깨었는데 닿지 않은 것은 `resumeSubContacts` 가 지운다.
         */
        void suspendSubContact( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB )
        {
            typename PairMap::iterator iter = _mapPair.find( makeKey( bodyA, bodyB ) );
            if ( iter == _mapPair.end() || iter->second._subContactCount <= 0 )
                return;
            --iter->second._subContactCount;
            ++iter->second._suspendedCount;
        }

        /**
         * @brief 쉬는 셰이프 쌍이 있는 바디 쌍 가운데 @p isAsleep( first, second ) 가 거짓인(깨어난) 쌍의 쉼을 지웁니다 — 깨어 다시 닿은 것은 이번 step 의
         *        알림이 이미 접촉으로 돌렸으니, 남은 쉼은 깨어 보니 떨어진 것이다. 백엔드가 이번 step 의 알림을 넣은 뒤 `finishStep` 앞에서 부릅니다.
         */
        template <typename TPredicate>
        void resumeSubContacts( const TPredicate& isAsleep )
        {
            for ( auto& [key, state] : _mapPair )
            {
                if ( state._suspendedCount > 0 && isAsleep( state._first._body, state._second._body ) == false )
                    state._suspendedCount = 0;
            }
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
                    state._suspendedCount  = 0;
                    state._bBodyRemoved    = true;
                }
            }
        }

        /**
         * @brief 바디 @p body 의 셰이프를 갈았습니다(`setBodyShape`) — 그 바디가 낀 쌍의 셰이프 쌍 수를 0 으로 돌리고 쌍은 남깁니다.
         * @details 새 셰이프가 다음 step 에 다시 닿으면 쌍은 이어지고(끝 · 시작 없음), 닿지 않으면 그 `finishStep` 에서 끝난다. 백엔드는 옛 셰이프의 끝 알림을
         *          버려야 한다(Box2D 는 셰이프 → 바디 표에서 먼저 뺀다). 셰이프 쌍 수를 백엔드가 스스로 맞추는 Jolt(바디 id 가 그대로라 옛 쌍의 제거와 새 쌍의
         *          추가가 짝을 이룬다)는 부르지 않는다.
         */
        void resetSubContacts( PhysicsBodyHandle body )
        {
            for ( auto& [key, state] : _mapPair )
            {
                if ( state._first._body == body || state._second._body == body )
                {
                    state._subContactCount = 0;
                    state._suspendedCount  = 0;
                }
            }
        }

        /** @brief 이번 step 의 이벤트를 @p outListEvent 끝에 붙입니다. 끝난 쌍은 잊습니다. */
        void finishStep( vector<Event>& outListEvent )
        {
            const size_t firstNew = outListEvent.size();
            _listRestartEnd.clear();
            refreshStayPairs();
            for ( typename PairMap::iterator iter = _mapPair.begin(); iter != _mapPair.end(); )
            {
                PairState& state   = iter->second;
                const bool bActive = ( state._subContactCount > 0 || state._suspendedCount > 0 ) && state._bBodyRemoved == false;
                if ( state._bWasActive && bActive && state._bRestarted )
                {
                    _listRestartEnd.push_back( makeEvent( state, PhysicsContactPhase::End ) ); // 옛 생애의 끝 — 아래 정렬 묶음 앞에 둔다
                    outListEvent.push_back( makeEvent( state, PhysicsContactPhase::Begin ) );
                }
                else if ( state._bWasActive && bActive )
                {
                    if ( state._bReportStay && state._subContactCount > 0 ) // 모두 쉬는(잠든) 쌍은 유지를 내지 않는다
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
                    // 한 step 안에 닿았다 떨어졌다. 시작과 끝을 둘 다 낸다(빠른 공이 트리거를 스치고 지나간 경우).
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
                state._bRestarted       = false;
                state._impulse          = 0.0f;
                ++iter;
            }
            std::sort( outListEvent.begin() + static_cast<std::ptrdiff_t>( firstNew ), outListEvent.end(), &isEarlierEvent );
            // 재시작한 쌍의 끝은 이번 step 의 어떤 이벤트보다 앞선다 — 정렬(쌍 · 단계 순)에 넣으면 같은 쌍의 새 시작 뒤로 간다.
            std::sort( _listRestartEnd.begin(), _listRestartEnd.end(), &isEarlierEvent );
            outListEvent.insert( outListEvent.begin() + static_cast<std::ptrdiff_t>( firstNew ), _listRestartEnd.begin(), _listRestartEnd.end() );
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
            int32              _suspendedCount{ 0 };       ///< 잠들어 백엔드가 알림을 멈춘 셰이프 쌍 수(`suspendSubContact`) — 닿아 있는 것으로 센다
            bool               _bWasActive{ false };       ///< 지난 step 끝에 닿아 있었는지 나타낸다. NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            bool               _bTouchedThisStep{ false }; ///< 이번 step 에 새로 닿은 셰이프 쌍이 있었는지 나타낸다. NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            bool               _bRestarted{ false };       ///< 지운 바디가 같은 간격 안에서 다시 닿았다 — 끝과 시작을 함께 낸다. NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            bool               _bReportStay{ false };      ///< 어느 쪽 사용자 값이 유지를 듣는다(`setStayListener`). NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            bool               _bBodyRemoved{ false };     ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };

        using PairMap = unordered_map<PairKey, PairState, PairKeyHash>;

        static PairKey makeKey( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB )
        {
            const bool bSwap = bodyB < bodyA;
            return PairKey{ bSwap ? bodyB.packed() : bodyA.packed(), bSwap ? bodyA.packed() : bodyB.packed() };
        }

        bool isStayListener( const PhysicsContactBody& body ) const { return _setStayUserData.contains( body._userData ); }

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
            const bool bSolid = state._first._bTrigger == false && state._second._bTrigger == false && state._first._bOverlapOnly == false &&
                                state._second._bOverlapOnly == false;
            event._bOverlap = bSolid ? SW_FALSE : SW_TRUE;
            event._impulse  = ( bSolid && phase != PhysicsContactPhase::End ) ? state._impulse : 0.0f;
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

    private:
        PairMap               _mapPair;
        vector<Event>         _listRestartEnd;  ///< `finishStep` 스크래치 — 재시작한 쌍의 옛 끝(용량을 다시 쓴다)
        unordered_set<uint64> _setStayUserData; ///< 유지를 듣는 사용자 값(`setStayListener`)
        uint8                 _bStayDirty : 1;  ///< 듣기가 바뀌어 닿은 쌍에 다시 적용해야 한다(`refreshStayPairs`)
    };
} // namespace sw
