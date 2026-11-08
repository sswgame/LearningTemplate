/**
 * @file Engine/Animation/Scene/AnimationFrameObserver.h
 * @brief 애니메이션 시스템의 프레임 관찰자 등록입니다 — 평가 앞에서 그 프레임을 대신하거나(기록된 포즈 걸기) 평가 뒤에 결과를 보는(기록) 개발 도구가 `SW_ANIMATION_FRAME_OBSERVER`
 *        한 줄로 스스로 등록하고, `AnimationSystem` 은 만들 때 등록마다 관찰자 하나를 만들어 들고 돌리기만 합니다.
 * @details 되감기 기록기(`DevTools/Client/Animation/AnimationRewind` — 배포 구성이 짓지 않는다)가 등록한다. 배포본에는 등록이 없어 시스템은 관찰자 없이 평가만 한다.
 *          예전에는 `AnimationSystem` 이 `#if !SW_SHIPPING` 으로 기록기 칸을 들고 테스트용 전역 변수를 읽었다. 언리얼 Rewind Debugger 의
 *          `IRewindDebuggerExtension`(모듈러 기능 등록 — 엔진은 무엇이 붙었는지 모른다)과 같은 자리다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Registry/RegistrationList.h"

namespace sw
{
    class AnimationSystem;

    /**
     * @class IAnimationFrameObserver
     * @brief `AnimationSystem` 하나에 등록마다 하나씩 붙는 관찰자입니다. 게임 스레드에서만 불립니다.
     */
    class IAnimationFrameObserver
    {
    public:
        virtual ~IAnimationFrameObserver() = default;

        /** @brief 등록 이름입니다(`AnimationSystem::findFrameObserver` 가 찾는다). */
        [[nodiscard]] virtual const utf8* getObserverName() const = 0;
        /** @brief 평가 앞입니다. 이번 프레임을 이 관찰자가 대신했으면(포즈를 스스로 걸었다) true — 시스템은 평가하지 않는다. */
        [[nodiscard]] virtual bool replaceEvaluation( AnimationSystem& system ) = 0;
        /** @brief 평가를 마친 뒤입니다(대신한 프레임에는 불리지 않는다). */
        virtual void onEvaluated( AnimationSystem& system, float32 deltaSeconds ) = 0;
        /** @brief 평가를 대신하는 중이면 true 입니다 — 스스로 시간을 흘리는 컴포넌트(스프라이트 애니메이터)가 멈춘다. */
        [[nodiscard]] virtual bool isReplacingEvaluation() const = 0;
    };

    /** @brief 관찰자 하나를 만드는 함수입니다. */
    using AnimationFrameObserverCreateFunction = unique_ptr<IAnimationFrameObserver> ( * )();

    /** @struct AnimationFrameObserverRegistration @brief 등록 항목 하나입니다. 문자열과 함수는 등록한 이미지의 정적 저장소에 있습니다. */
    struct AnimationFrameObserverRegistration
    {
        const utf8*                          _pName; ///< 관찰자 이름(예: `AnimationRewind`)
        AnimationFrameObserverCreateFunction _pCreate;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AnimationFrameObserverRegistry
     * @brief 프레임 관찰자 레지스트리입니다. 같은 이름이 둘이면 뒤의 것을 거절하고 오류를 남깁니다. 목록은 이름 순입니다. 게임 스레드와 정적 초기화에서만 쓴다.
     */
    class SW_API AnimationFrameObserverRegistry
    {
    public:
        /** @brief 등록합니다. 같은 이름이 이미 있으면 false 이고 등록하지 않습니다. */
        [[nodiscard]] static bool registerObserver( const AnimationFrameObserverRegistration* pRegistration );
        static void               unregisterObserver( const AnimationFrameObserverRegistration* pRegistration );
        /** @brief 등록마다 관찰자 하나를 만들어 @p outListObserver 에 붙입니다(이름 순). */
        static void createAll( vector<unique_ptr<IAnimationFrameObserver>>& outListObserver );
    };

    /** @brief `SW_ANIMATION_FRAME_OBSERVER` 가 두는 정적 등록자입니다(공통 틀 `StaticRegistrar` — 거절된 등록은 내리지 않는다). */
    SW_DEFINE_STATIC_REGISTRAR( AnimationFrameObserverRegistrar, const AnimationFrameObserverRegistration*, &AnimationFrameObserverRegistry::registerObserver, &AnimationFrameObserverRegistry::unregisterObserver );
} // namespace sw

/** @brief 애니메이션 프레임 관찰자를 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). @p id 는 정적 이름을 만드는 식별자입니다. */
#define SW_ANIMATION_FRAME_OBSERVER( id, pName, pCreate ) SW_STATIC_REGISTRATION( animationFrameObserver, ::sw::AnimationFrameObserverRegistration, ::sw::AnimationFrameObserverRegistrar, id, pName, pCreate )
