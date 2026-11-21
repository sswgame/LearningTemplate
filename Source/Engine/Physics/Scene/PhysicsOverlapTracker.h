/**
 * @file PhysicsOverlapTracker.h
 * @brief 바디 쌍 겹침 시작 · 끝을 오브젝트 쌍의 진입 · 이탈 · 유지로 바꾸는 추적기입니다(트리거 계약 — docs/agents/Gameplay.md "트리거 · 물리").
 * @details 겹침 사건(`GameObjectEvents::_overlapBegin` · `_overlapEnd`)은 오브젝트 전체에 가므로 단위는 오브젝트 쌍이다. 몸이 여럿인 오브젝트(래그돌 뼈 · 탈것 · 캐릭터 안쪽 바디)가
 *          트리거에 들면 바디 쌍 시작이 여럿 오지만 진입은 한 번, 마지막 바디가 나갈 때 이탈 한 번이다(언리얼 `BeginOverlap` 한 번 · 유니티 콜라이더 쌍과 달리 오브젝트 단위).
 *          열쇠는 (두 오브젝트 id, 각 쪽 트리거 여부, 2D/3D)다 — 상대의 감지 범위(트리거)와 몸은 받는 쪽이 `OverlapInfo::_bOtherTrigger` 로 가르므로 따로 센다.
 *          프레임마다 정리한다: 프레임 시작과 끝의 수로 이탈(1→0) 먼저, 진입(0→1), 같은 프레임에 들고 난 것(0→0 인데 닿음 — 시작 + 끝) 순이다. 프레임 안에서 나갔다
 *          다시 든 것(1→1)은 내지 않는다 — 트리거 켜고 끄기 · 바디 다시 짓기가 헛 이탈 · 진입을 내지 않게. 유지는 프레임 끝에 안에 있는 쌍마다 한 번(진입 프레임 포함)이다.
 *          차례는 결정적이다 — 첫 알림 차례(백엔드 이벤트 순서)와 열쇠 순서다. 잠그지 않는다(게임 스레드 하나).
 */
#pragma once
#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"

