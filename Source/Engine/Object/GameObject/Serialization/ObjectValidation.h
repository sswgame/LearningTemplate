/**
 * @file ObjectValidation.h
 * @brief GameObject 의 컴포넌트마다 리플렉션 검증 함수(`Validate = fn`)를 돌려 결과를 그 오브젝트가 사는 월드의 표(`SceneValidationIssues`)에 둡니다.
 * @details 플레이를 시작할 때는 빈 필수 칸만 본다(`reportEmptyRequired`). 오브젝트 상태를 읽은 뒤(`ObjectStateBatch::finish`) · 저장하기 전(`ObjectStateSerializer`) · 인스펙터가 값을 고친 뒤 부릅니다.
 *          검증 함수가 없는 타입은 건너뜁니다(`ReflectionValidation::hasValidator` — 등록 때 한 번 구해 둔 답).
 *          에디터 데이터 검증이라 배포에는 없다(언리얼 `UObject::IsDataValid` · Map Check 가 `WITH_EDITOR` 인 것과 같다). 구현과 결과 표(`Dev/SceneValidationIssues.h`)는
 *          개발 전용 소스(`Serialization/Dev/`)에 있고 배포 구성은 짓지 않는다. 입구는 인라인 함수라 배포에서는 빈 함수다 — 버린 `if constexpr` 갈래가 쓰는 함수는
 *          정의가 없어도 된다(C++17 [basic.def.odr]). 배포에서도 도는 읽기 검사는 타입 검증(`REFLECT( Validate )`, `ReflectionValidation::validateObject`)이다.
 */
#pragma once
#include "Core/Foundation/BuildConfig.h"
#include "Core/Foundation/Types.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    class Component;
    class GameObject;
    class GameObjectManager;
    class ValidationContext;
} // namespace sw

namespace sw
{
    /** @brief 오브젝트 하나의 검증입니다. */
    struct SW_API ObjectValidation
    {
        /** @brief 검증하는 구성인가(배포는 하지 않는다). */
        static constexpr bool kEnabled = build::kShipping == false;

        /** @brief 컴포넌트마다 검증해 @p context 에 적고, 적힌 수를 돌려줍니다. 출처는 그 오브젝트(id · 이름)입니다. */
        static uint32 validateGameObject( const GameObject& object, ValidationContext& context )
        {
            if constexpr ( kEnabled )
                return validateGameObjectInternal( object, context );
            else
                return 0;
        }
        /**
         * @brief 검증해 그 오브젝트의 결과를 그 월드의 표(`SceneValidationIssues`)에서 바꿉니다(결과가 없으면 지운다).
         * @param bLog 결과마다 경고 로그를 남길지 — 로드 · 저장은 남기고, 프레임마다 오는 인스펙터 편집은 남기지 않는다.
         * @return 적힌 결과 수
         */
        static uint32 reportGameObject( const GameObject& object, const bool bLog )
        {
            if constexpr ( kEnabled )
                return reportGameObjectInternal( object, bLog );
            else
                return 0;
        }
        /**
         * @brief 플레이를 시작하는 컴포넌트의 빈 필수 이름 칸(`PROPERTY( Required )`)마다 오류를 남기고 그 수를 돌려줍니다(`Component::dispatchBeginPlay`).
         * @details 로드 · 저장은 데이터의 빠진 칸을 잡지만 코드로 붙인 컴포넌트(`addComponent`)는 그 길을 지나지 않는다 — 반만 채운 컴포넌트가 돌다 다른 자리에서 이상해졌다.
         *          언리얼 `AActor::PostInitializeComponents` 의 검증 · Godot `_get_configuration_warnings` 자리다. 필수 칸이 없는 타입은 타입마다 한 번 구한 깃발만 본다.
         */
        static uint32 reportEmptyRequired( const Component& component )
        {
            if constexpr ( kEnabled )
                return reportEmptyRequiredInternal( component );
            else
                return 0;
        }
        /**
         * @brief 사라지는 오브젝트(@p objectID)의 결과를 그 월드(@p pManager)의 표에서 지웁니다(`GameObject` 소멸자). 매니저 밖 오브젝트(nullptr)면 할 일이 없다.
         * @details 매니저를 소멸자가 넘긴다 — 소멸은 틱 표지 단언이 있는 `getMutableManager` 를 지나지 않는다.
         */
        static void removeGameObjectIssues( GameObjectManager* pManager, const uint64 objectID )
        {
            if constexpr ( kEnabled )
                removeGameObjectIssuesInternal( pManager, objectID );
        }

    private:
        static uint32 validateGameObjectInternal( const GameObject& object, ValidationContext& context );
        static uint32 reportGameObjectInternal( const GameObject& object, bool bLog );
        static uint32 reportEmptyRequiredInternal( const Component& component );
        static void   removeGameObjectIssuesInternal( GameObjectManager* pManager, uint64 objectID );
    };
} // namespace sw
