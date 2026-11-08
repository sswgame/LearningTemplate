/**
 * @file GameplayContext.h
 * @brief 조건 · 동작이 세계를 읽고 쓰는 문맥입니다. 형식 있는 칸(slot) 몇 개와 확률 씨앗만 듭니다.
 * @details 어휘는 Engine 에 있고 세계 상태(플래그 · 태그 · 블랙보드 · 달력)는 위층에 있다. 칸 형식은 그 값을 가진 층이 정한다(`struct GameFlagsSlot { using Type = const GameFlags; static constexpr uint32 kSlotID = ...; }`).
 *          없는 칸은 nullptr 이다 — 조건은 "모름" 으로 거짓을, 동작은 실패를 돌려준다. 실행기(행동 트리 · 스케줄 · 대화)는 자기가 아는 칸만 채운다.
 *          언리얼 Gameplay Ability 의 `FGameplayEffectContext` 처럼 실행기마다 다른 문맥을 한 형식으로 넘기는 자리이고, 이름이 아니라 칸 형식으로 찾아 틀린 형식을 꺼낼 수 없다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"

namespace sw
{
    /**
     * @class GameplayContext
     * @brief 칸 번호 → 포인터의 작은 고정 표와 씨앗입니다. 값을 소유하지 않습니다. 복사해도 할당이 없어 매 틱 만들어도 됩니다.
     */
    class GameplayContext
    {
    public:
        static constexpr uint32 kMaxSlot = 16; ///< 실행기 하나가 채우는 칸 수의 상한 — 지역 처리기가 AI 대상에 9 칸(위치 · 태그 · 대상 · 출처 · 플래그 읽기/쓰기 · 블랙보드 읽기/쓰기 · 동작 값)을 채운다. 표는 스택에 있어 커도 할당이 없다

        /** @brief 칸 @p TSlot 을 @p pValue 로 정합니다. 이미 있으면 바꿉니다. */
        template <typename TSlot>
        void set( typename TSlot::Type* pValue )
        {
            setRaw( TSlot::kSlotID, const_cast<void*>( static_cast<const void*>( pValue ) ) );
        }

        /** @brief 칸 @p TSlot 의 값입니다. 없으면 nullptr 입니다. */
        template <typename TSlot>
        typename TSlot::Type* find() const
        {
            return static_cast<typename TSlot::Type*>( findRaw( TSlot::kSlotID ) );
        }

        /**
         * @brief 확률 조건(`chance`)이 굴리는 값입니다. 같은 문맥 · 같은 호출 자리면 같은 답이 나온다.
         * @details 씨앗을 만드는 규칙은 실행기가 정한다 — 스케줄은 "날과 키의 해시" 라서 저장 · 재생 · 화면 밖 시뮬레이션이 같은 답을 낸다.
         *          식 안의 둘째 호출부터는 식이 호출 자리 번호를 섞은 문맥을 넘긴다(`mixCallSeed`) — 한 식의 `chance` 둘이 한 굴림을 나누지 않는다.
         */
        uint32 getSeed() const { return _seed; }
        void   setSeed( uint32 seed ) { _seed = seed; }
        /**
         * @brief 식 안의 호출 자리 번호(0 부터, 식의 글 순서)를 씨앗에 섞습니다. 0 은 씨앗을 그대로 둬 호출이 하나인 식의 답이 바뀌지 않습니다.
         * @details 단락 평가가 없으므로(`ConditionExpr`) 자리 번호는 평가 경로와 무관하게 글에서 정해진다 — 결정성이 유지된다.
         */
        void mixCallSeed( uint32 callIndex )
        {
            if ( callIndex != 0 )
                _seed = HashUtil::mix32( _seed ^ HashUtil::mix32( callIndex * 0x9e3779b1u ) );
        }

    private:
        struct Slot
        {
            uint32 _id{ 0 };
            void*  _pValue{ nullptr };
        };

        void setRaw( uint32 slotID, void* pValue )
        {
            for ( uint32 index = 0; index < _slotCount; ++index )
            {
                if ( _arrSlot[index]._id == slotID )
                {
                    _arrSlot[index]._pValue = pValue;
                    return;
                }
            }
            SW_ASSERT( _slotCount < kMaxSlot && "GameplayContext: raise kMaxSlot" );
            _arrSlot[_slotCount]._id     = slotID;
            _arrSlot[_slotCount]._pValue = pValue;
            ++_slotCount;
        }

        void* findRaw( uint32 slotID ) const
        {
            for ( uint32 index = 0; index < _slotCount; ++index )
            {
                if ( _arrSlot[index]._id == slotID )
                    return _arrSlot[index]._pValue;
            }
            return nullptr;
        }

    private:
        Slot   _arrSlot[kMaxSlot]{};
        uint32 _slotCount{ 0 };
        uint32 _seed{ 0 };
    };
} // namespace sw
