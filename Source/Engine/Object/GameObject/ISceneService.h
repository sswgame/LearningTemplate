/**
 * @file ISceneService.h
 * @brief 기능 폴더가 씬(`GameObjectManager`)마다 하나씩 두는 서비스의 계약과 등록표입니다. 서비스 클래스의 .cpp 에 `SW_SCENE_SERVICE` 한 줄을 둡니다.
 * @details 코어(`Object/GameObject`)는 등록표만 갖고 기능 서비스(내비게이션 …)의 타입을 모릅니다. 기능 폴더가 정적 초기화에서 이 표에 만드는 함수와
 *          돌 프레임 단계를 올리고, 매니저는 생성자에서 표를 훑어 서비스를 만들고 소유합니다 — 의존은 기능 → 코어 한 방향입니다.
 *          언리얼 `UWorldSubsystem`(모듈이 타입을 올리고 월드가 인스턴스를 만든다, `GetSubsystem<T>()`)과 같은 모양입니다.
 *
 *          - **갱신**: 매니저가 등록 줄의 단계(`SceneFrameStepList.xxx` 의 한 줄)에서 `update` 를 부릅니다(게임 스레드).
 *            한 단계에 서비스가 여럿이면 순서는 등록 순서(링크 순서)라 정해져 있지 않습니다 — 서로 기대지 않는 것만 한 단계에 둡니다.
 *          - **비우기**: `GameObjectManager::clear` 가 `shutdown` 을 부릅니다. 서비스는 남고 다음 씬에서 다시 씁니다.
 *          - **얻기**: `manager.getSceneService<T>()` 입니다. `T` 는 `getSceneServiceRegistration()` 정적 함수를 선언합니다(매크로가 정의한다).
 *
 * @note 표는 Engine 이미지 안에서만 채웁니다. 표의 항목은 내리지 않으므로, 핫 리로드 되는 모듈(게임 · 키트)이 쓰면 언로드 뒤 죽은 함수를 가리킵니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Object/GameObject/SceneFrameStep.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class ISceneService
     * @brief 씬마다 하나 있는 기능 서비스입니다. 매니저가 만들고 소유합니다(파일 머리말 참고).
     */
    class SW_API ISceneService
    {
    public:
        virtual ~ISceneService() = default;

        /** @brief 이번 프레임을 진행합니다(게임 스레드, 등록 줄의 단계에서). */
        virtual void update( float32 deltaTime ) = 0;
        /** @brief 씬을 비울 때 들고 있던 것을 놓습니다(`GameObjectManager::clear`). 서비스 자체는 남습니다. */
        virtual void shutdown() = 0;

    protected:
        ISceneService()                                  = default;
        ISceneService( const ISceneService& )            = delete;
        ISceneService& operator=( const ISceneService& ) = delete;
    };
} // namespace sw

namespace sw
{
    /** @brief 매니저 하나에 붙을 서비스를 만듭니다. */
    using CreateSceneServiceFunction = unique_ptr<ISceneService> ( * )( GameObjectManager& manager );

    /** @struct SceneServiceRegistration @brief 등록 줄 하나입니다. 등록한 .cpp 의 정적 저장소에 있고, 그 주소가 서비스를 찾는 키입니다. */
    struct SceneServiceRegistration
    {
        CreateSceneServiceFunction _pCreate;   ///< 서비스를 만드는 함수
        SceneFrameStep             _frameStep; ///< `update` 를 부를 프레임 단계
    };
} // namespace sw

namespace sw
{
    /**
     * @class SceneServiceRegistry
     * @brief 씬 서비스의 등록표입니다. 정적 초기화에서 채우고 `GameObjectManager` 생성자가 읽습니다.
     */
    class SW_API SceneServiceRegistry
    {
    public:
        /** @brief 등록 줄을 더합니다. 같은 줄을 두 번 올리면 무시합니다. */
        static void add( const SceneServiceRegistration* pRegistration );
        /** @brief 지금까지 올라온 등록 줄입니다(올린 순서). */
        static const vector<const SceneServiceRegistration*>& getAll();
    };
} // namespace sw

namespace sw
{
    /** @brief `SW_SCENE_SERVICE` 가 두는 정적 등록자입니다. */
    struct SW_API SceneServiceRegistrar
    {
        explicit SceneServiceRegistrar( const SceneServiceRegistration* pRegistration );
        SceneServiceRegistrar( const SceneServiceRegistrar& )            = delete;
        SceneServiceRegistrar& operator=( const SceneServiceRegistrar& ) = delete;
    };

    /** @brief 서비스를 만드는 함수입니다. 서비스 타입이 완전한 자리(그 서비스의 .cpp)에서 실체화합니다. `TService( GameObjectManager& )` 생성자가 있어야 합니다. */
    template <typename TService>
    unique_ptr<ISceneService> createSceneService( GameObjectManager& manager )
    {
        // 기반 포인터로 지우므로 정렬 할당을 쓰는 타입이면 해제가 어긋난다.
        static_assert( kUsesAlignedAllocation<TService> == false, "scene service must not be over-aligned" );
        return make_unique<TService>( manager );
    }
} // namespace sw

/**
 * @brief 서비스 @p Type 을 씬 서비스로 등록하고 `Type::getSceneServiceRegistration()` 을 정의합니다(서비스의 .cpp, 파일 범위, `namespace sw` 안).
 *        @p frameStep 은 `update` 를 부를 `SceneFrameStep` 의 값 이름입니다.
 */
#define SW_SCENE_SERVICE( Type, frameStep )                                                                                                 \
    static const ::sw::SceneServiceRegistration sw_sceneService_##Type{ &::sw::createSceneService<Type>, ::sw::SceneFrameStep::frameStep }; \
    static const ::sw::SceneServiceRegistrar    sw_sceneServiceRegistrar_##Type{ &sw_sceneService_##Type };                                 \
    const ::sw::SceneServiceRegistration&       Type::getSceneServiceRegistration()                                                         \
    {                                                                                                                                       \
        return sw_sceneService_##Type;                                                                                                      \
    }
