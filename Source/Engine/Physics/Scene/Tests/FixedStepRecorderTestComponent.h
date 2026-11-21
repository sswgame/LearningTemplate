/**
 * @file FixedStepRecorderTestComponent.h
 * @brief 고정 스텝 그룹 테스트(`TestFixedStepGroup.cpp`)가 다는 기록 컴포넌트입니다 — 개발 구성만 코드젠하고 "Add Component" 메뉴에 나오지 않는다.
 * @details 생성기가 이 타입마다 `SW_REGISTER_FIXED_STEP_TYPE` 을 내므로 훅은 엔진과 같은 길(표 → 고정 스텝 목록 → 틱 스케줄러)로 불린다. 받은 그룹 · 스텝 칸을
 *          공용 기록(`_pLog`)에 차례대로 남긴다. 스텝 앞 훅에서 다른 구성원을 끄고 켜게 하면(`_pToggleTarget`) 도는 중 켜고 끄기가 안전한지 본다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/FixedStepContext.h"
#include "Engine/Object/GameObject/Tick/FixedStepTickList.h"
#include "Engine/Physics/Component/PhysicsComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 고정 스텝 훅 하나의 기록입니다. */
    struct FixedStepCallRecord
    {
        const Component* _pComponent{ nullptr };
        FixedStepGroup   _group{ FixedStepGroup::FrameBegin };
        uint32           _stepIndex{ 0 };
        uint32           _stepCount{ 0 };
        float32          _alpha{ 0.0f };
    };

    /** @brief 바디 단계의 기록 컴포넌트입니다(파일 머리말). 네 그룹을 모두 재정의한다. */
    REFLECT( HideInMenu )
    class FixedStepRecorderTestComponent : public PhysicsComponent
    {
    public:
        REFLECT_BODY();

        FixedStepRecorderTestComponent()
            : FixedStepRecorderTestComponent( PhysicsComponentPhase::Body )
        {
        }

        vector<FixedStepCallRecord>* _pLog{ nullptr };          ///< 공용 기록(테스트가 넣는다)
        const Component*             _pToggleTarget{ nullptr }; ///< 스텝 앞 훅에서 끄고(0 번 스텝) 다시 켤(1 번 스텝) 구성원
        uint32                       _toggleFrame{ 0 };         ///< 몇 번째 물리 프레임에서 끄고 켤지(프레임 시작 훅이 센다)
        uint32                       _frameIndex{ 0 };

        void onPhysicsFrameBegin( const FixedStepContext& context )
        {
            record( FixedStepGroup::FrameBegin, context );
            ++_frameIndex;
        }
        void onPrePhysicsStep( const FixedStepContext& context )
        {
            record( FixedStepGroup::PreStep, context );
            if ( _pToggleTarget == nullptr || _frameIndex != _toggleFrame + 1 || getOwner() == nullptr || getOwner()->getMutableManager() == nullptr )
                return;
            // 0 번 스텝에서 끄고 1 번 스텝에서 켠다 — 같은 그룹을 도는 중이라 이번 구간은 그대로 끝까지 돈다.
            FixedStepTickList& list = getOwner()->getMutableManager()->getFixedStepTickList();
            if ( context.getStepIndex() == 0 )
                (void)list.setEnabled( *_pToggleTarget, false );
            else if ( context.getStepIndex() == 1 )
                (void)list.setEnabled( *_pToggleTarget, true );
        }
        void onPostPhysicsStep( const FixedStepContext& context ) { record( FixedStepGroup::PostStep, context ); }
        void onPhysicsFrameEnd( const FixedStepContext& context ) { record( FixedStepGroup::FrameEnd, context ); }

    protected:
        explicit FixedStepRecorderTestComponent( PhysicsComponentPhase phase )
            : PhysicsComponent( phase )
        {
        }

    private:
        void record( FixedStepGroup group, const FixedStepContext& context )
        {
            if ( _pLog != nullptr )
                _pLog->push_back( FixedStepCallRecord{ this, group, context.getStepIndex(), context.getStepCount(), context.getAlpha() } );
        }
    };

    /** @brief 캐릭터 단계의 기록 컴포넌트입니다 — 훅은 기반의 것을 물려받는다(중간 기반의 재정의도 한정 호출로 불린다). 그룹 안에서 바디 단계 뒤에 돈다. */
    REFLECT( HideInMenu )
    class FixedStepLateRecorderTestComponent : public FixedStepRecorderTestComponent
    {
    public:
        REFLECT_BODY();

        FixedStepLateRecorderTestComponent()
            : FixedStepRecorderTestComponent( PhysicsComponentPhase::Character )
        {
        }
    };
} // namespace sw
