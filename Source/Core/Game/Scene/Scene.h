#pragma once

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"
#include "Core/Utility/String/hashed_string.h"

namespace sw
{
	class GameObjectManager;
	class IRHIDevice;
	class Material;
	class FrameRenderer;

	/**
	 * @class Scene
	 * @brief 게임 월드의 기본 단위 (Level/World). 고유의 GameObjectManager를 소유합니다.
	 */
	class SW_API Scene
	{
	public:
		explicit Scene( const std::string& name );
		virtual ~Scene();

		/** @brief 씬 초기화 */
		virtual bool initialize( IRHIDevice* rhiDevice );

		/** @brief 매 프레임 호출되어 씬 내부의 객체들을 병렬 업데이트합니다. */
		virtual void update( float32 deltaTime );

		/** @brief 씬을 렌더링합니다. (FrameRenderer 우선, 없으면 drawTriangle) */
		virtual void render( IRHIDevice* rhiDevice );

		/** @brief Optional FrameRenderer owned by App (non-owning). */
		void setFrameRenderer( FrameRenderer* frameRenderer ) { _frameRenderer = frameRenderer; }
		FrameRenderer* getFrameRenderer() const { return _frameRenderer; }

		/** @brief 씬의 이름 반환 */
		const std::string& getName() const { return _name; }

		/** @brief 씬이 소유한 GameObjectManager 반환 */
		GameObjectManager* getObjectManager() const { return _objectManager.get(); }

		// 임시 머티리얼 인터페이스
		Material* getMaterial() const;

	private:
		std::string						   _name;
		std::unique_ptr<GameObjectManager> _objectManager;
		std::unique_ptr<Material>		   _material;
		FrameRenderer*					   _frameRenderer = nullptr;
	};
} // namespace sw

