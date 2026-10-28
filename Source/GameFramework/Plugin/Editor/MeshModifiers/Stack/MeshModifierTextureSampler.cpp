/**
 * @file MeshModifierTextureSampler.cpp
 * @brief 이미지 파일 CPU 디코드 · 경로 키 캐시 · 반복 래핑 이중선형 표본을 구현합니다.
 * @details 디코드는 Editor 의 `ImageUtil`(stb_image 를 감싼다)에 맡깁니다.
 */
#include "pch.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierTextureSampler.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Asset/Texture/ImageUtil.h"

#include "Engine/FileSystem/AssetPaths.h"

namespace sw::editor
{
    SW_LOG_CALLER( "MeshModifierTextureSampler" );

    namespace
    {
        /** @brief 디코드한 이미지 한 장입니다. 픽셀은 만든 뒤 바뀌지 않고, 다시 읽으면 새 항목으로 바꿔 끼웁니다. */
        struct DecodedTexture
        {
            string         _resolvedPath;               ///< 실제로 읽은 파일 경로(못 찾았으면 빈 문자열)
            int64          _writeTicks = 0;             ///< 읽을 때의 파일 쓰기 시각(FileUtil 눈금)
            int32          _width      = 0;             ///< 가로 텍셀 수
            int32          _height     = 0;             ///< 세로 텍셀 수
            vector<float4> _listTexel;                  ///< 행 우선, 첫 행이 v = 0 쪽입니다
            atomic<int64>  _lastCheckMilliseconds{ 0 }; ///< 마지막으로 파일 시각을 본 단조 시계(ms)
            atomic<int64>  _lastUseMilliseconds{ 0 };   ///< 마지막으로 표본한 단조 시계(ms) - 캐시 상한을 넘으면 오래된 것부터 내린다

            bool   isValid() const { return _width > 0 && _height > 0 && _listTexel.empty() == false; }
            size_t getByteSize() const { return _listTexel.size() * sizeof( float4 ); }
        };

        struct MeshModifierTextureSamplerInternal
        {
            /** @brief 파일 시각을 다시 보는 최소 간격입니다. 정점마다 파일 시스템을 묻지 않으려는 것입니다. */
            static constexpr int64 kRecheckIntervalMilliseconds = 1000;
            /** @brief 디코드한 텍셀 캐시의 상한(바이트)입니다. 넘으면 가장 오래 안 쓴 이미지부터 내린다(방금 읽은 것은 남긴다). 4096² 두 장이 들어간다. */
            static constexpr size_t kMaxCacheBytes = constant::kBytesPerGigabyte / 2;

            /** @brief 경로 키로 디코드한 이미지를 담는 캐시입니다. 표본기 인스턴스가 잠금과 함께 소유한다(`MeshModifierTextureSampler::State`). */
            using TextureCache = unordered_map<string, std::shared_ptr<DecodedTexture>>;

            static int64 getNowMilliseconds() { return MonotonicClock::nowMicroseconds() / constant::kMicrosecondsPerMillisecond; }

            /** @brief 절대 경로 · 작업 폴더 상대 경로가 있으면 그대로, 아니면 리소스 id 로 해석합니다. 못 찾으면 빈 문자열입니다. */
            static string resolvePath( string_view assetPath )
            {
                string resolved;
                if ( FileUtil::exists( assetPath ) )
                    resolved = string( assetPath );
                else
                    resolved = AssetPaths::findLooseFile( assetPath ).getDiskPath();
                return resolved;
            }

            /** @brief 파일을 읽어 float4 텍셀로 디코드합니다. 실패해도 경로 · 시각이 든 (무효) 항목을 돌려줘 같은 실패를 정점마다 되풀이하지 않습니다. */
            static std::shared_ptr<DecodedTexture> decodeFile( string_view assetPath )
            {
                std::shared_ptr<DecodedTexture> pTexture = std::make_shared<DecodedTexture>();
                pTexture->_resolvedPath                  = resolvePath( assetPath );
                pTexture->_lastCheckMilliseconds.store( getNowMilliseconds(), std::memory_order_relaxed );

                vector<uint8> bytes;
                const bool    bRead = pTexture->_resolvedPath.empty() == false && FileUtil::getFileWriteTime( pTexture->_resolvedPath, pTexture->_writeTicks ) && FileUtil::readFile( pTexture->_resolvedPath, bytes ) && bytes.empty() == false && bytes.size() <= static_cast<size_t>( MathUtil::kMaxInt32 );
                if ( bRead == false )
                {
                    SW_LOG_WARNING( "Mesh modifier texture '%#' could not be read", string( assetPath ).c_str() );
                }
                else
                {
                    if ( ImageUtil::loadFloat4FromMemory( bytes.data(), bytes.size(), pTexture->_listTexel, pTexture->_width, pTexture->_height ) == false )
                        SW_LOG_WARNING( "Mesh modifier texture '%#' could not be decoded", string( assetPath ).c_str() );
                }
                return pTexture;
            }

