#include "pch.h"

#include "Engine/Object/GameObject/Registry/ScenePrimitive.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneTransformStorage.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"

namespace sw
{
    ScenePrimitive::ScenePrimitive()
        : _primitiveIndex{ kInvalidPrimitiveIndex }
    {
    }

    void ScenePrimitive::setPrimitiveIndex( uint32 index )
    {
        _primitiveIndex = index;
        // 틱 뒤 적용 · 플러시는 이 번호로 레지스트리에 바로 찍는다(컴포넌트를 거치지 않는다).
        setTransformPrimitiveIndex( index == kInvalidPrimitiveIndex ? SceneTransformStorage::kNoPrimitive : index );
    }

    bool ScenePrimitive::computeObjectRenderBounds( const GameObject& object, float3& outCenter, float32& outRadius )
    {
        bool bFound = false;
        object.forEachComponentOfType<ScenePrimitive>( [&]( const ScenePrimitive* pPrimitive )
        {
            float3  center{};
            float32 radius = 0.0f;
            if ( pPrimitive->isActive() == false || pPrimitive->getWorldBounds( center, radius ) == false )
                return;
            if ( bFound == false )
            {
                outCenter = center;
                outRadius = radius;
                bFound    = true;
                return;
            }
            // 두 구를 덮는 가장 작은 구 — 한쪽이 다른 쪽을 품으면 큰 쪽 그대로다.
            const float3  offset   = center - outCenter;
            const float32 distance = offset.getLength();
            if ( distance + radius <= outRadius )
                return;
            if ( distance + outRadius <= radius )
            {
                outCenter = center;
                outRadius = radius;
                return;
            }
            const float32 merged = ( distance + outRadius + radius ) * 0.5f;
            outCenter            = outCenter + offset * ( ( merged - outRadius ) / distance );
            outRadius            = merged;
        } );
        return bFound;
    }

    ScenePrimitiveBatch::ScenePrimitiveBatch( uint32 entryCount )
        : _pRegistry{ nullptr }
        , _firstEntry{ 0 }
        , _entryCount{ entryCount }
    {
    }

    ScenePrimitiveBatch::~ScenePrimitiveBatch()
    {
        unregisterBatch();
    }

    void ScenePrimitiveBatch::unregisterBatch()
    {
        if ( _pRegistry != nullptr )
            _pRegistry->removeInstanceBatch( this );
    }

    void ScenePrimitiveBatch::markEntryDirty( uint32 index )
    {
        if ( _pRegistry != nullptr )
            _pRegistry->markInstanceDirty( this, index );
    }
} // namespace sw
