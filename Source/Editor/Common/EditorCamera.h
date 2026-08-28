/**
 * @file EditorCamera.h
 * @brief 에디터 뷰포트 카메라 탐색·생성. Scene은 게임 카메라만 관리합니다.
 */
#pragma once

namespace sw
{
	class Scene;
	class CameraComponent;
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
		/** @brief CameraRole::Editor 중 우선순위가 가장 높은 활성 카메라를 반환합니다. */
		static CameraComponent* find( const Scene* pScene );
		/**
		 * @brief 에디터 카메라가 없으면 기본 포즈로 생성합니다.
		 * @details 이미 있으면 트랜스폼을 덮어쓰지 않습니다.
		 */
		static CameraComponent* ensure( Scene* pScene );
		/**
		 * @brief Game View에 쓸 카메라를 고릅니다.
		 * @param bPlaying PIE 중이면 게임 카메라, 아니면 에디터 카메라.
		 */
		static CameraComponent* getViewportCamera( Scene* pScene, bool bPlaying );
	};
} // namespace sw::editor
