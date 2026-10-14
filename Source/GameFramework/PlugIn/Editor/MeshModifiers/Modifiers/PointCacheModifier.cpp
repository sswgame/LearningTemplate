/**
 * @file PointCacheModifier.cpp
 * @brief 정점 위치 프레임 시퀀스 파일(포인트 캐시)을 시간에 따라 재생해 정점 위치를 바꾸는 모디파이어입니다.
 * @details 파일 형식(리틀 엔디언, 단순 바이너리): 헤더 `정점 수 u32 · 프레임 수 u32 · fps f32` 다음에 프레임마다 float3 × 정점 수.
 *          캐시 위치는 메시 로컬 공간이다 — 모디파이어 공간으로 옮겨 넣는다(기즈모를 옮겨도 캐시 모양은 그대로).
 *          재생 위치 = (시간 × `Speed` + `Time Offset`) × fps 프레임, 두 프레임 사이는 선형 보간. `Loop` 면 감고, 아니면 끝 프레임에 멈춘다.
 *          경로가 비면 항등이다. 읽기 실패 · 형식 틀림 · 정점 수 불일치면 경고하고 항등이다. 법선은 건드리지 않는다(스택의 법선 재계산을 쓴다).
 *          경로는 파일 경로로 그대로 연다(리소스 id 를 절대 경로로 푸는 일은 엔진 서비스가 필요해 확장 모듈에서 하지 않는다).
 *          읽은 파일은 경로가 같은 동안 모디파이어 안에 붙잡아 둔다 — 스택은 게임 스레드 한 곳에서만 건다.
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

#include <cstring>

namespace sw::editor
{
    namespace
    {
        /** @brief 읽어 둔 포인트 캐시 파일입니다. */
        struct PointCacheData
        {
            string         _path;
            vector<float3> _listFramePosition; ///< 프레임 순서로 정점 수씩
            uint32         _vertexCount{ 0 };
            uint32         _frameCount{ 0 };
            float32        _fps{ 0.0f };
            bool           _bValid{ false };
        };

        struct PointCacheModifierInternal
        {
            static constexpr size_t kHeaderSize = 12;

            // 파일 확장자는 .mfpc(mesh-modifier point cache)다. 이 모디파이어가 정한 형식이며, 에셋 슬롯 필터도 이 확장자만 받는다.
            [[nodiscard]] static bool load( string_view path, PointCacheData& outData )
            {
                outData       = PointCacheData{};
                outData._path = string( path );
                vector<uint8> bytes;
                if ( FileUtil::readFile( path, bytes ) == false || bytes.size() < kHeaderSize )
                    return false;
                uint32  vertexCount = 0;
                uint32  frameCount  = 0;
                float32 fps         = 0.0f;
                std::memcpy( &vertexCount, bytes.data(), 4 );
                std::memcpy( &frameCount, bytes.data() + 4, 4 );
                std::memcpy( &fps, bytes.data() + 8, 4 );
                const uint64 floatCount = static_cast<uint64>( vertexCount ) * frameCount * 3;
                if ( vertexCount == 0 || frameCount == 0 || MathUtil::isFinite( fps ) == false || fps <= 0.0f ||
                     bytes.size() != kHeaderSize + floatCount * sizeof( float32 ) )
                    return false;
                outData._listFramePosition.resize( static_cast<size_t>( vertexCount ) * frameCount );
                std::memcpy( outData._listFramePosition.data(), bytes.data() + kHeaderSize, static_cast<size_t>( floatCount * sizeof( float32 ) ) );
                for ( const float3& value : outData._listFramePosition )
                {
                    if ( MathUtil::isFinite( value._x ) == false || MathUtil::isFinite( value._y ) == false || MathUtil::isFinite( value._z ) == false )
                        return false;
                }
                outData._vertexCount = vertexCount;
                outData._frameCount  = frameCount;
                outData._fps         = fps;
                outData._bValid      = true;
                return true;
            }
        };
    } // namespace

    /** @brief 포인트 캐시를 재생합니다. */
    class PointCacheModifier final : public MeshModifier
    {
    public:
        PointCacheModifier()
            : MeshModifier()
            , _cache{
        }
            , _bCacheTried{ false }
            , _fileParam{ addParam( MeshModifierParamDef{ "Cache File", MeshModifierParamKind::AssetPath, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f }, 1, "Point cache file (u32 vertices, u32 frames, f32 fps, float3 frames)" }.withLayout( "Cache", "Binary point cache file with per-frame vertex positions in mesh space" )
                                        // 캐시 파일 확장자는 이 모디파이어가 정한 .mfpc(mesh-modifier point cache)다 — load() 위 주석과 같다.
                                        .withAssetFilter( ".mfpc" ) ) },
            _speedParam{ addParam( MeshModifierParamDef{ "Speed", MeshModifierParamKind::Float, -10.0f, 10.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Playback", "Playback speed multiplier (negative plays backward)", "x" ) ) },
            _timeOffsetParam{ addParam( MeshModifierParamDef{ "Time Offset", MeshModifierParamKind::Float, -1000.0f, 1000.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Playback", "Time added before converting to a cache frame", "s" ) ) },
            _loopParam{ addParam( MeshModifierParamDef{ "Loop", MeshModifierParamKind::Bool, 0.0f, 1.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Playback", "Wrap around at the end instead of holding the last frame" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Plays back a point cache file over time, replacing vertex positions with interpolated cache frames."; }

        void applyToMesh( MeshModifierMesh& inoutMesh, const MeshModifierContext& context ) const override
        {
            const string& path = getText( _fileParam );
            if ( path.empty() || context._pEvaluation == nullptr )
                return;
            const PointCacheData* pCache = findCache( path );
            if ( pCache == nullptr )
                return;
            if ( pCache->_vertexCount != inoutMesh.getVertexCount() )
            {
                SW_LOG_WARNING( "Point cache '%#' has %# vertices but the mesh has %# - skipped", path.c_str(), pCache->_vertexCount,
                                static_cast<uint32>( inoutMesh.getVertexCount() ) );
                return;
            }

            // 프레임 위치를 정한다
            const float32 lastFrame = static_cast<float32>( pCache->_frameCount - 1 );
            float32       frame     = ( context.getTime() * getFloat( _speedParam ) + getFloat( _timeOffsetParam ) ) * pCache->_fps;
            if ( MathUtil::isFinite( frame ) == false )
                frame = 0.0f;
            if ( getBool( _loopParam ) && pCache->_frameCount > 1 )
            {
                const float32 period = static_cast<float32>( pCache->_frameCount );
                frame                = MathUtil::fmod( frame, period );
                if ( frame < 0.0f )
                    frame += period;
            }
            else
            {
                frame = MathUtil::clamp( frame, 0.0f, lastFrame );
            }
            const uint32  frameA = MathUtil::min( static_cast<uint32>( MathUtil::floor( frame ) ), pCache->_frameCount - 1 );
            const uint32  frameB = ( frameA + 1 ) % pCache->_frameCount; // 감을 때 마지막 → 처음
            const float32 ratio  = pCache->_frameCount > 1 ? MathUtil::saturate( frame - static_cast<float32>( frameA ) ) : 0.0f;

            // 캐시는 메시 로컬 — 로컬 → 월드 → 모디파이어 공간(행 벡터 규약)
            const float4x4 localToModifier = context._pEvaluation->_meshToWorld * context._worldToModifier;
            const size_t   vertexCount     = inoutMesh.getVertexCount();
            const float3*  pFrameA         = pCache->_listFramePosition.data() + static_cast<size_t>( frameA ) * vertexCount;
            const float3*  pFrameB         = pCache->_listFramePosition.data() + static_cast<size_t>( frameB ) * vertexCount;
            for ( size_t index = 0; index < vertexCount; ++index )
            {
                const float32 weight = MathUtil::saturate( inoutMesh.getWeight( index ) );
                if ( weight <= 0.0f )
                    continue;
                const float3 local  = pFrameA[index] + ( pFrameB[index] - pFrameA[index] ) * ratio;
                const float3 target = float3::transform( local, localToModifier );
                inoutMesh._listPosition[index] += ( target - inoutMesh._listPosition[index] ) * weight;
            }
        }

        void onParamsChanged() override
        {
            if ( _cache._path != getText( _fileParam ) )
                _bCacheTried = false;
        }

    private:
        /** @brief 경로의 캐시를 (필요하면 읽어) 냅니다. 못 읽으면 경고 한 번 뒤 nullptr 입니다. */
        const PointCacheData* findCache( const string& path ) const
        {
            if ( _bCacheTried == false || _cache._path != path )
            {
                _bCacheTried = true;
                if ( PointCacheModifierInternal::load( path, _cache ) == false )
                    SW_LOG_WARNING( "Point cache '%#' could not be read or is malformed - skipped", path.c_str() );
            }
            return _cache._bValid ? &_cache : nullptr;
        }

        mutable PointCacheData _cache;
        mutable bool           _bCacheTried;
        uint32                 _fileParam;
        uint32                 _speedParam;
        uint32                 _timeOffsetParam;
        uint32                 _loopParam;
    };

    SW_MESH_MODIFIER( PointCacheModifier, "pointcache", "Point Cache", "Data" );
} // namespace sw::editor
