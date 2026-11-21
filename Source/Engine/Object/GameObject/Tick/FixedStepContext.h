/**
 * @file FixedStepContext.h
 * @brief 물리 고정 스텝 그룹(`FixedStepGroup`)과 그 훅이 받는 문맥(`FixedStepContext`)입니다 — 프레임 틱 그룹(`TickGroup`)과 나란한 두 번째 주기입니다.
 * @details 프레임 틱은 한 프레임에 한 번, 고정 스텝 훅은 물리 프레임 안에서 돈다(스텝 훅은 한 프레임 0..N 번). 주기는 나누고 스케줄링은 틱 시스템 하나가 한다 —
 *          컴포넌트가 등록 자리(`onRegister`)에서 멤버 함수 포인터로 명시 등록하고(`FixedStepTickList::add<&T::메서드>` — 프레임 틱 `registerSubTick` ·
 *          사건 `GameObjectEvents::listen<&T::메서드>` 와 같은 모양), 구성원 · 일괄 구간은 틱 스케줄러의 고정 스텝 목록, 디스패치는 같은 스케줄러
 *          (`SceneTickScheduler::tickFixedStepGroup`)다. 유니티 `Update` 대 `FixedUpdate`, Godot `_process` 대 `_physics_process`(같은 우선순위 장치),
 *          언리얼 틱 그룹 대 물리 서브스텝 콜백과 같은 나눔이다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class Component;
    class ScenePhysics;

    /**
     * @enum FixedStepGroup
     * @brief 물리 프레임 안의 훅 그룹입니다. 값의 차례가 부르는 차례입니다(`ScenePhysics::step`).
     * @details `FrameBegin` · `FrameEnd` 는 물리 프레임마다 한 번, `PreStep` · `PostStep` 은 고정 스텝마다(한 프레임 0..N 번) 돈다.
     */
    enum class FixedStepGroup : uint8
    {
        FrameBegin, ///< 물리 프레임 시작 — 백엔드 객체를 만들거나 트랜스폼의 바깥 변화를 옮긴다(`onPhysicsFrameBegin`)
        PreStep,    ///< 고정 스텝 직전 — 키네마틱 목표 · 힘 · 캐릭터 이동(`onPrePhysicsStep`)
        PostStep,   ///< 고정 스텝 직후 — 자세 기록(`onPostPhysicsStep`)
        FrameEnd,   ///< 물리 프레임 끝 — 보간 비로 자세를 트랜스폼에 쓴다(`onPhysicsFrameEnd`)
        Count
    };

    /**
     * @class FixedStepContext
     * @brief 고정 스텝 훅 하나가 받는 문맥입니다 — 틱 문맥(`TickContext`)처럼 훅 서명을 하나로 맞춘다. 그룹마다 뜻 있는 칸만 읽는다.
     * @details 스텝 칸(`getFixedDeltaTime` · `getStepIndex` · `getStepCount`)은 `PreStep` · `PostStep` 에서, 보간 비(`getAlpha`)는 `FrameEnd` 에서만 뜻이 있다.
     */
    class FixedStepContext final
    {
    public:
        FixedStepContext( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount, float32 alpha )
            : _pPhysics{ &physics }
            , _fixedDeltaTime{ fixedDeltaTime }
            , _stepIndex{ stepIndex }
            , _stepCount{ stepCount }
            , _alpha{ alpha }
        {
        }

        /** @brief 이 물리 프레임을 도는 씬 물리입니다. */
        ScenePhysics& getPhysics() const { return *_pPhysics; }
        /** @brief 고정 스텝 길이(초)입니다. */
        float32 getFixedDeltaTime() const { return _fixedDeltaTime; }
        /** @brief 이 프레임의 몇 번째 스텝인지입니다(0 부터). */
        uint32 getStepIndex() const { return _stepIndex; }
        /** @brief 이 프레임의 스텝 수입니다. */
        uint32 getStepCount() const { return _stepCount; }
        /** @brief 마지막 두 스텝 사이의 보간 비입니다(0..1). */
        float32 getAlpha() const { return _alpha; }

    private:
        ScenePhysics* _pPhysics;
        float32       _fixedDeltaTime;
        uint32        _stepIndex;
        uint32        _stepCount;
        float32       _alpha;
    };

    /**
     * @brief 같은 메서드를 등록한 컴포넌트 @p count 개(@p ppComponent)에 그 메서드를 부르는 함수입니다(`TickBatchFunction` 과 같은 자리).
     * @details 메서드마다 `runFixedStepBatch<&T::메서드>` 하나가 인스턴스화된다 — 메서드 포인터가 컴파일 시간 상수라 가상 호출이 아닌 직접 호출로 접힌다.
     */
    using FixedStepBatchFunction = void ( * )( const FixedStepContext& context, Component* const* ppComponent, uint32 count );

    /** @brief 고정 스텝 메서드 형식에서 그 클래스를 꺼냅니다. 서명이 `void ( T::* )( const FixedStepContext& )` 가 아니면 정의가 없어 컴파일이 멈춘다. */
    template <typename TMethod>
    struct FixedStepMethodClass;

    template <typename TClass>
    struct FixedStepMethodClass<void ( TClass::* )( const FixedStepContext& )>
    {
        using Type = TClass;
    };

    /**
     * @brief 메서드 @p TMethod 의 일괄 함수입니다 — 등록한 쪽(`FixedStepTickList::add<TMethod>`)의 TU 에서 인스턴스화된다.
     * @details 등록 자리에서 메서드를 이름으로 집으므로(접근 검사도 그 자리) 이름은 자유이고, 서명이 틀리면 `FixedStepMethodClass` 에서 컴파일이 멈춘다.
     */
    template <auto TMethod>
    void runFixedStepBatch( const FixedStepContext& context, Component* const* ppComponent, uint32 count )
    {
        using TClass = typename FixedStepMethodClass<decltype( TMethod )>::Type;
        static_assert( std::is_base_of_v<Component, TClass>, "fixed step methods belong to a component type" );
        for ( uint32 index = 0; index < count; ++index )
        {
            ( static_cast<TClass*>( ppComponent[index] )->*TMethod )( context );
        }
    }
} // namespace sw
