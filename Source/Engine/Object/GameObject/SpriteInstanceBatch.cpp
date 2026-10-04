#include "pch.h"

#include "Engine/Object/GameObject/SpriteInstanceBatch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/Shader/Binding/GpuSpriteInstanceData.h"
#include "Engine/Object/Component/2D/SpriteRenderUtil.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/MeshInstanceBatch.h"
#include "Engine/Object/GameObject/PrimitiveRegistry.h"
#include "Engine/Resource/AssetManager.h"

namespace sw
{
    SW_LOG_CALLER( "SpriteInstanceBatch" );

    SpriteInstanceBatch::SpriteInstanceBatch()
        : _batch{}
        , _acquiredMaterialPath{}
    {
    }

    SpriteInstanceBatch::~SpriteInstanceBatch()
    {
        shutdown();
    }

    bool SpriteInstanceBatch::initialize( GameObjectManager& manager, string_view texturePath, uint32 count )
    {
        shutdown();
        if ( count == 0 )
            return false;

        Material* pMaterial = nullptr;
        if ( engine::areEngineServicesBound() )
        {
            const hashed_string materialPath = SpriteRenderUtil::getSpriteMaterialPath();
            MaterialCache&      cache        = engine::getAssetManager().getMaterialManager();
            pMaterial                        = cache.acquire( materialPath.c_str(), nullptr );
            if ( pMaterial != nullptr )
            {
                cache.requestInitialize( materialPath.c_str() );
                _acquiredMaterialPath = materialPath;
            }
            else
            {
                SW_LOG_WARNING( "Sprite material '%#' could not be acquired - the sprites use the scene default material", materialPath.c_str() );
            }
        }

        shared_ptr<MaterialInstance> instance;
        if ( pMaterial != nullptr && texturePath.empty() == false )
            instance = SpriteRenderUtil::acquireTextureInstance( pMaterial, hashed_string( string{ texturePath }.c_str() ) );

        // 스프라이트 사각형은 공유 프리미티브다 — 같은 메시라야 스프라이트 컴포넌트와 한 배치로 묶인다.
        _batch                                  = sw::make_unique<MeshInstanceBatch>( MeshUtil::acquirePrimitive( "Sprite" ), pMaterial, std::move( instance ), count );
        constexpr float32 kUnitQuadHalfDiagonal = 0.70710678f;
        for ( uint32 index = 0; index < count; ++index )
        {
            _batch->setBoundsRadius( index, kUnitQuadHalfDiagonal );
            _batch->setEntryVisible( index, false );
        }
        manager.getPrimitiveRegistry().addInstanceBatch( _batch.get() );
        return true;
    }

    void SpriteInstanceBatch::shutdown()
    {
        // 배치의 소멸자가 등록부에서 뺀다. 머티리얼은 배치가 놓인 **뒤에** 놓는다 — 놓는 순간 캐시가 지울 수 있다.
        _batch.reset();
        if ( _acquiredMaterialPath.empty() )
            return;
        if ( engine::areEngineServicesBound() )
            engine::getAssetManager().getMaterialManager().release( _acquiredMaterialPath.c_str() );
        _acquiredMaterialPath = hashed_string{};
    }

    uint32 SpriteInstanceBatch::getCount() const
    {
        return ( _batch != nullptr ) ? _batch->getCount() : 0u;
    }

    void SpriteInstanceBatch::setEntry( uint32 index, const float4x4& world, const float4& uvRect, const float4& tint )
    {
        if ( _batch == nullptr || index >= _batch->getCount() )
            return;
        // 그대로인 자리는 더티로 찍지 않는다 — 서 있는 캐릭터의 HP 바가 프레임마다 다시 모이지 않게(행렬은 비트로 견준다).
        if ( Memory::compare( &_batch->getEntry( index )._world, &world, sizeof( world ) ) != 0 )
            _batch->setWorld( index, world );
        _batch->setSprite( index, GpuSpriteInstanceData::make( uvRect, tint ) );
        _batch->setEntryVisible( index, true );
    }

    void SpriteInstanceBatch::setEntryVisible( uint32 index, bool bVisible )
    {
        if ( _batch != nullptr )
            _batch->setEntryVisible( index, bVisible );
    }

    bool SpriteInstanceBatch::isEntryVisible( uint32 index ) const
    {
        return _batch != nullptr && _batch->isEntryVisible( index );
    }

    void SpriteInstanceBatch::setVisible( bool bVisible )
    {
        if ( _batch != nullptr )
            _batch->setVisible( bVisible );
    }

    float4x4 SpriteInstanceBatch::makeQuadWorld( const float3& center, float32 width, float32 height )
    {
        return float4x4::createScale( width, height, 1.0f ) * float4x4::createTranslation( center );
    }
} // namespace sw
