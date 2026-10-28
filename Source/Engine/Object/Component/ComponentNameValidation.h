/**
 * @file ComponentNameValidation.h
 * @brief 컴포넌트가 이름으로 가리키는 설정 표(물리 레이어 · 재질, 정렬 레이어, 내비 영역)를 검증 함수(`PROPERTY( Validate = fn )`)에서 대 봅니다.
 * @details 런타임은 모르는 이름을 기본값으로 대신하고 로그만 남긴다(레이어 0 · 첫 재질 · `Default` 정렬 레이어 · 평지 영역). 검증 함수는 로드 · 저장 ·
 *          인스펙터 편집 뒤에 불려 생성자 기본값까지 본다 — 데이터에 적힌 값만 보는 에셋 검증 규칙 `component_reference` 가 못 보는 자리다.
 *          언리얼 `UObject::IsDataValid` 에 해당한다. 빈 이름은 "기본" 이라는 뜻이라 보지 않는다.
 *
 *          구현은 표를 가진 기능 폴더에 나눠 둔다(`Physics/` · `2D/` · `Navigation/` 의 `ComponentNameValidation*.cpp`) — 코어는 그 폴더를 include 하지 않는다.
 *          표의 출처는 런타임과 같다. 물리는 초기화된 `PhysicsSystem` 의 설정, 없으면(도구 · 서비스 없는 테스트) `PhysicsSystem::kSettingsPath` 를 한 번 읽은 것,
 *          정렬 레이어는 서비스 표의 `engine::getSubsystem<Render2DSettings>()`(서비스가 없으면 비어 있지 않은 이름을 검증하지 못하고 멈춘다), 내비 영역은 `EngineDefaultAssets::_navMeshSettings` 를 한 번 읽은 것(`SceneNavigation::getSettings` 와 같은 파일)이다.
 *          주의: 한 번 읽은 파일 표는 프로세스가 끝날 때까지 그대로다 — 설정 파일을 고친 뒤의 검증은 엔진을 다시 띄워야 새 표를 본다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class ValidationContext;
} // namespace sw

namespace sw
{
    /**
     * @struct ComponentNameValidation
     * @brief 이름 참조 검증의 공통 몸통입니다. 컴포넌트의 검증 함수가 칸 이름과 값을 넘깁니다.
     */
    struct SW_API ComponentNameValidation
    {
        /** @brief 물리 충돌 레이어 이름(`physicssettings.xml` 의 `PhysicsLayerDef`)인지 봅니다. */
        static void validatePhysicsLayer( ValidationContext& context, const utf8* pFieldName, const hashed_string& name );
        /** @brief 물리 재질 이름(`PhysicsMaterialDef`)인지 봅니다. */
        static void validatePhysicsMaterial( ValidationContext& context, const utf8* pFieldName, const hashed_string& name );
        /** @brief 2D 정렬 레이어 이름(`render2d.xml` 의 `<Layer>`)인지 봅니다. */
        static void validateSortingLayer( ValidationContext& context, const utf8* pFieldName, const hashed_string& name );
        /** @brief 내비게이션 영역 이름(`navmeshsettings.xml` 의 `NavAreaDef`)인지 봅니다. */
        static void validateNavArea( ValidationContext& context, const utf8* pFieldName, const hashed_string& name );
    };
} // namespace sw
