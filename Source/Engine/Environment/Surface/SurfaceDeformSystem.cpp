#include "pch.h"

#include "Engine/Environment/Surface/SurfaceDeformSystem.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/StringUtil.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Environment/EnvironmentUtil.h"
#include "Engine/Environment/Surface/SurfaceKind.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    namespace
    {
        struct SurfaceDeformSystemInternal
        {
            /** @brief 매니저마다 다른 CPU 텍스처 경로입니다(에디터 미리보기 씬과 게임 씬이 한 캐시를 나눈다). */
            static string makeTexturePath()
            {
                static atomic<uint32> s_serial{ 0 };
                return string{ TextureCache::kCPUTexturePrefix } + "surface/deform" + to_string( s_serial.fetch_add( 1u, std::memory_order_relaxed ) );
            }

            static SurfaceDeformSystem* findSystem( const AutomationWatchQuery& query )
            {
                return query._pManager != nullptr ? &query._pManager->getSubsystem<SurfaceDeformSystem>() : nullptr;
            }

            /** @brief 감시 값 `Surface.DeformDepth` 입니다. 키 "x z"(월드 m) 자리가 꺼진 깊이(m, 솟았으면 음수)입니다. */
            [[nodiscard]] static bool readDepth( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceDeformSystem* pSystem = findSystem( query );
                float32                    arrPoint[2]{};
                if ( pSystem == nullptr || StringUtil::parseExactFloats( query._key, " ", arrPoint, 2 ) == false )
                {
                    outValue.setText( "key must be 'x z' (world meters)" );
                    return false;
                }
                outValue.setNumber( static_cast<float64>( -pSystem->findHeightAt( arrPoint[0], arrPoint[1] ) ) );
                return true;
            }

            /** @brief 감시 값 `Surface.DeformActiveTexels` 입니다. 높이가 0 이 아닌(아직 메워지지 않은) 칸 수입니다. */
            [[nodiscard]] static bool readActiveTexels( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceDeformSystem* pSystem = findSystem( query );
                if ( pSystem == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pSystem->getGrid().countActiveTexels() ) );
                return true;
            }

            /** @brief 감시 값 `Surface.DeformDeepest` 입니다. 창 안에서 가장 깊이 꺼진 깊이(m)입니다. */
            [[nodiscard]] static bool readDeepest( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceDeformSystem* pSystem = findSystem( query );
                if ( pSystem == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pSystem->getGrid().findDeepest() ) );
                return true;
            }

            /** @brief 감시 값 `Surface.DeformDeepestNear` 입니다. 키 "x z r"(월드 m) — (x, z) 둘레 한 변 2r 정사각형 안에서 가장 깊이 꺼진 깊이(m)입니다. */
            [[nodiscard]] static bool readDeepestNear( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceDeformSystem* pSystem = findSystem( query );
                float32                    arrArea[3]{};
                if ( pSystem == nullptr || StringUtil::parseExactFloats( query._key, " ", arrArea, 3 ) == false || arrArea[2] < 0.0f )
                {
                    outValue.setText( "key must be 'x z r' (world meters, r >= 0)" );
                    return false;
                }
                outValue.setNumber( static_cast<float64>( pSystem->getGrid().findDeepestNear( arrArea[0], arrArea[1], arrArea[2] ) ) );
                return true;
            }

            /** @brief 감시 값 `Surface.DeformStamps` 입니다. 이 씬에서 찍은 도장 수(누적)입니다. */
            [[nodiscard]] static bool readStamps( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceDeformSystem* pSystem = findSystem( query );
                if ( pSystem == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pSystem->getStampCount() ) );
                return true;
            }

            /** @brief 감시 값 `Surface.DeformHeld` 입니다. 지금 붙잡힌 도장 수입니다. */
            [[nodiscard]] static bool readHeld( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceDeformSystem* pSystem = findSystem( query );
                if ( pSystem == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pSystem->getHeldCount() ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( surfaceDeformDepth, "Surface.DeformDepth", "Depth (m) the surface deform layer sank at key 'x z' (negative: raised)", &SurfaceDeformSystemInternal::readDepth, false,
                               true );
    SW_AUTOMATION_QUERY_WATCH( surfaceDeformActiveTexels, "Surface.DeformActiveTexels", "Texels of the surface deform layer not yet filled back", &SurfaceDeformSystemInternal::readActiveTexels,
                               false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceDeformDeepest, "Surface.DeformDeepest", "Deepest sink (m) in the surface deform window", &SurfaceDeformSystemInternal::readDeepest, false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceDeformDeepestNear, "Surface.DeformDeepestNear", "Deepest sink (m) within the square of half side r around key 'x z r'",
                               &SurfaceDeformSystemInternal::readDeepestNear, false, true );
    SW_AUTOMATION_QUERY_WATCH( surfaceDeformStamps, "Surface.DeformStamps", "Stamps put on the surface deform layer in this scene (cumulative)", &SurfaceDeformSystemInternal::readStamps, false,
                               false );
    SW_AUTOMATION_QUERY_WATCH( surfaceDeformHeld, "Surface.DeformHeld", "Stamps held on the surface deform layer now", &SurfaceDeformSystemInternal::readHeld, false, false );
} // namespace sw

namespace sw
{
    SW_SCENE_SERVICE( SurfaceDeformSystem, FlushEnd )

    SurfaceDeformSystem::HeldStamp::HeldStamp()
        : _def{}
        , _point{}
        , _yaw{ 0.0f }
        , _age{ 0.0f }
        , _generation{ 0 }
        , _bInUse{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    SurfaceDeformSystem::SurfaceDeformSystem( GameObjectManager& manager )
        : _pManager{ &manager }
        , _grid{}
        , _listHeld{}
        , _listTexelByte{}
        , _texturePath{}
        , _depthRange{ 0.0f }
        , _stampCount{ 0 }
        , _bindingRevision{ 0 }
        , _heldGeneration{ 0 }
    {
    }

    SurfaceDeformSystem::~SurfaceDeformSystem()
    {
        releaseTexture();
    }

    void SurfaceDeformSystem::update( float32 deltaTime )
    {
        if ( _grid.isReady() == false )
            return;
        float2 focus;
        if ( findFocus( _grid.getExtent(), focus ) )
        {
            const float32 halfExtent = _grid.getExtent() * 0.5f;
            const float2  origin     = _grid.getOrigin();
            const float32 limit      = _grid.getExtent() * kRecenterRatio;
            const bool    bAway      = MathUtil::abs( focus._x - ( origin._x + halfExtent ) ) > limit || MathUtil::abs( focus._y - ( origin._y + halfExtent ) ) > limit;
            if ( bAway && _grid.recenter( focus._x, focus._y ) )
                ++_bindingRevision;
        }
        _grid.fill( deltaTime );
        for ( HeldStamp& held : _listHeld )
        {
            if ( held._bInUse == SW_FALSE )
                continue;
            held._age += MathUtil::max( deltaTime, 0.0f );
            _grid.applyStamp( held._def, held._point._x, held._point._z, held._yaw, 0.0f, held._def._spinSpeed * held._age );
        }
        uploadTexture();
    }

    void SurfaceDeformSystem::onSceneCleared()
    {
        releaseTexture();
        _grid = SurfaceDeformGrid{};
        _listHeld.clear();
        _depthRange = 0.0f;
        _stampCount = 0;
        ++_bindingRevision;
    }

    void SurfaceDeformSystem::stamp( const SurfaceStampDef& def, const float3& point, float32 yaw )
    {
        if ( prepareGrid( def, point ) == false )
            return;
        _grid.applyStamp( def, point._x, point._z, yaw, 0.0f, 0.0f );
        ++_stampCount;
    }

    void SurfaceDeformSystem::stampSegment( const SurfaceStampDef& def, const float3& from, const float3& to )
    {
        const float32 deltaX = to._x - from._x;
        const float32 deltaZ = to._z - from._z;
        const float32 length = MathUtil::sqrt( deltaX * deltaX + deltaZ * deltaZ );
        if ( length <= 0.0f )
        {
            stamp( def, to, 0.0f );
            return;
        }
        if ( prepareGrid( def, to ) == false )
            return;
        // 띠의 앞은 from → to 다(요 규칙 YawUtil: atan2( x, z )).
        _grid.applyStamp( def, ( from._x + to._x ) * 0.5f, ( from._z + to._z ) * 0.5f, MathUtil::atan2( deltaX, deltaZ ), length, 0.0f );
        ++_stampCount;
    }

    SurfaceStampHandle SurfaceDeformSystem::holdStamp( const SurfaceStampDef& def, const float3& point, float32 yaw )
    {
        if ( prepareGrid( def, point ) == false )
            return SurfaceStampHandle{};
        uint32 slot = 0;
        while ( slot < _listHeld.size() && _listHeld[slot]._bInUse == SW_TRUE )
        {
            ++slot;
        }
        if ( slot == _listHeld.size() )
            _listHeld.emplace_back();
        HeldStamp& held  = _listHeld[slot];
        held._def        = def;
        held._point      = point;
        held._yaw        = yaw;
        held._age        = 0.0f;
        held._generation = ++_heldGeneration;
        held._bInUse     = SW_TRUE;
        _grid.applyStamp( def, point._x, point._z, yaw, 0.0f, 0.0f );
        ++_stampCount;
        return SurfaceStampHandle{ slot, held._generation };
    }

    bool SurfaceDeformSystem::moveHeldStamp( SurfaceStampHandle handle, const float3& point, float32 yaw )
    {
        HeldStamp* pHeld = findHeld( handle );
        if ( pHeld == nullptr )
            return false;
        pHeld->_point = point;
        pHeld->_yaw   = yaw;
        return true;
    }

    void SurfaceDeformSystem::releaseStamp( SurfaceStampHandle handle )
    {
        HeldStamp* pHeld = findHeld( handle );
        if ( pHeld != nullptr )
            pHeld->_bInUse = SW_FALSE;
    }

    uint32 SurfaceDeformSystem::getHeldCount() const
    {
        uint32 heldCount = 0;
        for ( const HeldStamp& held : _listHeld )
        {
            heldCount += held._bInUse == SW_TRUE ? 1u : 0u;
        }
        return heldCount;
    }

    float4 SurfaceDeformSystem::makeShaderParams() const
    {
        if ( _grid.isReady() == false )
            return float4{ 0.0f, 0.0f, 0.0f, 0.0f };
        const float2 origin = _grid.getOrigin();
        return float4{ origin._x, origin._y, 1.0f / _grid.getExtent(), _depthRange };
    }

    bool SurfaceDeformSystem::prepareGrid( const SurfaceStampDef& def, const float3& point )
    {
        // 변형은 보이는 것만이다 — 그리는 것이 없는 전용 서버는 격자를 두지 않는다.
        if ( build::kWithClientCode == false || def.isSet() == false )
            return false;
        if ( _grid.isReady() )
            return true;
        const shared_ptr<const SurfaceKindTable> pTable = SurfaceKindTable::findDefault();
        if ( pTable == nullptr )
            return false; // 표를 읽지 못한 까닭은 findDefault 가 남겼다
        // 창은 카메라 앞이다. 카메라가 없는 씬(헤드리스 테스트)은 처음 찍은 자리를 창 가운데에 둔다 — 그 뒤로 카메라가 생기면 따라간다.
        float2 focus;
        if ( findFocus( pTable->_deformExtent, focus ) == false )
            focus = float2{ point._x, point._z };
        _grid.reset( pTable->_deformResolution, pTable->_deformExtent, focus._x, focus._y );
        _depthRange = pTable->_deformDepthRange;
        ++_bindingRevision;
        return true;
    }

    bool SurfaceDeformSystem::findFocus( float32 extent, float2& outFocus ) const
    {
        float3 position;
        float3 forward;
        if ( EnvironmentUtil::findView( *_pManager, position, forward ) == false )
            return false;
        // 땅을 내려다보는 카메라는 앞의 수평 성분이 작다 — 수평 방향만 쓰고 길이는 창 너비로 정한다.
        const float32 flatLength = MathUtil::sqrt( forward._x * forward._x + forward._z * forward._z );
        const float32 ahead      = extent * kFocusAheadRatio;
        const float32 scale      = flatLength > 1.0e-4f ? ahead / flatLength : 0.0f;
        outFocus                 = float2{ position._x + forward._x * scale, position._z + forward._z * scale };
        return true;
    }

    SurfaceDeformSystem::HeldStamp* SurfaceDeformSystem::findHeld( SurfaceStampHandle handle )
    {
        if ( handle.isValid() == false || handle._slot >= _listHeld.size() )
            return nullptr;
        HeldStamp& held = _listHeld[handle._slot];
        return held._bInUse == SW_TRUE && held._generation == handle._generation ? &held : nullptr;
    }

    void SurfaceDeformSystem::uploadTexture()
    {
        AssetManager* pAssets = _pManager->findAssetManager();
        if ( pAssets == nullptr || _grid.hasDirtyRect() == false )
            return;
        TextureCache& cache      = pAssets->getAssetCache<TextureCache>();
        const uint32  resolution = _grid.getResolution();
        if ( _texturePath.empty() )
        {
            const SurfaceDeformGrid::TexelRect full{ 0, 0, resolution, resolution };
            _grid.writeTexels( full, _depthRange, _listTexelByte );
            string path = SurfaceDeformSystemInternal::makeTexturePath();
            if ( cache.createCPUTexture( path, resolution, resolution, _listTexelByte ) == false )
            {
                SW_LOG_ERROR_ID( "Surface.DeformTextureFailed", "Surface deform texture '%#' (%#x%#) could not be created", path.c_str(), resolution, resolution );
                return;
            }
            _texturePath = std::move( path );
            ++_bindingRevision;
            _grid.clearDirtyRect();
            return;
        }
        const SurfaceDeformGrid::TexelRect& dirty = _grid.getDirtyRect();
        _grid.writeTexels( dirty, _depthRange, _listTexelByte );
        cache.requestRegionUpdate( _texturePath, dirty._minX, dirty._minZ, dirty.getWidth(), dirty.getHeight(), _listTexelByte );
        _grid.clearDirtyRect();
    }

    void SurfaceDeformSystem::releaseTexture()
    {
        if ( _texturePath.empty() )
            return;
        AssetManager* pAssets = _pManager->findAssetManager();
        if ( pAssets != nullptr )
            pAssets->getAssetCache<TextureCache>().releaseCPUTexture( _texturePath );
        _texturePath.clear();
    }
} // namespace sw