#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    /** @brief 겹침 한 건입니다 — 받는 알림(바디 쌍 시작 · 끝)이자 내는 결과(오브젝트 쌍 진입 · 이탈 · 유지)입니다. 결과의 바디는 그 쌍에서 마지막으로 알린 바디입니다. */
    struct PhysicsOverlapEvent
    {
        uint64              _objectA{ 0 };
        uint64              _objectB{ 0 };
        PhysicsBodyHandle   _bodyA{};
        PhysicsBodyHandle   _bodyB{};
        float32             _hitFraction{ 1.0f }; ///< 쓸어 낸 겹침의 닿은 시점(0..1), 나머지는 1
        PhysicsContactPhase _phase{ PhysicsContactPhase::Begin };
        bool                _bTriggerA{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool                _bTriggerB{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool                _bIs2D{ false };     ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };

    /** @class PhysicsOverlapTracker @brief 파일 머리말 참고. */
    class PhysicsOverlapTracker
    {
    public:
        PhysicsOverlapTracker()
            : _mapPair{}
            , _listTouched{}
            , _listBegin{}
            , _listPassed{}
        {
        }

        /** @brief 바디 쌍 시작 · 끝 하나를 셉니다. 유지 알림은 무시합니다(유지는 `finishFrame` 이 오브젝트 쌍마다 만든다). 모르는 쌍의 끝은 무시합니다. */
        void record( const PhysicsOverlapEvent& event )
        {
            if ( event._phase == PhysicsContactPhase::Stay )
                return;
            const PhysicsOverlapEvent ordered = makeOrdered( event );
            const PairKey             key     = makeKey( ordered );
            PairMap::iterator         iter    = _mapPair.find( key );
            if ( iter == _mapPair.end() )
            {
                if ( ordered._phase == PhysicsContactPhase::End )
                    return;
                iter = _mapPair.emplace( key, PairState{} ).first;
            }
            PairState& state = iter->second;
            if ( state._bTouched == false )
            {
                state._bTouched          = true;
                state._countAtFrameStart = state._count;
                _listTouched.push_back( key );
            }
            state._last = ordered;
            if ( ordered._phase == PhysicsContactPhase::Begin )
            {
                ++state._count;
                state._bBegunThisFrame = true;
            }
            else if ( state._count > 0 )
            {
                --state._count;
            }
        }

        /**
         * @brief 이번 프레임의 진입 · 이탈을 @p outListTransition 에(먼저 비운다), @p bWantStay 면 안에 있는 쌍의 유지를 @p outListStay 에(먼저 비운다) 냅니다.
         * @details 차례는 이탈 → 진입 → 들고 난 것(시작, 끝)이다. 다 나간 쌍은 잊는다.
         */
        void finishFrame( vector<PhysicsOverlapEvent>& outListTransition, vector<PhysicsOverlapEvent>& outListStay, bool bWantStay )
        {
            outListTransition.clear();
            outListStay.clear();
            _listBegin.clear();
            _listPassed.clear();
            for ( const PairKey& key : _listTouched )
            {
                PairMap::iterator iter = _mapPair.find( key );
                if ( iter == _mapPair.end() )
                    continue;
                PairState& state = iter->second;
                if ( state._countAtFrameStart > 0 && state._count == 0 )
                    outListTransition.push_back( withPhase( state._last, PhysicsContactPhase::End ) );
                else if ( state._countAtFrameStart == 0 && state._count > 0 )
                    _listBegin.push_back( withPhase( state._last, PhysicsContactPhase::Begin ) );
                else if ( state._countAtFrameStart == 0 && state._bBegunThisFrame )
                    _listPassed.push_back( state._last );
                state._bTouched        = false;
                state._bBegunThisFrame = false;
                if ( state._count == 0 )
                    _mapPair.erase( iter );
            }
            _listTouched.clear();
            outListTransition.insert( outListTransition.end(), _listBegin.begin(), _listBegin.end() );
            for ( const PhysicsOverlapEvent& passedEvent : _listPassed )
            {
                outListTransition.push_back( withPhase( passedEvent, PhysicsContactPhase::Begin ) );
                outListTransition.push_back( withPhase( passedEvent, PhysicsContactPhase::End ) );
            }
            if ( bWantStay == false )
                return;
            for ( const auto& [key, state] : _mapPair )
            {
                outListStay.push_back( withPhase( state._last, PhysicsContactPhase::Stay ) );
            }
            VectorUtil::sort( outListStay, &isEarlierKey ); // 해시 표 순서에 기대지 않는다
        }

        /** @brief 두 오브젝트 사이(트리거 여부 · 차원이 다른 쌍을 모두 더해) 지금 겹친 바디 쌍 수입니다(테스트 · 감시 값). */
        uint32 getBodyPairCount( uint64 objectA, uint64 objectB ) const
        {
            const uint64 first  = objectA < objectB ? objectA : objectB;
            const uint64 second = objectA < objectB ? objectB : objectA;
            uint32       count  = 0;
            for ( const auto& [key, state] : _mapPair )
            {
                if ( key._first == first && key._second == second )
                    count += static_cast<uint32>( state._count );
            }
            return count;
        }

        /** @brief 오브젝트 @p object 가 낀 겹친 오브젝트 쌍 수입니다 — 트리거 여부 · 차원이 다른 쌍은 따로 센다(감시 값 `Physics.OverlapCount`). */
        uint32 getOverlapCount( uint64 object ) const
        {
            uint32 count = 0;
            for ( const auto& [key, state] : _mapPair )
            {
                const bool bInvolved = ( key._first == object || key._second == object ) && state._count > 0;
                count += bInvolved ? 1u : 0u;
            }
            return count;
        }

        /** @brief 모든 쌍을 잊습니다(이탈 없이 — 씬을 내릴 때). */
        void clear()
        {
            _mapPair.clear();
            _listTouched.clear();
        }

    private:
        struct PairKey
        {
            uint64 _first{ 0 };
            uint64 _second{ 0 };
            uint8  _flags{ 0 }; ///< 비트 0 · 1: 첫째 · 둘째 트리거, 비트 2: 2D

            bool operator==( const PairKey& other ) const noexcept { return _first == other._first && _second == other._second && _flags == other._flags; }
        };

        struct PairKeyHash
        {
            size_t operator()( const PairKey& key ) const noexcept
            {
                size_t hash = std::hash<uint64>{}( key._first );
                hash        = HashUtil::combine( hash, std::hash<uint64>{}( key._second ) );
                hash        = HashUtil::combine( hash, std::hash<uint8>{}( key._flags ) );
                return hash;
            }
        };

        struct PairState
        {
            PhysicsOverlapEvent _last{};
            int32               _count{ 0 };
            int32               _countAtFrameStart{ 0 };
            bool                _bTouched{ false };        ///< 이번 프레임에 알림이 왔다. NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            bool                _bBegunThisFrame{ false }; ///< 이번 프레임에 시작이 왔다(들고 남을 가린다). NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };

        using PairMap = unordered_map<PairKey, PairState, PairKeyHash>;

        /** @brief 작은 오브젝트 id 를 A 로 둡니다(같으면 작은 바디가 A). */
        static PhysicsOverlapEvent makeOrdered( const PhysicsOverlapEvent& event )
        {
            const bool bSwap = event._objectB < event._objectA || ( event._objectB == event._objectA && event._bodyB < event._bodyA );
            if ( bSwap == false )
                return event;
            PhysicsOverlapEvent swapped = event;
            swapped._objectA            = event._objectB;
            swapped._objectB            = event._objectA;
            swapped._bodyA              = event._bodyB;
            swapped._bodyB              = event._bodyA;
            swapped._bTriggerA          = event._bTriggerB;
            swapped._bTriggerB          = event._bTriggerA;
            return swapped;
        }

        static PairKey makeKey( const PhysicsOverlapEvent& ordered )
        {
            const uint8 flags = static_cast<uint8>( ( ordered._bTriggerA ? 1u : 0u ) | ( ordered._bTriggerB ? 2u : 0u ) | ( ordered._bIs2D ? 4u : 0u ) );
            return PairKey{ ordered._objectA, ordered._objectB, flags };
        }

        static PhysicsOverlapEvent withPhase( const PhysicsOverlapEvent& event, PhysicsContactPhase phase )
        {
            PhysicsOverlapEvent result = event;
            result._phase              = phase;
            if ( phase != PhysicsContactPhase::Begin )
                result._hitFraction = 1.0f;
            return result;
        }

        static bool isEarlierKey( const PhysicsOverlapEvent& lhs, const PhysicsOverlapEvent& rhs )
        {
            const PairKey lhsKey = makeKey( lhs );
            const PairKey rhsKey = makeKey( rhs );
            if ( lhsKey._first != rhsKey._first )
                return lhsKey._first < rhsKey._first;
            if ( lhsKey._second != rhsKey._second )
                return lhsKey._second < rhsKey._second;
            return lhsKey._flags < rhsKey._flags;
        }

    private:
        PairMap                     _mapPair;
        vector<PairKey>             _listTouched; ///< 이번 프레임에 알림이 온 쌍(첫 알림 차례)
        vector<PhysicsOverlapEvent> _listBegin;   ///< `finishFrame` 스크래치(용량 재사용)
        vector<PhysicsOverlapEvent> _listPassed;  ///< 위와 같다
    };
} // namespace sw
