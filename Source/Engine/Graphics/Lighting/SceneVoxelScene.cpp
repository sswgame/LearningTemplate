#include "pch.h"

#include "Engine/Graphics/Lighting/SceneVoxelScene.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/PackUtil.h"

#include "Engine/Graphics/Mesh/Mesh.h"

namespace sw
{
    bool SceneVoxelSource::isSamePaint( const SceneVoxelSource& other ) const
    {
        return _mesh == other._mesh && _contentID == other._contentID && _world == other._world && _firstIndex == other._firstIndex && _indexCount == other._indexCount &&
               _packedEmission == other._packedEmission;
    }

    uint32 SceneVoxelScene::findEmissionRank( uint32 packedEmission ) const
    {
        for ( uint32 index = 0; index < static_cast<uint32>( _listEmissionPalette.size() ); ++index )
        {
            if ( _listEmissionPalette[index] == packedEmission )
                return index + 1u;
        }
        return 0;
    }

    const vector<SceneVoxelSource>& SceneVoxelScene::getStaticSources() const
    {
        static const vector<SceneVoxelSource> s_listEmpty;
        return _pListStaticSource != nullptr ? *_pListStaticSource : s_listEmpty;
    }

    void SceneVoxelScene::setStaticSources( vector<SceneVoxelSource>&& listSource )
    {
        _pListStaticSource = make_shared<const vector<SceneVoxelSource>>( std::move( listSource ) );
    }

    void SceneVoxelScene::rebuildEmissionPalette()
    {
        _listEmissionPalette.clear();
        const auto addEmission = [this]( const SceneVoxelSource& source )
        {
            if ( source.isPainted() && source._packedEmission != 0 )
                _listEmissionPalette.push_back( source._packedEmission );
        };
        for ( const SceneVoxelSource& source : getStaticSources() )
        {
            addEmission( source );
        }
        for ( const SceneVoxelMovingSource& moving : _listMovingSource )
        {
            addEmission( moving._source );
        }
        // 밝기(푼 세 채널의 합) 차례, 같은 밝기는 비트 차례 — 실행마다 같다. 중복은 하나로 합친다.
        const auto computeBrightness = []( uint32 packed )
        {
            const float3 color = PackUtil::unpackRGB9E5( packed );
            return color._x + color._y + color._z;
        };
        VectorUtil::sortUnique( _listEmissionPalette, [&computeBrightness]( uint32 lhs, uint32 rhs )
        {
            const float32 lhsBrightness = computeBrightness( lhs );
            const float32 rhsBrightness = computeBrightness( rhs );
            return lhsBrightness != rhsBrightness ? lhsBrightness < rhsBrightness : lhs < rhs;
        } );
    }
} // namespace sw