            /** @brief 간격이 지났으면 파일 시각을 다시 보고, 바뀌었으면 true(다시 읽어야 함)입니다. 한 스레드만 검사를 맡습니다. */
            static bool isStale( DecodedTexture& texture )
            {
                bool        bStale    = false;
                const int64 now       = getNowMilliseconds();
                int64       lastCheck = texture._lastCheckMilliseconds.load( std::memory_order_relaxed );
                if ( now - lastCheck >= kRecheckIntervalMilliseconds && texture._lastCheckMilliseconds.compare_exchange_strong( lastCheck, now, std::memory_order_relaxed ) )
                {
                    int64 writeTicks = 0;
                    if ( texture._resolvedPath.empty() )
                        bStale = true; // 못 찾았던 파일이 생겼을 수 있다
                    else if ( FileUtil::getFileWriteTime( texture._resolvedPath, writeTicks ) == false )
                        bStale = true;
                    else
                        bStale = writeTicks != texture._writeTicks;
                }
                return bStale;
            }

            /** @brief 캐시에서 찾고, 없거나 낡았으면 단일 잠금 아래에서 한 번만 디코드합니다. */
            static std::shared_ptr<DecodedTexture> acquire( shared_mutex& mutex, TextureCache& cache, string_view assetPath )
            {
                std::shared_ptr<DecodedTexture> pTexture;
                {
                    ScopedSharedLock<shared_mutex> lock{ mutex };
                    auto                           iter = cache.find( assetPath );
                    if ( iter != cache.end() )
                        pTexture = iter->second;
                }

                bool bReload = pTexture == nullptr;
                if ( bReload == false && isStale( *pTexture ) )
                    bReload = true;

                if ( bReload )
                {
                    UniqueLock<shared_mutex> lock{ mutex };
                    auto                     iter = cache.find( assetPath );
                    // 다른 스레드가 잠금을 먼저 잡아 새로 읽었으면 그것을 쓴다.
                    if ( iter != cache.end() && iter->second != pTexture )
                    {
                        pTexture = iter->second;
                    }
                    else
                    {
                        pTexture                   = decodeFile( assetPath );
                        cache[string( assetPath )] = pTexture;
                        evictOverBudget( cache, *pTexture );
                    }
                }
                touch( *pTexture );
                return pTexture;
            }

            /** @brief 마지막 사용 시각을 적습니다. 같은 밀리초 안에서는 쓰지 않아 여러 작업 스레드가 한 줄을 두고 다투지 않습니다. */
            static void touch( DecodedTexture& texture )
            {
                const int64 now = getNowMilliseconds();
                if ( texture._lastUseMilliseconds.load( std::memory_order_relaxed ) != now )
                    texture._lastUseMilliseconds.store( now, std::memory_order_relaxed );
            }

            /**
             * @brief 캐시 합이 상한을 넘으면 최근 1 간격(`kRecheckIntervalMilliseconds`) 동안 안 쓴 이미지를 오래된 것부터 내립니다. 쓰기 잠금 안에서 부릅니다.
             * @details 지금 적용이 번갈아 쓰는 이미지는 내리지 않는다 — 내리면 정점마다 다시 디코드한다. 그래서 한 적용이 상한보다 많이 쓰면 그 동안은 상한을 넘는다.
             *          표본 중인 쪽은 shared_ptr 로 들고 있어 내려도 안전하다.
             */
            static void evictOverBudget( TextureCache& cache, const DecodedTexture& keep )
            {
                size_t total = 0;
                for ( const auto& [path, pTexture] : cache )
                {
                    total += pTexture->getByteSize();
                }
                const int64 idleBefore = getNowMilliseconds() - kRecheckIntervalMilliseconds;
                while ( total > kMaxCacheBytes )
                {
                    auto  oldest        = cache.end();
                    int64 oldestLastUse = idleBefore;
                    for ( auto iter = cache.begin(); iter != cache.end(); ++iter )
                    {
                        const int64 lastUse    = iter->second->_lastUseMilliseconds.load( std::memory_order_relaxed );
                        const bool  bCandidate = iter->second.get() != &keep && lastUse < oldestLastUse;
                        if ( bCandidate )
                        {
                            oldest        = iter;
                            oldestLastUse = lastUse;
                        }
                    }
                    if ( oldest == cache.end() )
                        break; // 남은 것은 방금 읽었거나 지금 쓰는 이미지뿐이다
                    SW_LOG_INFO( "Mesh modifier texture cache over %# MB - dropping '%#'", kMaxCacheBytes / constant::kBytesPerMegabyte, oldest->first.c_str() );
                    total -= oldest->second->getByteSize();
                    cache.erase( oldest );
                }
            }

