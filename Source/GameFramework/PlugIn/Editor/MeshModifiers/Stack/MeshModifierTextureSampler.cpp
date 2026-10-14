/**
 * @file MeshModifierTextureSampler.cpp
 * @brief 이미지 파일 CPU 디코드 · 경로 키 캐시 · 반복 래핑 이중선형 표본을 구현합니다.
 * @details stb_image 는 `STB_IMAGE_STATIC` 으로 이 번역 단위 안에만 둡니다 — Editor 의 `ImageUtil` 이 같은 구현을 따로 품으므로 심볼이 겹치지 않아야 합니다.
 */
#include "pch.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierTextureSampler.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Resource/ResourcePaths.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace sw::editor
{
    SW_LOG_CALLER( "MeshModifierTextureSampler" );

    namespace
    {
        /** @brief 디코드한 이미지 한 장입니다. 픽셀은 만든 뒤 바뀌지 않고, 다시 읽으면 새 항목으로 바꿔 끼웁니다. */
        struct DecodedTexture
        {
            string         _resolvedPath;              ///< 실제로 읽은 파일 경로(못 찾았으면 빈 문자열)
            int64          _writeTicks = 0;            ///< 읽을 때의 파일 쓰기 시각(FileUtil 눈금)
            int32          _width      = 0;            ///< 가로 텍셀 수
            int32          _height     = 0;            ///< 세로 텍셀 수
            vector<float4> _listTexel;                 ///< 행 우선, 첫 행이 v = 0 쪽입니다
            atomic<int64>  _lastCheckMilliseconds{ 0 }; ///< 마지막으로 파일 시각을 본 단조 시계(ms)

            bool isValid() const { return _width > 0 && _height > 0 && _listTexel.empty() == false; }
        };

        struct MeshModifierTextureSamplerInternal
        {
            /** @brief 파일 시각을 다시 보는 최소 간격입니다. 정점마다 파일 시스템을 묻지 않으려는 것입니다. */
            static constexpr int64 kRecheckIntervalMilliseconds = 1000;

            static std::shared_mutex& getMutex()
            {
                static std::shared_mutex s_mutex;
                return s_mutex;
            }

            static unordered_map<string, std::shared_ptr<DecodedTexture>>& getCache()
            {
                static unordered_map<string, std::shared_ptr<DecodedTexture>> s_cache;
                return s_cache;
            }

            static int64 getNowMilliseconds()
            {
                return static_cast<int64>( std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now().time_since_epoch() ).count() );
            }

            /** @brief 절대 경로 · 작업 폴더 상대 경로가 있으면 그대로, 아니면 리소스 id 로 해석합니다. 못 찾으면 빈 문자열입니다. */
            static string resolvePath( string_view assetPath )
            {
                string resolved;
                if ( FileUtil::exists( assetPath ) )
                    resolved = string( assetPath );
                else
                    resolved = ResourcePaths::getResourcePath( assetPath );
                return resolved;
            }

            /** @brief 파일을 읽어 float4 텍셀로 디코드합니다. 실패해도 경로 · 시각이 든 (무효) 항목을 돌려줘 같은 실패를 정점마다 되풀이하지 않습니다. */
            static std::shared_ptr<DecodedTexture> decodeFile( string_view assetPath )
            {
                std::shared_ptr<DecodedTexture> pTexture = std::make_shared<DecodedTexture>();
                pTexture->_resolvedPath                  = resolvePath( assetPath );
                pTexture->_lastCheckMilliseconds.store( getNowMilliseconds(), std::memory_order_relaxed );

                vector<uint8> bytes;
                const bool    bRead = pTexture->_resolvedPath.empty() == false
                                && FileUtil::getFileWriteTime( pTexture->_resolvedPath, pTexture->_writeTicks )
                                && FileUtil::readFile( pTexture->_resolvedPath, bytes ) && bytes.empty() == false
                                && bytes.size() <= static_cast<size_t>( MathUtil::kMaxInt32 );
                if ( bRead == false )
                {
                    SW_LOG_WARNING( "Mesh modifier texture '%#' could not be read", string( assetPath ).c_str() );
                }
                else
                {
                    decodeBytes( bytes, *pTexture );
                    if ( pTexture->isValid() == false )
                        SW_LOG_WARNING( "Mesh modifier texture '%#' could not be decoded: %#", string( assetPath ).c_str(), stbi_failure_reason() );
                }
                return pTexture;
            }

            /** @brief HDR 원본은 float 그대로, 나머지는 8 비트를 0..1 로 나눠 4 채널로 채웁니다. */
            static void decodeBytes( const vector<uint8>& bytes, DecodedTexture& outTexture )
            {
                const int32 byteCount = static_cast<int32>( bytes.size() );
                int32       width     = 0;
                int32       height    = 0;
                int32       channels  = 0;
                if ( stbi_is_hdr_from_memory( bytes.data(), byteCount ) != 0 )
                {
                    float32* pDecoded = stbi_loadf_from_memory( bytes.data(), byteCount, &width, &height, &channels, 4 );
                    if ( pDecoded == nullptr )
                        return;
                    const size_t texelCount = static_cast<size_t>( width ) * static_cast<size_t>( height );
                    outTexture._listTexel.resize( texelCount );
                    for ( size_t i = 0; i < texelCount; ++i )
                        outTexture._listTexel[i] = float4{ pDecoded[i * 4 + 0], pDecoded[i * 4 + 1], pDecoded[i * 4 + 2], pDecoded[i * 4 + 3] };
                    stbi_image_free( pDecoded );
                }
                else
                {
                    uint8* pDecoded = stbi_load_from_memory( bytes.data(), byteCount, &width, &height, &channels, 4 );
                    if ( pDecoded == nullptr )
                        return;
                    constexpr float32 kByteToUnit = 1.0f / 255.0f;
                    const size_t      texelCount  = static_cast<size_t>( width ) * static_cast<size_t>( height );
                    outTexture._listTexel.resize( texelCount );
                    for ( size_t i = 0; i < texelCount; ++i )
                    {
                        outTexture._listTexel[i] = float4{ static_cast<float32>( pDecoded[i * 4 + 0] ) * kByteToUnit,
                                                           static_cast<float32>( pDecoded[i * 4 + 1] ) * kByteToUnit,
                                                           static_cast<float32>( pDecoded[i * 4 + 2] ) * kByteToUnit,
                                                           static_cast<float32>( pDecoded[i * 4 + 3] ) * kByteToUnit };
                    }
                    stbi_image_free( pDecoded );
                }
                outTexture._width  = width;
                outTexture._height = height;
            }

            /** @brief 간격이 지났으면 파일 시각을 다시 보고, 바뀌었으면 true(다시 읽어야 함)입니다. 한 스레드만 검사를 맡습니다. */
            static bool isStale( DecodedTexture& texture )
            {
                bool        bStale    = false;
                const int64 now       = getNowMilliseconds();
                int64       lastCheck = texture._lastCheckMilliseconds.load( std::memory_order_relaxed );
                if ( now - lastCheck >= kRecheckIntervalMilliseconds
                     && texture._lastCheckMilliseconds.compare_exchange_strong( lastCheck, now, std::memory_order_relaxed ) )
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
            static std::shared_ptr<DecodedTexture> acquire( string_view assetPath )
            {
                std::shared_ptr<DecodedTexture> pTexture;
                {
                    std::shared_lock<std::shared_mutex> lock( getMutex() );
                    auto                                iter = getCache().find( assetPath );
                    if ( iter != getCache().end() )
                        pTexture = iter->second;
                }

                bool bReload = pTexture == nullptr;
                if ( bReload == false && isStale( *pTexture ) )
                    bReload = true;

                if ( bReload )
                {
                    std::unique_lock<std::shared_mutex> lock( getMutex() );
                    auto                                iter = getCache().find( assetPath );
                    // 다른 스레드가 잠금을 먼저 잡아 새로 읽었으면 그것을 쓴다.
                    if ( iter != getCache().end() && iter->second != pTexture )
                    {
                        pTexture = iter->second;
                    }
                    else
                    {
                        pTexture                           = decodeFile( assetPath );
                        getCache()[string( assetPath )] = pTexture;
                    }
                }
                return pTexture;
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

            static float4 sampleBilinear( const DecodedTexture& texture, const float2& uv )
            {
                int32   x0 = 0, x1 = 0, y0 = 0, y1 = 0;
                float32 fx = 0.0f, fy = 0.0f;
                splitCoordinate( uv._x, texture._width, x0, x1, fx );
                splitCoordinate( uv._y, texture._height, y0, y1, fy );
                const auto texelAt = [&texture]( int32 x, int32 y ) -> const float4& {
                    return texture._listTexel[static_cast<size_t>( y ) * static_cast<size_t>( texture._width ) + static_cast<size_t>( x )];
                };
                const float4 top    = lerp4( texelAt( x0, y0 ), texelAt( x1, y0 ), fx );
                const float4 bottom = lerp4( texelAt( x0, y1 ), texelAt( x1, y1 ), fx );
                return lerp4( top, bottom, fy );
            }
        };
    } // namespace

    bool MeshModifierTextureSampler::trySample( string_view assetPath, float2 uv, float4& outRGBA )
    {
        bool bSampled = false;
        if ( assetPath.empty() == false )
        {
            const std::shared_ptr<DecodedTexture> pTexture = MeshModifierTextureSamplerInternal::acquire( assetPath );
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
        std::unique_lock<std::shared_mutex> lock( MeshModifierTextureSamplerInternal::getMutex() );
        MeshModifierTextureSamplerInternal::getCache().clear();
    }
} // namespace sw::editor
