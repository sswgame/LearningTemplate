/**
 * @file TextureCacheImpl.h
 * @brief 텍스처 캐시의 내부 상태(`TextureCache::Impl`)와 스트리밍 폐기 목록입니다. 캐시 본문과 핫 리로드(`TextureCacheReload.cpp`)만 include 한다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/Image/DDSLoader.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"

#include <condition_variable>

namespace sw
{
    /**
     * @brief 스트리밍이 위쪽 밉을 업로드하며 바꿔 끼운 옛 GPU 자원의 목록입니다(`Texture2D::upgradeResidency`). 디바이스 수명 통보를 받아 종료 때 남은 것을 해제합니다.
     * @details 렌더 스레드가 넣고 게임 스레드가 해제한다(`TextureCache::releaseRetiredTextures`) — 게임 스레드의 `releaseHandle` 은 이미 만든 패킷이 끝날 때까지 미루지만
     *          렌더 스레드 자신의 해제는 곧바로라, 옛 SRV 번호를 값으로 가진 패킷이 끝나기 전에 그 번호가 다른 텍스처에 다시 쓰일 수 있다.
     */
    class TextureStreamingRetireList final : public RHIRenderResource
    {
    public:
        struct Item
        {
            IRHIDevice*              _pDevice{ nullptr };
            Texture2DRetiredResource _resource{};
        };

        TextureStreamingRetireList() = default;

        void add( IRHIDevice* pDevice, const Texture2DRetiredResource& resource )
        {
            ScopedLock<mutex> lock{ _mutex };
            _listItem.push_back( Item{ pDevice, resource } );
        }

        /** @brief @p pDevice 의 것을 모두 `releaseHandle` 로 해제합니다(게임 스레드 — 이미 만든 패킷이 끝날 때까지 미룬다). */
        void releaseFor( IRHIDevice* pDevice )
        {
            vector<Item> listRelease;
            {
                ScopedLock<mutex> lock{ _mutex };
                for ( size_t index = 0; index < _listItem.size(); )
                {
                    if ( _listItem[index]._pDevice != pDevice )
                    {
                        ++index;
                        continue;
                    }
                    listRelease.push_back( _listItem[index] );
                    VectorUtil::removeAtSwap( _listItem, index );
                }
            }
            for ( const Item& item : listRelease )
            {
                if ( item._resource._srv != kInvalidDescriptorIndex )
                    pDevice->releaseHandle( RHIHandleKind::BindlessTexture, item._resource._srv );
                if ( item._resource._handle != 0 )
                    pDevice->releaseHandle( RHIHandleKind::Texture, item._resource._handle );
            }
        }

        void releaseRHI( IRHIDevice* pDevice ) override { releaseFor( pDevice ); }

        void forgetRHI( IRHIDevice* pDevice ) override
        {
            ScopedLock<mutex> lock{ _mutex };
            VectorUtil::removeIf( _listItem, [pDevice]( const Item& item )
            { return item._pDevice == pDevice; } );
        }

    private:
        mutable mutex          _mutex;
        vector<Item> _listItem SW_GUARDED_BY( _mutex );
    };

    struct TextureCache::Impl
    {
        struct Entry
        {
            unique_ptr<Texture2D>      _texture;
            weak_ptr<TextureLoadBlock> _pLoadBlock; ///< 진행 중인 로드(`request`) — 같은 경로의 요청은 이 블록을 받는다
            uint32                     _refCount{ 0 };
            bool                       _bLoading{ false };   ///< 잠금 밖에서 올리는 중(로드 블록 · `reload` · `resizeRenderTarget`) — 블록이 없으면 다른 스레드는 `_cvLoaded` 로 기다린다
            bool                       _bPublished{ false }; ///< 한 번이라도 올라왔는가 — `find` 는 이것만 내준다
            bool                       _bPinned{ false };    ///< CPU 텍스처 — `releaseCPUTexture` 전에는 참조가 0 이어도 지우지 않는다
            bool                       _bUpgrading{ false }; ///< 렌더 스레드가 위쪽 밉을 업로드하는 중(`updateStreaming`) — 참조가 0 이어도 지우지 않고, 다시 읽기는 기다린다
        };
        /** @brief 위쪽 밉을 기다리는 텍스처 하나입니다 — 로드 단계가 읽은 DDS 이미지를 보관한다(다시 읽지 않는다). */
        struct StreamingUpgrade
        {
            string                   _key;
            unique_ptr<DDSImageData> _pImage;
        };
        struct RenderTargetSize
        {
            uint32 _width{ TextureCache::kDefaultRenderTargetSize };
            uint32 _height{ TextureCache::kDefaultRenderTargetSize };
        };
        /** @brief `requestRegionUpdate` 가 맡긴 구간 하나입니다(RGBA8 바이트를 소유합니다). */
        struct RegionUpdate
        {
            string        _key;
            uint32        _x{ 0 };
            uint32        _y{ 0 };
            uint32        _width{ 0 };
            uint32        _height{ 0 };
            vector<uint8> _rgbaBytes;
            bool          _bSwappedToBgra{ false }; ///< `_rgbaBytes` 를 이미 BGRA 순서로 바꿨다(`Busy` 로 미룬 갱신을 다시 낼 때 두 번 바꾸지 않게)
        };
        unordered_map<string, Entry> _mapEntry  SW_GUARDED_BY( _mutex );
        vector<RegionUpdate>                    _listPendingRegion;  ///< 게임 스레드가 쌓고 렌더 스레드가 비운다(`_regionMutex`)
        vector<RegionUpdate>                    _listFlushingRegion; ///< 렌더 스레드만 쓰는 맞바꾸기 자리(저장소를 재사용한다)
        mutex                                   _regionMutex;
        unordered_map<string, RenderTargetSize> _mapRenderTargetSize; ///< `declareRenderTarget` 이 알린 크기
        shared_mutex                            _mutex;
        /// @brief 항목의 `_bLoading` 이 내려갈 때 깨운다. 태스크 대기가 아니다 — 맵 잠금 아래의 항목 상태를 기다리고, 올리는 쪽(`reload` · `resizeRenderTarget`)은 태스크 없이 디바이스 스레드에서 돈다.
        std::condition_variable_any                    _cvLoaded;
        atomic<uint32>                                 _reloadGeneration{ 0 }; ///< SRV 가 바뀌었을 수 있는 횟수 — 머티리얼이 SRV 인덱스를 다시 받을 때를 안다
        mutex                                          _awaitingMutex;
        vector<shared_ptr<TextureLoadBlock>>           _listAwaitingUpload; ///< 워커가 읽기를 마치고 업로드를 기다리는 로드(`completePendingLoads` 가 비운다, `_awaitingMutex`)
        vector<shared_ptr<TextureLoadBlock>>           _listUploading;      ///< 업로드를 제출했고 끝남 확인을 기다리는 로드(`completePendingLoads` 가 폴링한다, `_awaitingMutex`)
        mutex                                          _streamingMutex;
        vector<StreamingUpgrade> _listStreamingUpgrade SW_GUARDED_BY( _streamingMutex ); ///< 위쪽 밉을 기다리는 텍스처(먼저 들어온 것부터, `updateStreaming` 이 비운다)
        TextureStreamingRetireList                     _retired;                         ///< 바꿔 끼운 옛 GPU 자원(`releaseRetiredTextures` 가 해제한다)
        vector<string> _listUpgrading                  SW_GUARDED_BY( _streamingMutex ); ///< 위쪽 밉 업로드를 제출했고 끝남 확인을 기다리는 텍스처의 키(`updateStreaming` 이 폴링한다)
        unordered_map<string, float32>                 _mapScreenSize;                   ///< 이번 프레임에 알린 화면 크기(`noteScreenSize`, `_streamingMutex`) — `updateStreaming` 이 줄을 정렬한 뒤 비운다

        /** @brief `submitRegionUpdate` 의 결과: 올림 · 다음 프레임에 다시(`Busy` 이면 이번 차례의 나머지도 미룬다) · 버림. */
        enum class RegionFlushResult : uint8
        {
            Uploaded,
            Retry,
            RetryBusy,
            Dropped
        };

        /** @brief 구간 갱신 하나를 CPU 원본 또는 GPU 텍스처에 냅니다(`flushRegionUpdates` 의 요청 하나 내기). `Busy` · `Rejected` 규칙은 그 함수 머리 주석에 있다. */
        RegionFlushResult submitRegionUpdate( RegionUpdate& update, IRHIDevice* pDevice )
        {
            // 올리는 동안 같은 텍스처를 놓거나 다시 올리지 못하게 읽기 잠금을 쥔다(release · reload 는 쓰기 잠금을 잡는다).
            ScopedSharedLock<shared_mutex> lock{ _mutex };
            const auto                     it     = _mapEntry.find( update._key );
            Texture2D*                     pFound = it != _mapEntry.end() ? it->second._texture.get() : nullptr;
            RHITextureRegionUploadDesc     desc{};
            desc._pData     = update._rgbaBytes.data();
            desc._sizeBytes = static_cast<uint32>( update._rgbaBytes.size() );
            desc._x         = update._x;
            desc._y         = update._y;
            desc._width     = update._width;
            desc._height    = update._height;
            // CPU 텍스처는 GPU 에 없어도 원본을 고친다. 올리는 중이면 다음 프레임으로 미룬다(그 스레드가 원본을 읽고 있다).
            if ( pFound != nullptr && pFound->hasCPUBytes() )
            {
                if ( it->second._bLoading )
                    return RegionFlushResult::Retry;
                const RHISubmitResult written = pFound->writeCPUBytes( pDevice, desc );
                if ( written == RHISubmitResult::Accepted )
                    return RegionFlushResult::Uploaded;
                // 원본은 이미 고쳤다 — 다음 프레임에 같은 구간을 다시 쓰고 낸다(같은 바이트라 원본은 그대로다).
                if ( written == RHISubmitResult::Busy )
                    return RegionFlushResult::RetryBusy;
                SW_LOG_ERROR( "Region update for CPU texture %# rejected (%#,%# %#x%#) - dropped", update._key.c_str(), update._x, update._y, update._width, update._height );
                return RegionFlushResult::Dropped;
            }
            const bool bResident = pFound != nullptr && it->second._bPublished && it->second._bLoading == false && pFound->isRHIValid();
            if ( bResident == false || pDevice == nullptr )
            {
                SW_LOG_WARNING( "Region update for texture %# dropped - the texture is not resident", update._key.c_str() );
                return RegionFlushResult::Dropped;
            }
            const Texture2D& texture = *pFound;
            if ( texture.getFormat() == RHIFormat::B8G8R8A8_UNORM )
            {
                if ( update._bSwappedToBgra == false )
                {
                    for ( size_t offset = 0; offset + 3 < update._rgbaBytes.size(); offset += 4 )
                    {
                        std::swap( update._rgbaBytes[offset], update._rgbaBytes[offset + 2] );
                    }
                    update._bSwappedToBgra = true;
                }
            }
            else if ( texture.getFormat() != RHIFormat::R8G8B8A8_UNORM )
            {
                SW_LOG_WARNING( "Region update for texture %# dropped - only RGBA8 and BGRA8 textures take RGBA8 bytes", update._key.c_str() );
                return RegionFlushResult::Dropped;
            }
            const RHITransferSubmit submit = pDevice->getResourceFactory()->submitTextureRegionUpload( texture.getHandle(), desc );
            if ( submit._result == RHISubmitResult::Busy )
                return RegionFlushResult::RetryBusy;
            if ( submit.isAccepted() )
                return RegionFlushResult::Uploaded;
            SW_LOG_ERROR( "Region update for texture %# rejected by the RHI (%#,%# %#x%#) - dropped", update._key.c_str(), update._x, update._y, update._width, update._height );
            return RegionFlushResult::Dropped;
        }

        /** @brief 위쪽 밉을 기다리는 줄에 넣습니다(로드 블록의 완료 단계). */
        void queueStreamingUpgrade( const string& key, unique_ptr<DDSImageData> pImage )
        {
            ScopedLock<mutex> lock{ _streamingMutex };
            _listStreamingUpgrade.push_back( StreamingUpgrade{ key, std::move( pImage ) } );
        }

        /** @brief 참조를 하나 놓습니다. 0 이 되면 GPU 자원까지 해제합니다(`TextureCache::release` · 로드 블록 소멸). */
        void releaseReference( const string& key, IRHIDevice* pDevice )
        {
            // GPU 자원은 잠금을 놓은 뒤 내린다(장치 호출 — 잠금은 잎 잠금, 결정 5-3). 표에서 지우는 항목은 텍스처를 꺼내고, 표에 남는 고정(CPU) 항목은
            // 진행 중 표시(`_bLoading`)를 세워 그사이 acquire 가 기다리게 한 뒤 내리고 다시 잠가 표시를 내린다(`reload` 와 같은 순서).
            unique_ptr<Texture2D> pErased;
            Texture2D*            pPinned = nullptr;
            {
                UniqueLock<shared_mutex> lock{ _mutex };
                auto                     it = _mapEntry.find( key );
                if ( it == _mapEntry.end() )
                    return;
                if ( it->second._refCount > 0 )
                    --it->second._refCount;
                // 올리는 중(acquire · reload · 스트리밍)이면 지우지 않는다 — 그 스레드가 끝날 때 참조 수를 보고 정리한다.
                const bool bUnused = it->second._refCount == 0 && it->second._bLoading == false && it->second._bUpgrading == false;
                if ( bUnused == false )
                    return;
                // CPU 텍스처는 원본을 남긴다 — 소유자가 `releaseCPUTexture` 로 지운다.
                if ( it->second._bPinned )
                {
                    if ( it->second._texture == nullptr )
                        return;
                    it->second._bLoading = true;
                    pPinned              = it->second._texture.get();
                }
                else
                {
                    pErased = std::move( it->second._texture );
                    _mapEntry.erase( it );
                }
            }
            if ( pErased != nullptr )
                pErased->releaseRHI( pDevice );
            if ( pPinned == nullptr )
                return;
            pPinned->releaseRHI( pDevice );
            {
                UniqueLock<shared_mutex> lock{ _mutex };
                const auto               it = _mapEntry.find( key );
                SW_ASSERT( it != _mapEntry.end() && it->second._bLoading );
                it->second._bLoading = false;
                // 내리는 사이 소유자가 `releaseCPUTexture` 했으면 여기서 지운다(GPU 자원은 이미 내렸다).
                const bool bOrphaned = it->second._bPinned == false && it->second._refCount == 0;
                if ( bOrphaned )
                    _mapEntry.erase( it );
            }
            _cvLoaded.notify_all();
        }

        /**
         * @brief 로드 블록의 완료 단계가 끝났음을 항목에 적습니다. 성공하면 공개하고 블록 몫의 참조를 하나 셉니다. 실패하면 아무도 쥐지 않은 항목을 지웁니다.
         * @details 진행 중 표시를 내리는 곳은 여기 하나다 — 로드 단계가 실패해도 완료 단계를 거쳐 여기로 온다(`TextureLoadBlock` 머리말).
         */
        void finishLoad( const string& key, bool bReady )
        {
            {
                UniqueLock<shared_mutex> lock{ _mutex };
                const auto               it = _mapEntry.find( key );
                SW_ASSERT( it != _mapEntry.end() && it->second._bLoading );
                Entry& entry      = it->second;
                entry._bLoading   = false;
                entry._pLoadBlock = weak_ptr<TextureLoadBlock>{};
                if ( bReady )
                {
                    entry._bPublished = true;
                    ++entry._refCount;
                }
                else if ( entry._refCount == 0 && entry._bPinned == false )
                {
                    _mapEntry.erase( it );
                }
            }
            // 새 SRV 가 생겼다 — 로드 중 텍스처를 빌려 둔 머티리얼이 다시 받게 한다(진행 중 표시를 내린 뒤에 올린다: `reload` 와 같은 순서).
            if ( bReady )
                _reloadGeneration.fetch_add( 1u, std::memory_order_acq_rel );
            _cvLoaded.notify_all();
        }
    };
} // namespace sw
