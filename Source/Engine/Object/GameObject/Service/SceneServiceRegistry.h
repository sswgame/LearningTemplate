/**
 * @file SceneServiceRegistry.h
 * @brief 씬 서비스(`ISceneService.h`)의 등록 줄 · 레지스트리 · 등록 매크로(`SW_SCENE_SERVICE`)입니다. 서비스 클래스의 .cpp 와 서비스를 만드는 매니저만 포함합니다.
 * @details 계약(`ISceneService`)을 보는 TU 는 서비스를 부르기만 하고, 등록 표의 의존(등록 목록 틀 · 씬 범위 · 프레임 단계 · 접근 선언)은 등록하는 .cpp 만 읽는다
 *          (헤더 팬인 다이어트 — 점수로 정했다. 언리얼도 `USubsystem` 계약과 수집기 `FSubsystemCollection` 을 다른 헤더에 둔다). 서비스 헤더는 `getSceneServiceRegistration` 을
 *          앞 선언(`SceneServiceRegistration`)으로만 든다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Registry/RegistrationList.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Object/GameObject/Service/ISceneService.h"
#include "Engine/Object/GameObject/Tick/SceneFrameAccess.h"
#include "Engine/Object/GameObject/Tick/SceneFrameStep.h"

namespace sw
{
    /** @brief 매니저 하나에 붙을 서비스를 만들어 씬 범위 @p scope 에 그 타입으로 넣습니다. */
    using CreateSceneServiceFunction = ISceneService& (*)( GameObjectManager & manager, SubsystemScope& scope );

    /** @struct SceneServiceRegistration @brief 등록 줄 하나입니다. 등록한 .cpp 의 정적 저장소에 있고, 그 주소가 서비스를 찾는 키입니다. */
    struct SceneServiceRegistration
    {
        CreateSceneServiceFunction _pCreate;                               ///< 서비스를 만드는 함수
        SceneFrameStep             _frameStep;                             ///< `update` 를 부를 프레임 단계
        SceneFrameAccess           _updateAccess;                          ///< `update`(게임 스레드)가 읽고 쓰는 씬 데이터 — 다른 서비스의 넘긴 일과 겹치는지 매니저가 본다
        SceneFrameStep             _completeStep{ SceneFrameStep::Count }; ///< `update` 가 넘긴 워커 일을 마치는(`completeFrameJob`) 단계 — 그 단계 본문 앞. `Count` 면 넘기는 일이 없다
        SceneFrameAccess           _jobAccess{};                           ///< 넘긴 일이 `_frameStep` 끝 ~ `_completeStep` 앞 사이에 읽고 쓰는 씬 데이터(일이 없으면 빈 선언)

        /** @brief 워커에 넘기는 프레임 일이 있으면 true 입니다. */
        constexpr bool hasFrameJob() const { return _completeStep != SceneFrameStep::Count; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class SceneServiceRegistry
     * @brief 씬 서비스의 레지스트리입니다. 정적 초기화에서 채우고 `GameObjectManager` 생성자가 읽습니다.
     */
    class SW_API SceneServiceRegistry
    {
    public:
        /** @brief 등록 줄을 더합니다. 같은 줄을 두 번 올리면 무시합니다(true). 빈 줄은 거절하고 false 입니다. */
        [[nodiscard]] static bool add( const SceneServiceRegistration* pRegistration );
        /** @brief 등록 줄을 뺍니다(등록한 모듈이 내려갈 때 정적 등록자가 부른다). 올라 있지 않으면 할 일이 없다. */
        static void remove( const SceneServiceRegistration* pRegistration );
        /** @brief 지금까지 올라온 등록 줄입니다(올린 순서). */
        static const vector<const SceneServiceRegistration*>& getAll();
    };
} // namespace sw

namespace sw
{
    /** @brief `SW_SCENE_SERVICE` 가 두는 정적 등록자입니다(공통 틀 `StaticRegistrar` — 모듈이 내려가면 뺀다). */
    SW_DEFINE_STATIC_REGISTRAR( SceneServiceRegistrar, const SceneServiceRegistration*, &SceneServiceRegistry::add, &SceneServiceRegistry::remove );

    /**
     * @brief 서비스를 만들어 씬 범위에 넣는 함수입니다. 서비스 타입이 완전한 자리(그 서비스의 .cpp)에서 실체화합니다. `TService( GameObjectManager& )` 생성자가 있어야 합니다.
     * @details 범위는 넣은 타입으로 찾고 그 타입으로 지운다(정렬 할당 타입도 된다).
     */
    template <typename TService>
    ISceneService& createSceneService( GameObjectManager& manager, SubsystemScope& scope )
    {
        return scope.addSubsystem( make_unique<TService>( manager ) );
    }
} // namespace sw

/**
 * @brief 서비스 @p Type 을 씬 서비스로 등록하고 `Type::getSceneServiceRegistration()` 을 정의합니다(서비스의 .cpp, 파일 범위, `namespace sw` 안).
 *        @p frameStep 은 `update` 를 부를 `SceneFrameStep` 의 값 이름, @p updateAccess 는 `update` 가 읽고 쓰는 씬 데이터입니다(필수 — 없으면 전처리 오류).
 *        접근은 `SceneFrameAccess{}`(씬 데이터를 건드리지 않음) 또는 `makeSceneFrameAccess( 쓰기, 읽기, 지난 값 읽기 )` 로 적습니다(괄호 밖 쉼표는 인자를 가른다).
 */
#define SW_SCENE_SERVICE( Type, frameStep, updateAccess ) SW_SCENE_SERVICE_WITH_FRAME_JOB( Type, frameStep, updateAccess, Count, ::sw::SceneFrameAccess{} )

/**
 * @brief `SW_SCENE_SERVICE` 와 같되 `update` 가 워커에 일을 넘기고 @p completeStep 단계 본문 앞에서 `completeFrameJob` 으로 마칩니다(파일 머리말의 "프레임 일").
 *        @p jobAccess 는 그 일이 사이에 읽고 쓰는 씬 데이터입니다.
 */
#define SW_SCENE_SERVICE_WITH_FRAME_JOB( Type, frameStep, updateAccess, completeStep, jobAccess )                                                                                                        \
    static const ::sw::SceneServiceRegistration sw_sceneService_##Type{ &::sw::createSceneService<Type>, ::sw::SceneFrameStep::frameStep, updateAccess, ::sw::SceneFrameStep::completeStep, jobAccess }; \
    static const ::sw::SceneServiceRegistrar    sw_sceneServiceRegistrar_##Type{ &sw_sceneService_##Type };                                                                                              \
    const ::sw::SceneServiceRegistration&       Type::getSceneServiceRegistration()                                                                                                                      \
    {                                                                                                                                                                                                    \
        return sw_sceneService_##Type;                                                                                                                                                                   \
    }
