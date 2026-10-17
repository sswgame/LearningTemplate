/**
 * @file AnimationPlayer.h
 * @brief 재생할 것(`IAnimationPlayable`) 두 슬롯을 크로스페이드하는 플레이어, 동기 그룹, 상태 기계 파라미터입니다. 2D 와 3D 가 함께 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/AnimationPlayback.h"

namespace sw
{
    /**
     * @class AnimationPlayer
     * @brief 현재 슬롯과 다음 슬롯(크로스페이드 대상)을 가집니다. 시간을 흘리고, 지나간 알림을 섞임 가중치와 함께 모읍니다.
     * @details 무엇을 샘플하는지(스프라이트 프레임, 본 포즈)는 모른다. 호출하는 쪽이 슬롯의 재생할 것, 시각, 가중치로 샘플한다.
     *          페이드가 끝나면 다음 슬롯이 현재 슬롯이 된다. 속도는 0 이상이다(역재생은 지원하지 않는다).
     */
    class SW_API AnimationPlayer
    {
    public:
        AnimationPlayer();

        /** @brief 즉시 재생합니다(페이드 없음). nullptr 이면 멈춥니다. */
        void play( const IAnimationPlayable* pPlayable, bool bLoop );
        /** @brief @p fadeSeconds 동안 섞으며 넘어갑니다. 현재 재생이 없거나 길이가 0 이면 즉시 재생합니다. */
        void crossfade( const IAnimationPlayable* pPlayable, float32 fadeSeconds, bool bLoop );
        /**
         * @brief 섞지 않고 바로 넘어가되, 포즈를 내는 쪽이 끊긴 순간의 차이를 @p halfLifeSeconds 반감기로 풀어 내게 합니다(inertialization).
         * @details 플레이어는 포즈를 모르므로 요청 횟수와 반감기만 남긴다. 포즈를 내는 쪽(스켈레탈 애니메이터)은 `getInertializationCount` 가 오르면
         *          `PoseInertializer::begin` 으로 직전 포즈와 새 포즈의 차이를 잡는다. 페이드 중이었으면 섞이던 슬롯은 버린다(차이가 이어 준다).
         */
        void inertialize( const IAnimationPlayable* pPlayable, float32 halfLifeSeconds, bool bLoop );
        /** @brief 멈추고 비웁니다. */
        void stop() { play( nullptr, false ); }
        /**
         * @brief 시간을 흘립니다(속도 배율 적용). 지나간 알림은 @p pOutListFired 에 덧붙입니다(선택).
         * @details 페이드 중이면 두 슬롯이 함께 흐르고, 각자 자기 가중치로 알림을 낸다.
         */
        void update( float32 deltaSeconds, vector<AnimationFiredNotify>* pOutListFired );

        /** @brief 재생 속도 배율입니다(음수는 0 으로 막습니다). */
        void    setSpeed( float32 speed ) { _speed = speed > 0.0f ? speed : 0.0f; }
        float32 getSpeed() const { return _speed; }
        /** @brief 페이드 중에 다른 페이드로 끊긴 횟수입니다. 끊길 때 섞이던 슬롯이 버려지므로, 포즈를 내는 쪽은 이 값이 오르면 직전 포즈에서 이어서 섞어야 합니다. */
        uint32 getInterruptCount() const { return _interruptCount; }
        /** @brief 현재(또는 마지막) 페이드 길이(초)입니다. */
        float32 getFadeDuration() const { return _fadeDuration; }
        /** @brief `inertialize` 로 넘어간 횟수입니다. 포즈를 내는 쪽은 이 값이 오르면 직전 포즈에서 inertialization 을 시작합니다. */
        uint32 getInertializationCount() const { return _inertializationCount; }
        /** @brief 마지막 `inertialize` 의 반감기(초)입니다. */
        float32 getInertializationHalfLife() const { return _inertializationHalfLife; }

        const IAnimationPlayable* getCurrentPlayable() const { return _arrSlot[0]._pPlayable; }
        const IAnimationPlayable* getNextPlayable() const { return _arrSlot[1]._pPlayable; }
        bool                      isCurrentLooping() const { return _arrSlot[0]._bLoop == SW_TRUE; }
        bool                      isNextLooping() const { return _arrSlot[1]._bLoop == SW_TRUE; }
        bool                      isCrossfading() const { return _arrSlot[1]._pPlayable != nullptr; }
        /** @brief 현재 슬롯이 반복 없이 끝까지 갔으면 true 입니다(재생할 것이 없어도 true). */
        bool hasFinished() const;
        /** @brief 다음 슬롯의 섞임 가중치(0..1)입니다. 페이드 중이 아니면 0 입니다. */
        float32 getBlendAlpha() const;

        float32 getCurrentTime() const { return _arrSlot[0]._cursor.getTime(); }
        float32 getNextTime() const { return _arrSlot[1]._cursor.getTime(); }
        /** @brief 현재 슬롯의 반복 여부를 바꿉니다(재생 중에도 가능합니다). */
        void setCurrentLooping( bool bLoop ) { _arrSlot[0]._bLoop = bLoop ? SW_TRUE : SW_FALSE; }
        /** @brief 현재 슬롯의 시각을 옮깁니다(프레임 지정, 핫 리로드 복원). */
        void setCurrentTime( float32 time );
        /** @brief 현재 슬롯의 정규화 위치(0..1)를 반환합니다. */
        float32 getCurrentNormalizedTime() const;
        /** @brief 동기 그룹이 현재 슬롯(과 다음 슬롯)을 리더의 정규화 위치로 맞춥니다. */
        void setNormalizedTime( float32 normalizedTime );
        /** @brief 마지막 갱신에서 현재 슬롯이 지나간 구간을 반환합니다(루트 모션). */
        const AnimationTimeStep& getCurrentStep() const { return _arrSlot[0]._lastStep; }
        /** @brief 마지막 갱신에서 다음 슬롯이 지나간 구간을 반환합니다. */
        const AnimationTimeStep& getNextStep() const { return _arrSlot[1]._lastStep; }

    private:
        /** @brief 슬롯 하나입니다. 재생할 것, 위치, 반복, 마지막 이동 구간을 담습니다. */
        struct Slot
        {
            const IAnimationPlayable* _pPlayable{ nullptr };
            AnimationClipCursor       _cursor{};
            AnimationTimeStep         _lastStep{};
            uint8                     _bLoop{ SW_FALSE };
        };

        Slot    _arrSlot[2];
        float32 _fadeDuration;
        float32 _fadeElapsed;
        float32 _speed;
        float32 _inertializationHalfLife;
        uint32  _interruptCount;
        uint32  _inertializationCount;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimationSyncGroup
     * @brief 동기 그룹입니다. 가중치가 가장 큰 플레이어(리더)의 정규화 위치에 나머지(팔로워)를 맞춥니다(언리얼 Sync Group 의 마커 없는 위상 동기에 해당합니다).
     * @details 걷기 1.0 초와 달리기 0.7 초를 섞어도 발이 같은 위상을 딛는다. 리더의 가중치가 같으면 앞의 것이 리더다.
     */
    struct SW_API AnimationSyncGroup
    {
        /** @return 리더의 인덱스입니다. 목록이 비었으면 -1 입니다. */
        static int32 synchronize( AnimationPlayer* const* ppPlayer, const float32* pWeight, uint32 count );
    };
} // namespace sw

