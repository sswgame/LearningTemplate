/**
 * @file EditorCamera.h
 * @brief 에디터 씬 뷰 카메라를 찾거나 만듭니다. Scene 은 게임 카메라만 관리합니다(게임 뷰는 그 게임 카메라를 그린다).
 */
#pragma once
#include "Core/Math/VectorMath.h"

namespace sw
{
    class CameraComponent;
    class Scene;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorCamera
     * @brief 활성 씬에서 Editor 역할 카메라를 찾거나 없으면 만듭니다.
     */
    class EditorCamera
    {
    public:
        /** @brief CameraRole::Viewport 중 우선순위가 가장 높은 활성 카메라를 반환합니다. */
        static CameraComponent* find( const Scene* pScene );
        /**
         * @brief 에디터 카메라가 없으면 기본 포즈로 생성합니다.
         * @details 이미 있으면 트랜스폼을 덮어쓰지 않습니다.
         */
        static CameraComponent* ensure( Scene* pScene );
        /**
         * @brief `ensure` 가 새로 만든 카메라의 위치입니다. 씬 뷰는 처음 쓰는 카메라가 이 위치가 아니면 이미 놓인 카메라로 보고 그 포즈를 이어받습니다
         *        (에디터 모듈을 다시 로드하는 백엔드 교체 · 핫 리로드는 씬과 함께 카메라 오브젝트를 넘긴다).
         */
        static float3 getSpawnPosition() { return float3( 2.15f, 1.55f, 2.65f ); }
    };
} // namespace sw::editor
