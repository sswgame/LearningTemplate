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
                const uint64 floatCount          = static_cast<uint64>( vertexCount ) * frameCount * 3;
                const bool   bInvalidCacheHeader = vertexCount == 0 || frameCount == 0 || MathUtil::isFinite( fps ) == false || fps <= 0.0f ||
                                                 bytes.size() != kHeaderSize + floatCount * sizeof( float32 );
                if ( bInvalidCacheHeader )
                    return false;
                outData._listFramePosition.resize( static_cast<size_t>( vertexCount ) * frameCount );
                std::memcpy( outData._listFramePosition.data(), bytes.data() + kHeaderSize, static_cast<size_t>( floatCount * sizeof( float32 ) ) );
                for ( const float3& value : outData._listFramePosition )
                {
                    const bool bNonFinitePosition = MathUtil::isFinite( value._x ) == false || MathUtil::isFinite( value._y ) == false || MathUtil::isFinite( value._z ) == false;
                    if ( bNonFinitePosition )
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
            vector<float3> listTarget;
            if ( computeTargets( context, static_cast<uint32>( inoutMesh.getVertexCount() ), listTarget ) == false )
                return;
            for ( size_t index = 0; index < listTarget.size(); ++index )
            {
                const float32 weight = MathUtil::saturate( inoutMesh.getWeight( index ) );
                if ( weight <= 0.0f )
                    continue;
                inoutMesh._listPosition[index] += ( listTarget[index] - inoutMesh._listPosition[index] ) * weight;
            }
        }

        bool supportsGPU() const override { return true; }

        /**
         * @brief 이 시각의 두 프레임을 보간해 모디파이어 공간으로 옮긴 정점별 목표 위치를 싣는다: float4(얻음) 뒤에 정점마다 float4 하나(xyz = 목표).
         * @details 캐시 전체(프레임 × 정점)를 올리지 않는다 - 프로그램은 시간이 바뀔 때마다 다시 만들어지므로(머리의 시간) 지금 시각에 필요한 한 프레임분만 실으면
         *          한 번 올리는 양이 정점 수에 그친다. 셰이더는 정점 번호로 읽기만 한다. 정점 수가 안 맞으면 CPU 처럼 경고 뒤 항등이다.
         */
        bool packGPUInputs( const MeshModifierContext& context, vector<float4>& outListValue ) const override
        {
            vector<float3> listTarget;
            const bool     bGot = computeTargets( context, context._vertexCount, listTarget );
            outListValue.push_back( float4{ bGot ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f } );
            for ( const float3& target : listTarget )
            {
                outListValue.push_back( float4{ target._x, target._y, target._z, 0.0f } );
            }
            return true;
        }

        void onParamsChanged() override
        {
            if ( _cache._path != getText( _fileParam ) )
                _bCacheTried = false;
        }

    private:
        /** @brief 이 시각의 보간 프레임을 모디파이어 공간으로 옮겨 정점마다 하나씩 @p outListTarget 에 채웁니다. 캐시를 못 읽었거나 정점 수가 다르면 false 입니다. */
        bool computeTargets( const MeshModifierContext& context, uint32 vertexCount, vector<float3>& outListTarget ) const
        {
            outListTarget.clear();
            const string& path = getText( _fileParam );
            if ( path.empty() || context._pEvaluation == nullptr )
                return false;
            const PointCacheData* pCache = findCache( path );
            if ( pCache == nullptr )
                return false;
            if ( pCache->_vertexCount != vertexCount )
            {
                SW_LOG_WARNING( "Point cache '%#' has %# vertices but the mesh has %# - skipped", path.c_str(), pCache->_vertexCount, vertexCount );
                return false;
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

            // 캐시는 메시 로컬 - 로컬 → 월드 → 모디파이어 공간(행 벡터 규약)
            const float4x4 localToModifier = context._pEvaluation->_meshToWorld * context._worldToModifier;
            const float3*  pFrameA         = pCache->_listFramePosition.data() + static_cast<size_t>( frameA ) * vertexCount;
            const float3*  pFrameB         = pCache->_listFramePosition.data() + static_cast<size_t>( frameB ) * vertexCount;
            outListTarget.reserve( vertexCount );
            for ( uint32 index = 0; index < vertexCount; ++index )
            {
                outListTarget.push_back( float3::transform( pFrameA[index] + ( pFrameB[index] - pFrameA[index] ) * ratio, localToModifier ) );
            }
            return true;
        }

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