namespace sw
{
    /**
     * @class AnimationParameterSet
     * @brief 상태 기계 파라미터(이름 → 실수)입니다. 불은 0/1, 트리거는 한 번 쓰이면 0 으로 돌아가는 1 입니다.
     */
    class SW_API AnimationParameterSet
    {
    public:
        AnimationParameterSet() = default;

        void    setFloat( const hashed_string& name, float32 value );
        float32 getFloat( const hashed_string& name ) const;
        void    setBool( const hashed_string& name, bool bValue ) { setFloat( name, bValue ? 1.0f : 0.0f ); }
        bool    isTrue( const hashed_string& name ) const { return getFloat( name ) != 0.0f; }
        /** @brief 트리거를 켭니다. 그것을 조건으로 쓴 전이가 일어나면 꺼집니다(`consumeTrigger`). */
        void setTrigger( const hashed_string& name ) { setFloat( name, 1.0f ); }
        /** @brief 트리거를 끕니다. */
        void consumeTrigger( const hashed_string& name ) { setFloat( name, 0.0f ); }

    private:
        /** @brief 이름과 값 한 쌍입니다. 파라미터는 몇 개뿐이라 선형으로 찾습니다. */
        struct Entry
        {
            hashed_string _name;
            float32       _value{ 0.0f };
        };
        vector<Entry> _listEntry;
    };
} // namespace sw