            static int32 wrapIndex( int32 index, int32 size )
            {
                const int32 wrapped = index % size;
                return wrapped < 0 ? wrapped + size : wrapped;
            }

            /** @brief 반복 래핑 좌표 하나를 텍셀 두 개와 섞음 비율로 나눕니다. 텍셀 중심은 (i + 0.5) / size 입니다. */
            static void splitCoordinate( float32 coordinate, int32 size, int32& outIndex0, int32& outIndex1, float32& outFraction )
            {
                const float32 safeCoordinate = std::isfinite( coordinate ) ? coordinate - std::floor( coordinate ) : 0.0f;
                const float32 texelPosition  = safeCoordinate * static_cast<float32>( size ) - 0.5f;
                const float32 floorPosition  = std::floor( texelPosition );
                const int32   index          = static_cast<int32>( floorPosition );
                outFraction                  = texelPosition - floorPosition;
                outIndex0                    = wrapIndex( index, size );
                outIndex1                    = wrapIndex( index + 1, size );
            }

            static float4 lerp4( const float4& a, const float4& b, float32 t )
            {
                return float4{ a._x + ( b._x - a._x ) * t, a._y + ( b._y - a._y ) * t, a._z + ( b._z - a._z ) * t, a._w + ( b._w - a._w ) * t };
            }

            static const float4& texelAt( const DecodedTexture& texture, int32 x, int32 y )
            {
                return texture._listTexel[static_cast<size_t>( y ) * static_cast<size_t>( texture._width ) + static_cast<size_t>( x )];
            }

            static float4 sampleBilinear( const DecodedTexture& texture, const float2& uv )
            {
                int32   x0 = 0, x1 = 0, y0 = 0, y1 = 0;
                float32 fx = 0.0f, fy = 0.0f;
                splitCoordinate( uv._x, texture._width, x0, x1, fx );
                splitCoordinate( uv._y, texture._height, y0, y1, fy );
                const float4 top    = lerp4( texelAt( texture, x0, y0 ), texelAt( texture, x1, y0 ), fx );
                const float4 bottom = lerp4( texelAt( texture, x0, y1 ), texelAt( texture, x1, y1 ), fx );
                return lerp4( top, bottom, fy );
            }
        };
    } // namespace

    /** @brief 표본기가 소유하는 캐시와 그 잠금입니다. 여러 작업 스레드가 같이 부르므로 잠금이 캐시를 지킨다. */
    struct MeshModifierTextureSampler::State
    {
        shared_mutex                                     _mutex;
        MeshModifierTextureSamplerInternal::TextureCache _cache;
    };

    MeshModifierTextureSampler::MeshModifierTextureSampler()
        : _pState{ make_unique<State>() }
    {
    }

    MeshModifierTextureSampler::~MeshModifierTextureSampler() = default;

    bool MeshModifierTextureSampler::trySample( string_view assetPath, float2 uv, float4& outRGBA ) const
    {
        bool bSampled = false;
        if ( assetPath.empty() == false )
        {
            const std::shared_ptr<DecodedTexture> pTexture = MeshModifierTextureSamplerInternal::acquire( _pState->_mutex, _pState->_cache, assetPath );
            if ( pTexture != nullptr && pTexture->isValid() )
            {
                outRGBA  = MeshModifierTextureSamplerInternal::sampleBilinear( *pTexture, uv );
                bSampled = true;
            }
        }
        return bSampled;
    }

    void MeshModifierTextureSampler::clearCache()
    {
        UniqueLock<shared_mutex> lock{ _pState->_mutex };
        _pState->_cache.clear();
    }
} // namespace sw::editor
