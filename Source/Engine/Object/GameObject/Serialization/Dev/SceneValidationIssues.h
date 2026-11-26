/**
 * @file SceneValidationIssues.h
 * @brief 월드(씬)마다 하나 있는 검증 결과 표입니다(`manager.getSubsystem<SceneValidationIssues>().getLog()`). 개발 전용 소스라 배포 구성은 짓지 않는다.
 * @details 결과를 쓰는 쪽은 오브젝트 검증(`ObjectValidation`)이고 읽는 쪽은 에디터(맵 검사 · 상태줄 · 인스펙터)다. 배포에서는 아무도 읽지 않아 이 표가 없다.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Object/GameObject/Subsystem/ISceneSubsystem.h"
#include "Engine/Reflection/Dev/ValidationIssueLog.h"

namespace sw
{
    class GameObjectManager;
} // namespace sw

namespace sw
{
    /**
     * @class SceneValidationIssues
     * @brief 월드(씬)마다 하나 있는 검증 결과 표입니다.
     * @details 출처마다의 결과라 월드가 소유한다 — 에디터 미리보기 창 · 씬 둘의 결과가 섞이지 않는다(언리얼 Map Check 의 월드별 메시지 로그).
     *          씬을 비우면(`GameObjectManager::clear`) 표도 비고, 오브젝트 파괴는 `GameObject` 소멸자가 자기 출처를 지운다(`ObjectValidation::removeGameObjectIssues`).
     */
    class SW_API SceneValidationIssues final : public ISceneSubsystem
    {
    public:
        /** @brief 이 서비스의 등록 줄입니다(`SW_SCENE_SUBSYSTEM` 가 정의한다). */
        static const SceneSubsystemRegistration& getSceneSubsystemRegistration();
        static constexpr const utf8*             kSubsystemName = "SceneValidationIssues";
        const utf8*                              getSubsystemName() const override { return kSubsystemName; }

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
