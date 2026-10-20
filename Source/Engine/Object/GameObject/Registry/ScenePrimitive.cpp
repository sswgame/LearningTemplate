#include "pch.h"

#include "Engine/Object/GameObject/Registry/ScenePrimitive.h"

#include "Engine/Object/Component/SceneTransformStorage.h"
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
