#include "Scene.h"
#include "Core/Object/GameObjectManager.h"
#include "Core/Graphics/Material/Material.h"
#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
	Scene::Scene( const std::string& name )
		: _name{ name }
	// GameObjectManager의 생성자를 public으로 변경해야 합니다.
	// 임시로 기본 생성자를 호출합니다. (수정 필요)
	{
		// TODO: _objectManager = std::make_unique<GameObjectManager>();
		_material = std::make_unique<Material>();
	}

	Scene::~Scene()
	{
	}

	bool Scene::initialize( IRHIDevice* rhiDevice )
	{
		if ( _material && _material->initialize( rhiDevice ) == false )
		{
			SW_LOG_ERROR( "[Scene] Failed to initialize Material" );
			return false;
		}
		return true;
	}

	void Scene::update( float32 deltaTime )
	{
		if ( _objectManager )
		{
			// Scene 소유의 GameObjectManager를 이용한 Object 병렬 틱
			_objectManager->tickParallel( deltaTime );
		}
	}

	void Scene::render( IRHIDevice* rhiDevice )
	{
		if ( rhiDevice && _material )
		{
			rhiDevice->drawTriangle( _material->getDescriptorIndex() );
		}
	}

	Material* Scene::getMaterial() const
	{
		return _material.get();
	}
} // namespace sw
