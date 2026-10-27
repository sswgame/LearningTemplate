/**
 * @file ObjectValidation.h
 * @brief GameObject 의 컴포넌트마다 리플렉션 검증 함수(`Validate = fn`)를 돌려 결과를 그 오브젝트가 사는 월드의 표(`SceneValidationIssues`)에 둡니다.
 * @details 오브젝트 상태를 읽은 뒤(`ObjectStateBatch::finish`) · 저장하기 전(`ObjectStateSerializer`) · 인스펙터가 값을 고친 뒤 부릅니다.
 *          검증 함수가 없는 타입은 건너뜁니다(`ReflectionValidation::hasValidator` — 등록 때 한 번 구해 둔 답).
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Object/GameObject/Service/ISceneService.h"
#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;
    class ValidationContext;
} // namespace sw

namespace sw
{
    /** @brief 오브젝트 하나의 검증입니다. */
    struct SW_API ObjectValidation
    {
        /** @brief 컴포넌트마다 검증해 @p context 에 적고, 적힌 수를 돌려줍니다. 출처는 그 오브젝트(id · 이름)입니다. */
        static uint32 validateGameObject( const GameObject& object, ValidationContext& context );
        /**
         * @brief 검증해 그 오브젝트의 결과를 그 월드의 표(`SceneValidationIssues`)에서 바꿉니다(결과가 없으면 지운다).
         * @param bLog 결과마다 경고 로그를 남길지 — 로드 · 저장은 남기고, 프레임마다 오는 인스펙터 편집은 남기지 않는다.
         * @return 적힌 결과 수
         */
        static uint32 reportGameObject( const GameObject& object, bool bLog );
    };
} // namespace sw

namespace sw
{
    /**
     * @class SceneValidationIssues
     * @brief 월드(씬)마다 하나 있는 검증 결과 표입니다(`manager.getSubsystem<SceneValidationIssues>().getLog()`).
     * @details 출처마다의 결과라 월드가 소유한다 — 에디터 미리보기 창 · 씬 둘의 결과가 섞이지 않는다(언리얼 Map Check 의 월드별 메시지 로그).
     *          씬을 비우면(`GameObjectManager::clear`) 표도 비고, 오브젝트 파괴는 `GameObject` 소멸자가 자기 출처를 지운다.
     */
    class SW_API SceneValidationIssues final : public ISceneService
    {
    public:
        /** @brief 이 서비스의 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        static constexpr const utf8*           kSubsystemName = "SceneValidationIssues";
        const utf8*                            getSubsystemName() const override { return kSubsystemName; }

        explicit SceneValidationIssues( GameObjectManager& manager );

        SceneValidationIssues( const SceneValidationIssues& )            = delete;
        SceneValidationIssues& operator=( const SceneValidationIssues& ) = delete;

        /** @brief 프레임마다 할 일이 없습니다(결과는 검증하는 쪽이 바꾼다). */
        void update( float32 deltaTime ) override;
        /** @brief 씬을 비울 때 결과도 버립니다. */
        void onSceneCleared() override;

        ValidationIssueLog&       getLog() { return _log; }
        const ValidationIssueLog& getLog() const { return _log; }

    private:
        ValidationIssueLog _log;
    };
} // namespace sw
