#include "pch.h"

#include "Engine/Graphics/Texture/TextureCache.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Diagnostics/CacheStats.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Asset/AssetLoadProfiler.h"
#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/BuiltInAssetCache.h"
#include "Engine/Asset/Image/DDSLoader.h"
#include "Engine/Asset/Image/TextureMipLayout.h"
#include "Engine/Asset/StreamingSourcePriority.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIMemoryTracker.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/Transfer/RHITransferUtil.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureFormatFamily.h"
#include "Engine/Graphics/Texture/TextureStreamingPolicy.h"
#include "Engine/Graphics/Texture/TextureStreamingPool.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Serialization/Serializer/ReloadResult.h"

#include <condition_variable>

// 캐시의 내부 상태(`TextureCache::Impl`)다 — 이 파일만 쓴다(옛 핫 리로드 파일과 나눠 쓰던 헤더를 합쳤다).
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
        void releaseForDevice( IRHIDevice* pDevice )
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
                    pDevice->releaseBindlessIndex( item._resource._srv );
                if ( item._resource._handle != 0 )
                    pDevice->releaseHandle( item._resource._handle );
            }
        }

        void releaseRHI( IRHIDevice* pDevice ) override { releaseForDevice( pDevice ); }

        void clearRHI( IRHIDevice* pDevice ) override
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
            unique_ptr<Texture2D>           _texture;
            weak_ptr<TextureLoadOperation>  _pLoadOperation; ///< 진행 중인 로드(`request`) — 같은 경로의 요청은 이 로드 작업을 받는다
            uint32                          _refCount{ 0 };
            bool                            _bLoading{ false };                                    ///< 잠금 밖에서 올리는 중(로드 작업 · `reload` · `resizeRenderTarget`) — 로드 작업이 없으면 다른 스레드는 `_cvLoaded` 로 기다린다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            bool                            _bPublished{ false };                                  ///< 한 번이라도 올라왔는가 — `find` 는 이것만 내준다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            bool                            _bPinned{ false };                                     ///< CPU 텍스처 — `releaseCPUTexture` 전에는 참조가 0 이어도 지우지 않는다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            bool                            _bUpgrading{ false };                                  ///< 렌더 스레드가 위쪽 밉을 업로드하는 중(`updateStreaming`) — 참조가 0 이어도 지우지 않고, 다시 읽기는 기다린다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            TextureStreamingReservationSlot _streamingReservation;                                 ///< 밉 스트리밍이 풀(`_streamingPool`)에서 쥔 바이트 — 항목이 사라지면 돌려준다(지우는 자리마다 빼지 않는다). 바꾸다 실패하면 옛 크기 그대로다
            TextureMipLayout                _mipLayout;                                            ///< 위쪽 밉을 다시 읽을 파일 구간(로드가 끝날 때 적는다 — 픽셀은 쥐지 않는다, MR-2)
            string                          _mipReadPath;                                          ///< 그 구간을 읽을 경로(형식 변형이 있으면 변형 파일 — 첫 로드가 읽은 파일과 같다)
            uint32                          _idleFrames{ 0 };                                      ///< 화면 크기를 알리지 않은 `updateStreaming` 수(R6-6)
            uint32                          _wantedFirstMip{ 0 };                                  ///< 마지막으로 알린 화면 크기가 요구한 가장 큰 밉(R6-6)
            uint32                          _framesSinceChange{ 0 };                               ///< 밉을 마지막으로 바꾼 뒤 지난 `updateStreaming` 수(R6-6 — 경계 흔들림 막기)
            TextureStreamingGroup           _streamingGroup{ TextureStreamingGroup::NeverStream }; ///< 요청한 쪽이 정한다(`request` 가 새 항목에 적는다) — 하나라도 `NeverStream` 이면 그것이다
            bool                            _bDemoted{ false };                                    ///< 예산 때문에 내렸다 — 다시 보이면 정책이 다시 올린다(처음 올리기는 로드가 줄에 넣는다) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };
        /** @brief 범위 읽기 하나의 결과입니다 — IO 완료(다른 스레드)가 바이트를 넣고 상태를 올린다. 렌더 스레드는 상태가 `Reading` 이 아닐 때만 바이트를 만진다. */
        struct StreamingReadResult
        {
            enum class State : uint8
            {
                Reading,
                Read,
                Failed
            };
            atomic<uint8> _state{ static_cast<uint8>( State::Reading ) };
            vector<uint8> _bytes;
        };
        /** @brief 읽는 중이거나 읽었고 제출을 기다리는 밉 요청입니다(`updateStreaming`). 바이트는 결과가 쥐고, 제출하거나 버릴 때 놓는다. */
        struct StreamingRead
        {
            TextureMipRequest               _request;
            uint64                          _byteSize{ 0 }; ///< `_bytesInFlight` 에 센 바이트
            shared_ptr<StreamingReadResult> _pResult;
            AsyncReadHandle                 _handle; ///< 결정적 실행이 끝남을 기다린다(`TextureStreamingBudget::_bFinishEachFrame`)
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
            bool          _bSwappedToBgra{ false }; ///< `_rgbaBytes` 를 이미 BGRA 순서로 바꿨다(`Busy` 로 미룬 갱신을 다시 낼 때 두 번 바꾸지 않게) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };
        TextureStreamingPool                   _streamingPool; ///< 항목의 예약보다 먼저 만들고 뒤에 없앤다(멤버 순서) — 예약이 사라질 때 여기서 뺀다
        unordered_map<string, Entry> _mapEntry SW_GUARDED_BY( _mutex );
        vector<RegionUpdate>                   _listPendingRegion;  ///< 게임 스레드가 쌓고 렌더 스레드가 비운다(`_regionMutex`)
        vector<RegionUpdate>                   _listFlushingRegion; ///< 렌더 스레드만 쓰는 맞바꾸기 자리(저장소를 재사용한다)
        mutex                                  _regionMutex;
        vector<TextureLayerBakeRequest>        _listPendingBake; ///< 게임 스레드가 쌓고 렌더 스레드가 가져간다(`_bakeMutex`) — 결과 타깃마다 하나
        /// @brief 최근에 그린 굽기(결과 타깃, 그린 번호) — 앞이 오래된 것이고 `kBakeHistoryCount` 를 넘으면 앞을 버린다(`_bakeMutex`).
        vector<pair<string, uint32>>            _listBakedTarget;
        uint32                                  _bakeSerial{ 0 }; ///< 그린 굽기마다 오르는 번호(`_bakeMutex`)
        mutex                                   _bakeMutex;
        unordered_map<string, RenderTargetSize> _mapRenderTargetSize; ///< `declareRenderTarget` 이 알린 크기
        shared_mutex                            _mutex;
        /// @brief 항목의 `_bLoading` 이 내려갈 때 깨운다. 태스크 대기가 아니다 — 맵 잠금 아래의 항목 상태를 기다리고, 올리는 쪽(`reload` · `resizeRenderTarget`)은 태스크 없이 디바이스 스레드에서 돈다.
        std::condition_variable_any                     _cvLoaded;
        mutex                                           _standInMutex;               ///< 자리 텍스처를 올리는 쪽 하나만(`ensureStandInTextures`)
        atomic<Texture2D*>                              _pLoadingStandIn{ nullptr }; ///< 예약한 번호가 처음 가리킬 로드 중 텍스처(이 캐시가 참조 하나를 쥔다)
        atomic<Texture2D*>                              _pMissingStandIn{ nullptr }; ///< 실패한 로드의 번호를 다시 묶을 누락 체커(이 캐시가 참조 하나를 쥔다)
        atomic<IRHIDevice*>                             _pBoundDevice{ nullptr };    ///< 엔진이 붙인 디바이스(`bindDevice`) — 게임 스레드 요청 · 해제가 쓴다
        string _formatVariantSuffix                     SW_GUARDED_BY( _mutex );     ///< 붙은 장치가 고른 형식 변형 접미사(`getFormatVariantSuffix` — 비면 임포트한 DDS 그대로)
        mutex                                           _awaitingMutex;
        vector<shared_ptr<TextureLoadOperation>>        _listAwaitingUpload; ///< 워커가 읽기를 마치고 업로드를 기다리는 로드(`completePendingLoads` 가 비운다, `_awaitingMutex`)
        vector<shared_ptr<TextureLoadOperation>>        _listUploading;      ///< 업로드를 제출했고 끝남 확인을 기다리는 로드(`completePendingLoads` 가 폴링한다, `_awaitingMutex`)
        mutex                                           _streamingMutex;
        vector<TextureMipRequest> _listStreamingUpgrade SW_GUARDED_BY( _streamingMutex );         ///< 위쪽 밉을 기다리는 밉 요청(먼저 들어온 것부터, `updateStreaming` 이 비운다) — 바이트를 쥐지 않는다(MR-2)
        vector<StreamingRead> _listReading              SW_GUARDED_BY( _streamingMutex );         ///< 범위를 읽는 중이거나 읽어 제출을 기다리는 요청
        uint64 _bytesInFlight                           SW_GUARDED_BY( _streamingMutex ) = 0;     ///< `_listReading` 이 쥔(쥘) 바이트 합 — `TextureStreamingBudget::_maxBytesInFlight` 아래로 묶는다
        bool _bStreamingParked                          SW_GUARDED_BY( _streamingMutex ) = false; ///< 지난 `updateStreaming` 이 풀 예산에 막혔다 — 줄은 예산이 날 때까지 쉰다(`countLoadsInFlight` 가 세지 않는다) NOLINT(sw-bool-flag): 잠금이 지키는 칸 하나다
        TextureStreamingRetireList                      _retired;                                 ///< 바꿔 끼운 옛 GPU 자원(`releaseRetiredTextures` 가 해제한다)
        atomic<int32>                                   _qualityMipDrop{ 0 };                     ///< 텍스처 품질이 버리는 위쪽 밉 수 — 게임 스레드가 뜬 사본(`refreshQualitySettings`)이라 워커 · 렌더 스레드는 이것만 읽는다
        vector<string> _listUpgrading                   SW_GUARDED_BY( _streamingMutex );         ///< 위쪽 밉 업로드를 제출했고 끝남 확인을 기다리는 텍스처의 키(`updateStreaming` 이 폴링한다)
        unordered_map<string, TextureStreamingDemand>   _mapStreamingDemand;                      ///< 이번 프레임에 알린 요구(`recordStreamingDemand`, `_streamingMutex`) — `updateStreaming` 이 꺼내 항목의 필요한 밉에 적고 줄을 정렬한다

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
                SW_LOG_ERROR_ID( "TextureCache.RegionUpdateCPUTexture", "Region update for CPU texture %# rejected (%#,%# %#x%#) - dropped", update._key.c_str(), update._x, update._y, update._width, update._height );
                return RegionFlushResult::Dropped;
            }
            const bool bResident = pFound != nullptr && it->second._bPublished && it->second._bLoading == false && pFound->isRHIValid();
            if ( bResident == false || pDevice == nullptr )
            {
                SW_LOG_WARNING_ID( "TextureCache.RegionUpdateTextureDropped", "Region update for texture %# dropped - the texture is not resident", update._key.c_str() );
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
                SW_LOG_WARNING_ID( "TextureCache.RegionUpdateTextureDropped2", "Region update for texture %# dropped - only RGBA8 and BGRA8 textures take RGBA8 bytes", update._key.c_str() );
                return RegionFlushResult::Dropped;
            }
            const RHITransferSubmit submit = pDevice->getResourceFactory()->submitTextureRegionUpload( texture.getHandle(), desc );
            if ( submit._result == RHISubmitResult::Busy )
                return RegionFlushResult::RetryBusy;
            if ( submit.isAccepted() )
                return RegionFlushResult::Uploaded;
            SW_LOG_ERROR_ID( "TextureCache.RegionUpdateTextureRejected", "Region update for texture %# rejected by the RHI (%#,%# %#x%#) - dropped", update._key.c_str(), update._x, update._y, update._width, update._height );
            return RegionFlushResult::Dropped;
        }

        /**
         * @brief 위쪽 밉을 기다리는 줄에 밉 요청을 넣습니다(로드 작업의 완료 단계). 줄은 바이트를 쥐지 않는다 — 항목에 파일 구간과 읽을 경로만 적고, 올릴 때 범위로 다시 읽는다(MR-2).
         * @details 언리얼 `FRenderAssetStreamingManager` 의 대기 줄도 "어느 텍스처의 어느 밉" 만 든다(`FTexture2DStreamIn_IO` 가 그때 벌크 데이터를 오프셋으로 읽는다).
         */
        void queueMipRequest( const string& key, TextureMipLayout layout, string readPath )
        {
            const uint32 firstMip = Texture2D::computeUpgradeFirstMip( layout, _qualityMipDrop.load( std::memory_order_relaxed ) );
            const uint32 mipCount = static_cast<uint32>( layout._listMip.size() ) - firstMip;
            {
                UniqueLock<shared_mutex> lock{ _mutex };
                const auto               it = _mapEntry.find( key );
                if ( it == _mapEntry.end() )
                    return;
                it->second._mipLayout   = std::move( layout );
                it->second._mipReadPath = std::move( readPath );
            }
            ScopedLock<mutex> lock{ _streamingMutex };
            _listStreamingUpgrade.push_back( TextureMipRequest{ key, firstMip, mipCount, StreamingSourcePriority::Lowest } );
        }

        /** @brief 참조를 하나 놓습니다. 0 이 되면 GPU 자원까지 해제합니다(`TextureCache::release` · 로드 작업 소멸). */
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
                // 내리는 사이 소유자가 `releaseCPUTexture` 했으면 여기서 지운다(GPU 자원은 이미 내렸다). 텍스처는 잠금을 놓은 뒤 사라진다.
                const bool bOrphaned = it->second._bPinned == false && it->second._refCount == 0;
                if ( bOrphaned )
                {
                    pErased = std::move( it->second._texture );
                    _mapEntry.erase( it );
                }
            }
            _cvLoaded.notify_all();
        }

        /**
         * @brief 텍스처 번호 @p index 를 누락 체커로 다시 묶습니다(올리다 실패 — 그 번호를 쥔 머티리얼이 옛 텍스처 · 빈 칸을 그리지 않게, R-6).
         * @details 디바이스를 쓰는 스레드에서 부른다(등록표를 바꾼다). 체커가 없으면(엔진 서비스 없음) 오류를 남기고 번호를 놓은 뒤 false 다.
         */
        bool pointAtMissingChecker( IRHIDevice& device, RHITextureSRVIndex index, const string& key )
        {
            device.activateReservedBindlessTextures(); // 예약만 하고 아직 살리지 않은 번호일 수 있다
            const Texture2D* const     pMissing  = _pMissingStandIn.load( std::memory_order_acquire );
            IRHIResourceFactory* const pResource = device.getResourceFactory();
            const bool                 bChecker  = pMissing != nullptr && pMissing->isRHIValid() && pResource != nullptr;
            if ( bChecker && pResource->rebindBindlessTexture( index, pMissing->getHandle() ) )
                return true;
            SW_LOG_ERROR_ID( "TextureCache.MissingCheckerRebindFailed", "TextureCache: the bindless index %# of '%#' could not point at the missing-texture checker - releasing it", index.getValue(), key.c_str() );
            device.releaseBindlessIndex( index );
            return false;
        }

        /**
         * @brief 로드 작업의 완료 단계가 끝났음을 항목에 적습니다. 성공하면 공개하고 로드 작업 몫의 참조를 하나 셉니다. 실패하면 아무도 쥐지 않은 항목을 지웁니다.
         * @details 진행 중 표시를 내리는 곳은 여기 하나다 — 로드 단계가 실패해도 완료 단계를 거쳐 여기로 온다(`TextureLoadOperation` 머리말).
         */
        void finishLoad( const string& key, bool bReady )
        {
            {
                UniqueLock<shared_mutex> lock{ _mutex };
                const auto               it = _mapEntry.find( key );
                SW_ASSERT( it != _mapEntry.end() && it->second._bLoading );
                Entry& entry          = it->second;
                entry._bLoading       = false;
                entry._pLoadOperation = weak_ptr<TextureLoadOperation>{};
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
            _cvLoaded.notify_all();
        }
    };
} // namespace sw

namespace sw
{
    namespace
    {
        /** @brief 텍스처 캐시의 계측 칸입니다(`CacheStats`). 캐시는 에셋 매니저마다 하나지만 칸은 프로세스에 하나라 매니저를 다시 세워도 이어 센다. */
        const CacheStatsHandle& getTextureCacheStats()
        {
            static const CacheStatsHandle s_stats = CacheStats::getOrCreate( TextureCache::kAssetKindName );
            return s_stats;
        }

    } // namespace

    namespace
    {
        // 텍스처 캐시가 만들 때 한 번 읽는 변수 — 익명 이름공간이라 로드 워커 · 렌더 스레드가 도는 다른 번역 단위는 이름을 볼 수 없다(사본 `Impl::_qualityMipDrop` 만 읽는다).

        /**
         * @brief 텍스처를 읽을 때 버리는 위쪽 밉 수입니다. 사용자 설정 `graphics.textureQuality` 선택지의 `targetValue` 가 넣는다(`engine.settings.xml`, 다시 시작해야 반영).
         * @details Graphics 가 UserSettings 보다 아래 층이라 이 변수는 읽는 쪽인 여기에 둔다(설정은 이름으로 넣는다). 언리얼 `r.Streaming.MipBias` 처럼 기동 때 정한다.
         */
        SW_GLOBAL_VARIABLE( int32, gv_textureMipDrop, 0, "텍스처를 읽을 때 버리는 위쪽 밉 수 (사용자 설정 graphics.textureQuality 의 targetValue, 다시 시작해야 반영)" );
    } // namespace

    TextureCache::TextureCache()
        : AssetCacheBase{ CacheTracking::counted( kAssetKindName ) }
        , _impl{ sw::make_shared<Impl>() }
        , _renderTargetDeclarationSink{ *this }
        , _cpuTextureSerial{ 0 }
    {
        refreshQualitySettings(); // 캐시는 엔진 기동(게임 스레드, 명령줄을 적용한 뒤)에 선다 — 사용자 설정은 그 뒤 프레임마다 다시 뜬다
    }

    void TextureCache::refreshQualitySettings()
    {
        // 로드 워커 · 렌더 스레드는 변수를 직접 읽지 않는다 — 게임 스레드가 여기서 뜬 사본만 본다(언리얼 콘솔 변수의 렌더 스레드 사본과 같은 생각).
        if ( engine::areEngineServicesBound() )
            engine::getSubsystem<TaskManager>().ensureMainThread();
        _impl->_qualityMipDrop.store( gv_textureMipDrop, std::memory_order_relaxed );
    }

    int32 TextureCache::getQualityMipDrop() const
    {
        return _impl->_qualityMipDrop.load( std::memory_order_relaxed );
    }

    TextureCache::~TextureCache() = default;

    void TextureCache::bindDevice( IRHIDevice* pDevice )
    {
        // 접미사를 먼저 정한다 — 장치를 본 워커 읽기가 이 장치의 형식을 읽게 한다.
        string formatSuffix = pDevice != nullptr ? TextureFormatFamily::selectVariantSuffix( *pDevice ) : string{};
        {
            const UniqueLock<shared_mutex> lock{ _impl->_mutex };
            _impl->_formatVariantSuffix = std::move( formatSuffix );
        }
        _impl->_pBoundDevice.store( pDevice, std::memory_order_release );
    }

    void TextureCache::ensureStandInTextures()
    {
        if ( engine::areEngineServicesBound() == false )
            return; // 도구 · 테스트 — 자리 텍스처가 없으면 번호를 예약하지 않는다(업로드가 끝날 때 등록한다)
        const auto isResident = []( const Texture2D* pTexture )
        { return pTexture != nullptr && pTexture->isRHIValid(); };
        if ( isResident( _impl->_pLoadingStandIn.load( std::memory_order_acquire ) ) && isResident( _impl->_pMissingStandIn.load( std::memory_order_acquire ) ) )
            return;
        // 처음이거나 디바이스 교체로 GPU 자원을 잃었다 — 동기로 (다시) 올린다. 캐시는 자리 텍스처마다 참조 하나만 쥔다(다시 올릴 때 받은 참조는 곧바로 놓는다).
        const ScopedLock<mutex>    lock{ _impl->_standInMutex };
        const EngineDefaultAssets& defaults      = engine::getSubsystem<EngineDefaultAssets>();
        const auto                 ensureStandIn = [this, &isResident]( atomic<Texture2D*>& slot, const string& path )
        {
            Texture2D* const pHeld = slot.load( std::memory_order_acquire );
            if ( isResident( pHeld ) )
                return;
            Texture2D* const pAcquired = acquire( path );
            if ( pAcquired == nullptr )
            {
                // 엔진 필수 데이터다 — 없으면 예약하지 않고(머티리얼은 체커를 빌린다) 오류를 남긴다.
                SW_LOG_ERROR_ID( "TextureCache.StandInTextureMissing", "TextureCache: the stand-in texture '%#' could not be loaded - bindless indices are not reserved for loading textures", path.c_str() );
                return;
            }
            if ( pHeld != nullptr )
                release( path );
            slot.store( pAcquired, std::memory_order_release );
        };
        ensureStandIn( _impl->_pLoadingStandIn, defaults._loadingTexture.get() );
        ensureStandIn( _impl->_pMissingStandIn, defaults._missingTexture.get() );
    }

    string TextureCache::getFormatVariantSuffix() const
    {
        const ScopedSharedLock<shared_mutex> lock{ _impl->_mutex };
        return _impl->_formatVariantSuffix;
    }

    TextureHandle TextureCache::request( string_view relativePath, AssetLoadStart start, TextureStreamingGroup group )
    {
        const ScopedMemoryTag memoryScope{ kMemoryTag };
        IRHIDevice* const     pDevice = _impl->_pBoundDevice.load( std::memory_order_acquire );
        const string          key     = AssetRegistry::toCacheKey( relativePath );
        if ( key.empty() || pDevice == nullptr )
        {
            shared_ptr<TextureLoadOperation> pFailed = sw::make_shared<TextureLoadOperation>( key, pDevice, TextureLoadOperation::Source::File, nullptr, _impl );
            pFailed->markFailedWithoutLoad( key.empty() ? "the texture path is empty" : "there is no device to upload to" );
            return TextureHandle{ std::move( pFailed ) };
        }

        // 비동기 요청은 자리 텍스처를 먼저 올려 둔다(잠금 밖 — 동기 `acquire` 로 올린다). 새 항목의 번호가 처음 가리킬 텍스처다(R-6).
        if ( start == AssetLoadStart::Async )
            ensureStandInTextures();
        Texture2D* const pLoadingStandIn = _impl->_pLoadingStandIn.load( std::memory_order_acquire );

        // 디스크 읽기와 GPU 생성은 잠금 밖에서 한다. 잠금 안에서는 "진행 중" 표시(_bLoading)와 그 로드의 로드 작업만 적고, 같은 키를 동시에
        // 원한 쪽은 같은 로드 작업을 받는다(이중 생성 없음). Texture2D 는 unique_ptr 이라 맵이 원소를 옮겨도 주소가 그대로다. 진행 중인 항목은
        // release · 실패 정리가 지우지 않는다.
        UniqueLock<shared_mutex>     lock{ _impl->_mutex };
        Texture2D*                   pTexture = nullptr;
        TextureLoadOperation::Source source   = TextureLoadOperation::Source::File;
        for ( ;; )
        {
            const auto it = _impl->_mapEntry.find( key );
            if ( it == _impl->_mapEntry.end() )
            {
                // CPU 텍스처는 원본이 있어야 만든다 — 소유자가 이미 놓았다.
                if ( isCPUTexturePath( key ) )
                {
                    lock.unlock();
                    shared_ptr<TextureLoadOperation> pFailed = sw::make_shared<TextureLoadOperation>( key, pDevice, TextureLoadOperation::Source::CPUBytes, nullptr, _impl );
                    pFailed->markFailedWithoutLoad( "the CPU texture was already released by its owner" );
                    return TextureHandle{ std::move( pFailed ) };
                }
                Impl::Entry& entry    = _impl->_mapEntry[key];
                entry._texture        = make_unique<Texture2D>();
                entry._bLoading       = true;
                entry._streamingGroup = group;
                pTexture              = entry._texture.get();
                break;
            }
            Impl::Entry& entry = it->second;
            if ( group == TextureStreamingGroup::NeverStream )
                entry._streamingGroup = TextureStreamingGroup::NeverStream; // 누군가 밉 전부가 필요하다 — 내리지 않는다(내렸으면 다음 스트리밍이 되올린다)
            if ( entry._bLoading )
            {
                shared_ptr<TextureLoadOperation> pInFlight = entry._pLoadOperation.lock();
                if ( pInFlight != nullptr )
                {
                    // 기다린 쪽이 완료를 이 디바이스로 실행한다 — 앞선 요청의 디바이스는 내려갔을 수 있다(펌프 없는 테스트의 디바이스 차례).
                    pInFlight->_pDevice.store( pDevice, std::memory_order_release );
                    getTextureCacheStats().recordHit();
                    return TextureHandle{ std::move( pInFlight ) };
                }
                // 로드 작업이 아닌 길(`reload` · `resizeRenderTarget`, 디바이스 스레드)이 올리는 중이다 — 짧게 기다린다.
                _impl->_cvLoaded.wait( lock );
                continue;
            }
            if ( entry._texture->isRHIValid() )
            {
                // 캐시 적중 — 단계를 거치지 않고 참조를 쥔 `Ready` 로드 작업을 준다.
                ++entry._refCount;
                shared_ptr<TextureLoadOperation> pReady = sw::make_shared<TextureLoadOperation>( key, pDevice, TextureLoadOperation::Source::File, entry._texture.get(), _impl );
                pReady->_bHoldsReference                = SW_TRUE;
                pReady->markReadyWithoutLoad();
                getTextureCacheStats().recordHit();
                return TextureHandle{ std::move( pReady ) };
            }
            // 들고는 있지만 GPU 자원이 없다(리로드 실패 · CPU 텍스처의 참조가 0 이 되어 내린 뒤) — 같은 객체에 다시 올린다.
            entry._bLoading = true;
            pTexture        = entry._texture.get();
            break;
        }

        // 여기까지 왔으면 새로 읽거나 올려야 한다(없던 항목이거나 GPU 자원이 내려간 항목) — 실패다.
        getTextureCacheStats().recordMiss();
        // 렌더 텍스처는 파일이 없다. 알린 크기(없으면 기본)로 렌더 타깃을 만든다. CPU 텍스처는 자기 원본을 올린다.
        if ( isRenderTargetPath( key ) )
            source = TextureLoadOperation::Source::RenderTarget;
        else if ( pTexture->hasCPUBytes() )
            source = TextureLoadOperation::Source::CPUBytes;
        shared_ptr<TextureLoadOperation> pOperation = sw::make_shared<TextureLoadOperation>( key, pDevice, source, pTexture, _impl );
        // 비동기 로드(씬의 머티리얼)는 아래쪽 밉부터 — 위쪽 밉은 렌더 스레드가 뒤에 업로드한다. 동기 로드(도구 · 미리보기)는 받는 자리에서 밉 전부를 쓴다.
        const bool bStreamed   = start == AssetLoadStart::Async && _impl->_mapEntry[key]._streamingGroup == TextureStreamingGroup::Streamable;
        pOperation->_residency = bStreamed ? TextureResidency::StreamingBase : TextureResidency::Full;
        if ( source == TextureLoadOperation::Source::RenderTarget )
        {
            const auto sizeIter = _impl->_mapRenderTargetSize.find( key );
            if ( sizeIter != _impl->_mapRenderTargetSize.end() )
            {
                pOperation->_renderTargetWidth  = sizeIter->second._width;
                pOperation->_renderTargetHeight = sizeIter->second._height;
            }
        }
        // 번호를 예약해 로드 중 텍스처를 가리키게 한다 — 업로드가 끝나면 같은 번호를 진짜 텍스처로 다시 묶는다(R-6). 잠금 안에서 맡겨, 같은 로드를 받는 다른 요청도
        // 처음부터 같은 번호를 본다. 동기 요청도 자리 텍스처가 이미 있으면 예약한다 — 그 로드를 기다리던 비동기 요청이 번호 없는 로드를 받지 않게.
        const bool bReserve = pLoadingStandIn != nullptr && pLoadingStandIn != pTexture && pLoadingStandIn->isRHIValid() && pTexture->getKeptSRV() == kInvalidDescriptorIndex;
        if ( bReserve )
        {
            const RHITextureSRVIndex reserved = pDevice->reserveBindlessTexture( pLoadingStandIn->getHandle() );
            if ( reserved != kInvalidDescriptorIndex )
                pTexture->attachReservedSRV( reserved, pDevice );
            else
                SW_LOG_ERROR_ID( "TextureCache.ReserveBindlessFailed", "TextureCache: no bindless index could be reserved for '%#' - materials sample the missing-texture checker instead", key.c_str() );
        }
        _impl->_mapEntry[key]._pLoadOperation = pOperation;
        lock.unlock();

        AssetLoadOperation::startLoad( pOperation, start );
        return TextureHandle{ std::move( pOperation ) };
    }

    Texture2D* TextureCache::acquire( string_view relativePath )
    {
        const TextureHandle handle = request( relativePath, AssetLoadStart::OnCallingThread, TextureStreamingGroup::NeverStream );
        // 기다림이 계약이다 — `acquire` 는 포인터를 바로 돌려주는 동기 API 이고, 기다리지 않을 호출자는 `request` 핸들을 들고 뒤 프레임에 본다.
        const shared_ptr<Texture2D> pTexture = handle.wait();
        if ( pTexture == nullptr )
            return nullptr;
        // 핸들 몫의 참조는 이 함수를 나가며 놓인다 — 돌려주는 포인터 몫(짝 `release`)을 하나 더 센다. 핸들이 쥐고 있으므로 항목은 있다.
        const string             key = AssetRegistry::toCacheKey( relativePath );
        UniqueLock<shared_mutex> lock{ _impl->_mutex };
        const auto               it = _impl->_mapEntry.find( key );
        SW_ASSERT( it != _impl->_mapEntry.end() );
        ++it->second._refCount;
        return pTexture.get();
    }

    uint32 TextureCache::completePendingLoads( IRHIDevice* pDevice, uint32 maxCount )
    {
        // 업로드 전용 계측(렌더 스레드 업로드 판단 — docs/NEXT.md 측정 GPU 업로드): 펌프 CPU 시간, 프레임당 제출 · 폴링 · 대기 수.
        SW_PROFILE_SCOPE( "RT.TextureUpload.pump" );
        return processPendingLoads( pDevice, maxCount, false );
    }

    uint32 TextureCache::finishPendingLoads( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr )
            return 0;
        uint32 completedCount = 0;
        for ( ;; )
        {
            completedCount += processPendingLoads( pDevice, UINT32_MAX, true );
            ScopedLock<mutex> lock{ _impl->_awaitingMutex };
            if ( _impl->_listAwaitingUpload.empty() && _impl->_listUploading.empty() )
                return completedCount;
        }
    }

    uint32 TextureCache::processPendingLoads( IRHIDevice* pDevice, uint32 maxCount, bool bBlocking )
    {
        if ( pDevice == nullptr )
            return 0;
        // 0) 예약한 번호를 살린다 — 이번에 끝나는 로드가 그 번호를 다시 묶는다(R-6). 프레임 시작도 살리지만 차단 판(`finishPendingLoads`)은 프레임 밖에서도 돈다.
        pDevice->activateReservedBindlessTextures();
        // 1) 이미 제출한 로드의 끝남 확인 — 값싼 폴링이라 상한 없이 모두 한다. 끝나지 않은 로드 작업은 `onUploadSubmitted` 가 목록에 다시 넣는다.
        vector<shared_ptr<TextureLoadOperation>> listUploading;
        {
            ScopedLock<mutex> lock{ _impl->_awaitingMutex };
            listUploading.swap( _impl->_listUploading );
        }
        uint32 completedCount = 0;
        if ( bBlocking == false )
            SW_PROFILE_COUNT( "RT.TextureUpload.polled", listUploading.size() );
        for ( const shared_ptr<TextureLoadOperation>& pOperation : listUploading )
        {
            pOperation->_pDevice.store( pDevice, std::memory_order_release );
            // 다른 스레드의 `wait()` 가 이미 집었으면 건너뛴다(집은 쪽이 끝낸다).
            (void)pOperation->runUploadPollIfPending( bBlocking );
        }
        // 폴링이 끝낸 수는 로드 작업이 `Done` 이 된 것을 센다(아직 `Loading` 인 것은 목록에 남아 있다).
        for ( const shared_ptr<TextureLoadOperation>& pOperation : listUploading )
        {
            if ( pOperation->getState() != AssetLoadState::Loading )
                ++completedCount;
        }

        // 2) 새로 제출한다 — 프레임당 상한은 제출 수다.
        vector<shared_ptr<TextureLoadOperation>> listOperation;
        {
            ScopedLock<mutex> lock{ _impl->_awaitingMutex };
            const size_t      takeCount = MathUtil::min( static_cast<size_t>( maxCount ), _impl->_listAwaitingUpload.size() );
            if ( bBlocking == false )
                SW_PROFILE_COUNT( "RT.TextureUpload.waiting", _impl->_listAwaitingUpload.size() - takeCount ); // 프레임 상한에 밀려 다음 프레임으로 넘어간 수
            if ( takeCount == 0 )
                return completedCount;
            listOperation.assign( std::make_move_iterator( _impl->_listAwaitingUpload.begin() ), std::make_move_iterator( _impl->_listAwaitingUpload.begin() + static_cast<ptrdiff_t>( takeCount ) ) );
            _impl->_listAwaitingUpload.erase( _impl->_listAwaitingUpload.begin(), _impl->_listAwaitingUpload.begin() + static_cast<ptrdiff_t>( takeCount ) );
        }
        // 이미 다른 스레드가 기다리며(`wait`) 올렸으면 건너뛴다 — 완료 단계는 먼저 집은 쪽 하나만 실행한다.
        for ( size_t index = 0; index < listOperation.size(); ++index )
        {
            const shared_ptr<TextureLoadOperation>& pOperation = listOperation[index];
            pOperation->_pDevice.store( pDevice, std::memory_order_release );
            if ( pOperation->runCompletionIfPending() == false )
                continue;
            if ( bBlocking == false )
                SW_PROFILE_COUNT( "RT.TextureUpload.submitted", 1 );
            // `Busy`(스테이징이 참)면 로드 작업이 스스로 대기 목록으로 돌아갔다 — 이 프레임의 나머지도 같은 이유로 못 내므로 순서를 지켜 앞에 되돌려 놓고 멈춘다.
            if ( pOperation->isAwaitingCompletion() == false )
                continue;
            ScopedLock<mutex> lock{ _impl->_awaitingMutex };
            _impl->_listAwaitingUpload.insert( _impl->_listAwaitingUpload.begin(), std::make_move_iterator( listOperation.begin() + static_cast<ptrdiff_t>( index ) + 1 ), std::make_move_iterator( listOperation.end() ) );
            break;
        }
        return completedCount;
    }

    uint32 TextureCache::updateStreaming( IRHIDevice* pDevice, const TextureStreamingBudget& budget )
    {
        const uint32 maxCount = budget._maxUpgradesPerFrame;
        if ( pDevice == nullptr )
            return 0;
        uint32 upgradedCount = 0;
        // 항목의 올리는 중 표시를 내리고 결과를 반영한다 — 마지막 참조가 그사이 놓였으면 표에서 지우고 잠금 밖에서 내린다(`reload` 와 같다).
        const auto completeUpgrade = [this, pDevice, &upgradedCount]( const string& key, bool bUpgraded, const Texture2DRetiredResource& retired )
        {
            unique_ptr<Texture2D> pErased;
            {
                UniqueLock<shared_mutex> lock{ _impl->_mutex };
                const auto               it = _impl->_mapEntry.find( key );
                SW_ASSERT( it != _impl->_mapEntry.end() && it->second._bUpgrading );
                it->second._bUpgrading = false;
                if ( bUpgraded )
                {
                    _impl->_retired.add( pDevice, retired );
                    it->second._streamingReservation.commitChange(); // 새 크기가 옛 크기를 대신한다
                    it->second._framesSinceChange = 0;
                }
                else
                    it->second._streamingReservation.abandonChange(); // 옛 밉 그대로다 — 새 크기의 예약만 돌려주고 지금 크기는 그대로 센다
                if ( it->second._refCount == 0 && it->second._bPinned == false )
                {
                    pErased = std::move( it->second._texture );
                    _impl->_mapEntry.erase( it );
                }
            }
            if ( pErased != nullptr )
                pErased->releaseRHI( pDevice );
            // 밉을 올려도 SRV 번호는 그대로다(`rebindBindlessTexture` — R-6) — 머티리얼은 다시 묶지 않는다.
            if ( bUpgraded )
                ++upgradedCount;
            _impl->_cvLoaded.notify_all();
        };

        // 1) 이미 제출한 위쪽 밉 업로드의 끝남 확인 — 값싼 폴링이라 상한 없이 모두 한다(`finishUpgradeResidency`). 끝나지 않은 것은 줄에 남는다.
        vector<string> listInFlight;
        {
            ScopedLock<mutex> lock{ _impl->_streamingMutex };
            listInFlight.swap( _impl->_listUpgrading );
        }
        vector<string> listStillPending;
        for ( const string& key : listInFlight )
        {
            Texture2D* pTexture = nullptr;
            {
                ScopedSharedLock<shared_mutex> lock{ _impl->_mutex };
                const auto                     it = _impl->_mapEntry.find( key );
                if ( it == _impl->_mapEntry.end() || it->second._texture == nullptr ) // 캐시를 비웠다(종료)
                    continue;
                pTexture = it->second._texture.get(); // 올리는 중 표시가 항목을 지킨다
            }
            Texture2DRetiredResource    retired{};
            const Texture2DUploadStatus status = pTexture->finishUpgradeResidency( pDevice, budget._bFinishEachFrame, retired );
            if ( status == Texture2DUploadStatus::Pending )
            {
                listStillPending.push_back( key );
                continue;
            }
            completeUpgrade( key, status == Texture2DUploadStatus::Ready, retired );
        }
        if ( listStillPending.empty() == false )
        {
            ScopedLock<mutex> lock{ _impl->_streamingMutex };
            _impl->_listUpgrading.insert( _impl->_listUpgrading.end(), std::make_move_iterator( listStillPending.begin() ), std::make_move_iterator( listStillPending.end() ) );
        }
        if ( maxCount == 0 )
            return upgradedCount;

        SW_PROFILE_SCOPE( "RT.TextureStreaming" );
        SW_PROFILE_COUNT( "RT.TextureStreaming.poolBytes", _impl->_streamingPool.getUsedBytes() );

        // 1') 이번 프레임의 요구(화면 크기)를 항목에 적고, 정책(`TextureStreamingPolicy`)이 고른 내리기 · 다시 올리기를 줄 앞에 넣는다(R6-6).
        unordered_map<string, TextureStreamingDemand> mapFrameDemand;
        {
            ScopedLock<mutex> lock{ _impl->_streamingMutex };
            std::swap( mapFrameDemand, _impl->_mapStreamingDemand ); // sw::unordered_map 은 멤버 swap 이 없다 — 이동 생성 · 대입으로 바꾼다
        }
        unordered_map<string, uint8> mapDemotionKey; ///< 이번에 내리기로 한 키 — 줄에서 먼저 낸다(풀을 비워 올리기가 들어온다)
        if ( budget._viewHeightPixels > 0 )
            collectStreamingDecisions( mapFrameDemand, budget, mapDemotionKey );

        // 2) 다 읽은 밉 구간을 제출한다 — 프레임당 상한은 제출 수다. 읽는 중인 것은 줄에 남는다(MR-2).
        // 결정적 실행은 앞 프레임에 낸 읽기를 모두 기다린다 — 잠금 밖에서 기다린다(완료 콜백은 결과만 쓴다).
        if ( budget._bFinishEachFrame )
        {
            vector<AsyncReadHandle> listHandle;
            {
                ScopedLock<mutex> lock{ _impl->_streamingMutex };
                for ( const Impl::StreamingRead& reading : _impl->_listReading )
                    listHandle.push_back( reading._handle );
            }
            for ( const AsyncReadHandle& handle : listHandle )
                handle.wait();
        }
        vector<Impl::StreamingRead> listRead;
        {
            ScopedLock<mutex> lock{ _impl->_streamingMutex };
            SW_PROFILE_COUNT( "RT.TextureStreaming.bytesInFlight", _impl->_bytesInFlight );
            for ( size_t index = 0; index < _impl->_listReading.size(); )
            {
                if ( _impl->_listReading[index]._pResult->_state.load( std::memory_order_acquire ) == static_cast<uint8>( Impl::StreamingReadResult::State::Reading ) )
                {
                    ++index;
                    continue;
                }
                listRead.push_back( std::move( _impl->_listReading[index] ) );
                _impl->_listReading.erase( _impl->_listReading.begin() + static_cast<ptrdiff_t>( index ) );
            }
        }
        // 제출했거나 버린 구간의 바이트를 놓는다 — 읽는 중 · 제출 대기 바이트의 합만 상한에 센다.
        const auto releaseRead = [this]( Impl::StreamingRead& read )
        {
            read._pResult.reset();
            ScopedLock<mutex> lock{ _impl->_streamingMutex };
            _impl->_bytesInFlight -= MathUtil::min( _impl->_bytesInFlight, read._byteSize );
        };
        uint32 submitCount = 0;
        for ( size_t index = 0; index < listRead.size(); ++index )
        {
            Impl::StreamingRead&    read     = listRead[index];
            Texture2D*              pTexture = nullptr;
            const TextureMipLayout* pLayout  = nullptr;
            bool                    bDropped = false;
            {
                ScopedSharedLock<shared_mutex> lock{ _impl->_mutex };
                const auto                     it = _impl->_mapEntry.find( read._request._key );
                if ( it == _impl->_mapEntry.end() || it->second._texture == nullptr ) // 캐시를 비웠다(종료)
                    continue;
                // 올리는 중 표시가 항목을 지킨다. 그사이 마지막 참조가 놓였으면 올리지 않고 버린다 — 아래 마무리가 항목을 지운다.
                bDropped = it->second._refCount == 0 && it->second._bPinned == false;
                pTexture = it->second._texture.get();
                pLayout  = &it->second._mipLayout;
            }
            const bool bReadFailed = read._pResult->_state.load( std::memory_order_acquire ) == static_cast<uint8>( Impl::StreamingReadResult::State::Failed );
            if ( bReadFailed || bDropped )
            {
                if ( bReadFailed )
                    SW_LOG_ERROR_ID( "TextureCache.MipRangeReadFailed", "TextureCache: the top mips of '%#' could not be read - it keeps its lower mips", read._request._key.c_str() );
                releaseRead( read );
                completeUpgrade( read._request._key, false, Texture2DRetiredResource{} );
                continue;
            }
            const bool                bFrameFull = submitCount >= maxCount;
            const Texture2DUploadStep step       = bFrameFull ? Texture2DUploadStep::Busy : pTexture->beginUploadMips( pDevice, *pLayout, read._request._firstMip, read._pResult->_bytes, _impl->_qualityMipDrop.load( std::memory_order_relaxed ), false );
            if ( step == Texture2DUploadStep::Submitted )
            {
                ++submitCount;
                releaseRead( read ); // 스테이징이 복사했다
                ScopedLock<mutex> lock{ _impl->_streamingMutex };
                _impl->_listUpgrading.push_back( read._request._key );
                continue;
            }
            if ( step == Texture2DUploadStep::Busy )
            {
                // 프레임 상한 · 스테이징이 찼다 — 읽은 바이트를 쥔 채 나머지와 함께 줄로 되돌린다(다음 프레임에 다시 낸다, 다시 읽지 않는다).
                ScopedLock<mutex> lock{ _impl->_streamingMutex };
                _impl->_listReading.insert( _impl->_listReading.begin(), std::make_move_iterator( listRead.begin() + static_cast<ptrdiff_t>( index ) ), std::make_move_iterator( listRead.end() ) );
                break;
            }
            releaseRead( read ); // 실패했다(오류를 남겼다) — 아래쪽 밉 그대로 둔다
            completeUpgrade( read._request._key, false, Texture2DRetiredResource{} );
        }

        // 3) 새로 읽는다 — 프레임당 상한은 시작하는 읽기 수다. 읽는 중 · 제출 대기 바이트의 합을 `_maxBytesInFlight` 아래로 묶는다(하나가 상한보다 크면 혼자일 때만 읽는다).
        vector<TextureMipRequest> listRequest;
        {
            ScopedLock<mutex> lock{ _impl->_streamingMutex };
            const size_t      takeCount = MathUtil::min( static_cast<size_t>( maxCount ), _impl->_listStreamingUpgrade.size() );
            _impl->_bStreamingParked    = false;
            if ( takeCount == 0 )
                return upgradedCount;
            // 내리기를 먼저, 그다음 스트리밍 우선순위가 급한 것(셀 로드와 같은 값), 같으면 화면에서 큰 텍스처부터 올린다. 알리지 않은 것은 `Lowest` · 0 이고,
            // 같은 값끼리는 먼저 읽힌 순서를 지킨다(안정 정렬). 우선순위는 요청에 적어 둔다 — 아래 읽기의 IO 우선순위와 개발 도구(`captureStreamingQueue`)가 읽는다.
            if ( mapFrameDemand.empty() == false || mapDemotionKey.empty() == false )
            {
                for ( TextureMipRequest& request : _impl->_listStreamingUpgrade )
                {
                    const auto it     = mapFrameDemand.find( request._key );
                    request._priority = it != mapFrameDemand.end() ? it->second._priority : StreamingSourcePriority::Lowest;
                }
                const auto isDemotion = [&mapDemotionKey]( const TextureMipRequest& request )
                { return mapDemotionKey.find( request._key ) != mapDemotionKey.end(); };
                const auto getScreenSize = [&mapFrameDemand]( const TextureMipRequest& request )
                {
                    const auto it = mapFrameDemand.find( request._key );
                    return it != mapFrameDemand.end() ? it->second._screenSize : 0.0f;
                };
                VectorUtil::stableSort( _impl->_listStreamingUpgrade, [&isDemotion, &getScreenSize]( const TextureMipRequest& lhs, const TextureMipRequest& rhs )
                {
                    const bool bLhsDemotion = isDemotion( lhs );
                    const bool bRhsDemotion = isDemotion( rhs );
                    if ( bLhsDemotion != bRhsDemotion )
                        return bLhsDemotion;
                    if ( lhs._priority != rhs._priority )
                        return lhs._priority < rhs._priority;
                    return getScreenSize( lhs ) > getScreenSize( rhs );
                } );
            }
            listRequest.assign( std::make_move_iterator( _impl->_listStreamingUpgrade.begin() ),
                                std::make_move_iterator( _impl->_listStreamingUpgrade.begin() + static_cast<ptrdiff_t>( takeCount ) ) );
            _impl->_listStreamingUpgrade.erase( _impl->_listStreamingUpgrade.begin(), _impl->_listStreamingUpgrade.begin() + static_cast<ptrdiff_t>( takeCount ) );
        }
        // 줄 앞으로 되돌린다(이 요청부터 끝까지) — 순서를 지킨다.
        const auto restoreQueueFrom = [this, &listRequest]( size_t index, bool bSuspended )
        {
            ScopedLock<mutex> lock{ _impl->_streamingMutex };
            _impl->_bStreamingParked = bSuspended;
            _impl->_listStreamingUpgrade.insert( _impl->_listStreamingUpgrade.begin(), std::make_move_iterator( listRequest.begin() + static_cast<ptrdiff_t>( index ) ),
                                                 std::make_move_iterator( listRequest.end() ) );
        };
        for ( size_t index = 0; index < listRequest.size(); ++index )
        {
            const TextureMipRequest& request = listRequest[index];
            string                   readPath;
            uint64                   rangeOffset{ 0 };
            uint64                   rangeSize{ 0 };
            bool                     bOverBudget   = false;
            bool                     bOverInFlight = false;
            {
                UniqueLock<shared_mutex> lock{ _impl->_mutex };
                const auto               it = _impl->_mapEntry.find( request._key );
                // 놓였으면 버린다. 다시 읽는 중(`reload` · 디바이스 교체)이면 그쪽이 밉 전부를 올린다.
                const bool bReplaceable = it != _impl->_mapEntry.end() && it->second._bLoading == false && it->second._bUpgrading == false && it->second._bPublished &&
                                          it->second._texture != nullptr && it->second._texture->isRHIValid() && request._mipCount > 0 &&
                                          request._firstMip + request._mipCount == it->second._mipLayout._listMip.size() &&
                                          it->second._texture->getMipCount() <= it->second._mipLayout._listMip.size();
                if ( bReplaceable == false )
                    continue;
                // 지금 가장 큰 밉과 같으면 바꿀 것이 없다(그사이 다른 요청이 이미 그만큼 올렸거나 내렸다).
                const uint32 residentFirstMip = static_cast<uint32>( it->second._mipLayout._listMip.size() ) - it->second._texture->getMipCount();
                if ( request._firstMip == residentFirstMip )
                    continue;
                // 내리기는 작아지므로 예산을 보지 않는다 — 옛 크기의 예약과 함께 예산을 넘어 내리기가 막히지 않게(바꾸는 동안 두 크기를 함께 센다).
                const bool bShrink = request._firstMip > residentFirstMip;
                it->second._mipLayout.getRange( request._firstMip, request._mipCount, rangeOffset, rangeSize );
                {
                    ScopedLock<mutex> streamingLock{ _impl->_streamingMutex };
                    bOverInFlight = _impl->_bytesInFlight > 0 && _impl->_bytesInFlight + rangeSize > budget._maxBytesInFlight;
                }
                if ( bOverInFlight == false )
                {
                    // 풀 예산(`TextureStreamingConfig::_poolMegabytes`) — 넘으면 이 텍스처와 뒤의 것은 아래쪽 밉 그대로 줄에 남는다(화면에서 큰 것부터 받는다 — 작은 것이 건너뛰어 채우지 않는다).
                    optional<TextureStreamingReservation> reservation = _impl->_streamingPool.tryReserve( rangeSize, bShrink ? UINT64_MAX : budget._poolBytes );
                    bOverBudget                                       = reservation.has_value() == false;
                    if ( bOverBudget == false )
                    {
                        it->second._streamingReservation.beginChange( std::move( reservation.value() ) );
                        it->second._bUpgrading = true; // 읽는 동안 항목을 지킨다(놓여도 지우지 않는다) — 다시 읽기는 끝을 기다린다
                        readPath               = it->second._mipReadPath;
                    }
                }
            }
            if ( bOverInFlight || bOverBudget )
            {
                restoreQueueFrom( index, bOverBudget ); // 읽기 상한은 다음 프레임에 풀린다(쉬지 않는다). 풀 예산은 예산이 날 때까지 쉰다
                break;
            }
            // 읽기는 IO 스레드가 한다 — 완료는 결과에 바이트를 넣고 상태만 올린다. 렌더 스레드가 다음 프레임부터 본다.
            const shared_ptr<Impl::StreamingReadResult> pResult = sw::make_shared<Impl::StreamingReadResult>();
            const AsyncReadHandle                       handle  = AssetIO::readBinaryAssetRangeAsync( AssetID{ readPath }, rangeOffset, rangeSize, toAsyncIOPriority( request._priority ),
                                                                                                      SW_DELEGATE_LAMBDA( AssetReadCompleteDelegate, [pResult]( bool bSuccess, vector<uint8>& bytes )
                                   {
                if ( bSuccess )
                    pResult->_bytes.swap( bytes );
                const Impl::StreamingReadResult::State state = bSuccess ? Impl::StreamingReadResult::State::Read : Impl::StreamingReadResult::State::Failed;
                pResult->_state.store( static_cast<uint8>( state ), std::memory_order_release );
            } ) );
            if ( handle.isValid() == false )
            {
                SW_LOG_ERROR_ID( "TextureCache.MipRangeReadNotStarted", "TextureCache: the top-mip range of '%#' (%# bytes at %#) could not be read from '%#' - it keeps its lower mips",
                                 request._key.c_str(), rangeSize, rangeOffset, readPath.c_str() );
                completeUpgrade( request._key, false, Texture2DRetiredResource{} );
                continue;
            }
            ScopedLock<mutex> lock{ _impl->_streamingMutex };
            _impl->_bytesInFlight += rangeSize;
            _impl->_listReading.push_back( Impl::StreamingRead{ request, rangeSize, pResult, handle } );
        }
        return upgradedCount;
    }

    void TextureCache::collectStreamingDecisions( const unordered_map<string, TextureStreamingDemand>& mapFrameDemand, const TextureStreamingBudget& budget,
                                                  unordered_map<string, uint8>& outMapDemotionKey )
    {
        // 화면 크기 값은 (화면 크기 × UV 밀도 / 반지름) — 화면에서 UV 한 단위가 덮는 높이 비율의 두 배다(`TextureStreamingSystem`). 텍스처 가로 한 벌이 UV 한 단위이므로
        // 그 비율 × 뷰 높이가 텍스처 가로에 필요한 텍셀 수다(언리얼 wanted mip 의 화면 텍셀 크기).
        constexpr float32                 kScreenSizeToUVHeightFraction = 0.5f;
        const float32                     viewHeight                    = static_cast<float32>( budget._viewHeightPixels );
        vector<TextureStreamingCandidate> listCandidate;
        vector<const string*>             listKey;
        vector<uint8>                     listDemoted;
        vector<TextureMipRequest>         listRestore; ///< 내렸다가 `NeverStream` 이 된 텍스처의 되올리기
        uint64                            residentBytes = 0;
        // 스트리밍 상태(HUD `streaming` · 렌더 디버거 창): 스트리밍하는 텍스처 수, 원하는 밉보다 거친 밉만 올라온 수와 그 밉 차의 합(요청 밉 대 올라온 밉).
        uint32      streamingTextureCount = 0;
        uint32      belowWantedCount      = 0;
        uint32      missingMipCount       = 0;
        const int32 qualityMipDrop        = _impl->_qualityMipDrop.load( std::memory_order_relaxed ); // 이 차례 동안 같은 품질로 판정한다
        {
            UniqueLock<shared_mutex> lock{ _impl->_mutex };
            for ( auto& [key, entry] : _impl->_mapEntry )
            {
                const bool bResident = entry._texture != nullptr && entry._mipLayout._listMip.empty() == false && entry._bPublished && entry._bLoading == false &&
                                       entry._texture->isRHIValid() && entry._texture->getMipCount() <= entry._mipLayout._listMip.size();
                if ( bResident == false )
                    continue;
                if ( entry._streamingGroup == TextureStreamingGroup::NeverStream )
                {
                    // 내리기 대상이 아니다. 내린 뒤 밉 전부가 필요한 요청이 왔으면 품질 한도까지 되올린다.
                    if ( entry._bDemoted && entry._bUpgrading == false )
                    {
                        const uint32 qualityFirstMip = Texture2D::computeUpgradeFirstMip( entry._mipLayout, qualityMipDrop );
                        listRestore.push_back( TextureMipRequest{ key, qualityFirstMip, static_cast<uint32>( entry._mipLayout._listMip.size() ) - qualityFirstMip, StreamingSourcePriority::Lowest } );
                    }
                    continue;
                }
                const uint32                      mipCount = static_cast<uint32>( entry._mipLayout._listMip.size() );
                const TextureMipLayout::MipRange& top      = entry._mipLayout._listMip.front();
                const auto                        it       = mapFrameDemand.find( key );
                if ( it != mapFrameDemand.end() )
                {
                    entry._idleFrames     = 0;
                    entry._wantedFirstMip = TextureStreamingPolicy::computeWantedFirstMip( top._width, mipCount, it->second._screenSize * kScreenSizeToUVHeightFraction * viewHeight );
                }
                else if ( entry._idleFrames < UINT32_MAX )
                {
                    ++entry._idleFrames;
                }
                if ( entry._framesSinceChange < UINT32_MAX )
                    ++entry._framesSinceChange;
                // 품질이 버리는 밉 위로는 올리지 않는다 — 정책도 그 아래만 본다.
                const uint32              qualityFirstMip = Texture2D::computeUpgradeFirstMip( entry._mipLayout, qualityMipDrop );
                TextureStreamingCandidate candidate;
                candidate._mipCount          = mipCount;
                candidate._residentFirstMip  = mipCount - entry._texture->getMipCount();
                candidate._wantedFirstMip    = MathUtil::max( entry._wantedFirstMip, qualityFirstMip );
                candidate._idleFrames        = entry._idleFrames;
                candidate._framesSinceChange = entry._framesSinceChange;
                candidate._topMipBytes       = top._size;
                residentBytes += candidate.computeResidentBytes( candidate._residentFirstMip );
                ++streamingTextureCount;
                if ( candidate._residentFirstMip > candidate._wantedFirstMip )
                {
                    ++belowWantedCount;
                    missingMipCount += candidate._residentFirstMip - candidate._wantedFirstMip;
                }
                if ( entry._bUpgrading )
                    continue; // 바꾸는 중 — 바이트는 세고 이번 결정에서는 뺀다
                listCandidate.push_back( candidate );
                listKey.push_back( &key );
                listDemoted.push_back( entry._bDemoted ? SW_TRUE : SW_FALSE );
            }
        }
        TextureStreamingConfig config{};
        config._idleFramesBeforeRelease    = budget._idleFramesBeforeRelease;
        config._minFramesBetweenChanges    = budget._minFramesBetweenChanges;
        config._promoteBudgetMarginPercent = budget._promoteBudgetMarginPercent;
        vector<TextureStreamingDecision> listDecision;
        TextureStreamingPolicy::decide( vector_reference<const TextureStreamingCandidate>{ listCandidate }, residentBytes, budget._poolBytes, config, listDecision );
        SW_PROFILE_COUNT( "RT.TextureStreaming.residentBytes", residentBytes );
        SW_PROFILE_COUNT( "RT.TextureStreaming.textures", streamingTextureCount );
        SW_PROFILE_COUNT( "RT.TextureStreaming.belowWanted", belowWantedCount );
        SW_PROFILE_COUNT( "RT.TextureStreaming.missingMips", missingMipCount );
        if ( listDecision.empty() && listRestore.empty() )
            return;

        // 줄에 이미 있는 키는 다시 넣지 않는다(처음 올리기 · 앞서 넣은 결정).
        vector<TextureMipRequest> listNew;
        vector<string>            listDemotedKey;
        vector<string>            listPromotedKey;
        {
            ScopedLock<mutex>            lock{ _impl->_streamingMutex };
            unordered_map<string, uint8> mapQueued;
            for ( const TextureMipRequest& request : _impl->_listStreamingUpgrade )
                mapQueued[request._key] = SW_TRUE;
            for ( const TextureStreamingDecision& decision : listDecision )
            {
                const TextureStreamingCandidate& candidate = listCandidate[decision._candidateIndex];
                const string&                    key       = *listKey[decision._candidateIndex];
                const bool                       bDemote   = decision._targetFirstMip > candidate._residentFirstMip;
                // 처음 올리기는 로드가 줄에 넣었다 — 정책이 다시 올리는 것은 예산 때문에 내린 텍스처뿐이다.
                const bool bSkip = mapQueued.find( key ) != mapQueued.end() || ( bDemote == false && listDemoted[decision._candidateIndex] == SW_FALSE );
                if ( bSkip )
                    continue;
                listNew.push_back( TextureMipRequest{ key, decision._targetFirstMip, candidate._mipCount - decision._targetFirstMip, StreamingSourcePriority::Lowest } );
                if ( bDemote )
                {
                    outMapDemotionKey[key] = SW_TRUE;
                    listDemotedKey.push_back( key );
                }
                else
                {
                    listPromotedKey.push_back( key );
                }
            }
            for ( const TextureMipRequest& restore : listRestore )
            {
                if ( mapQueued.find( restore._key ) != mapQueued.end() )
                    continue;
                listNew.push_back( restore );
                listPromotedKey.push_back( restore._key );
            }
            _impl->_listStreamingUpgrade.insert( _impl->_listStreamingUpgrade.begin(), listNew.begin(), listNew.end() );
            if ( listNew.empty() == false )
                _impl->_bStreamingParked = false; // 내리기가 풀을 비운다 — 쉬던 줄도 다시 본다
        }
        UniqueLock<shared_mutex> lock{ _impl->_mutex };
        for ( const string& key : listDemotedKey )
        {
            const auto it = _impl->_mapEntry.find( key );
            if ( it != _impl->_mapEntry.end() )
                it->second._bDemoted = true;
        }
        for ( const string& key : listPromotedKey )
        {
            const auto it = _impl->_mapEntry.find( key );
            if ( it != _impl->_mapEntry.end() )
                it->second._bDemoted = false;
        }
    }

    void TextureCache::recordStreamingDemand( const vector<string>& listRelativePath, const TextureStreamingDemand& demand )
    {
        if ( demand._screenSize <= 0.0f )
            return;
        ScopedLock<mutex> lock{ _impl->_streamingMutex };
        for ( const string& relativePath : listRelativePath )
        {
            const auto [it, bInserted] = _impl->_mapStreamingDemand.emplace( AssetRegistry::toCacheKey( relativePath ), demand );
            if ( bInserted == false )
                it->second.merge( demand );
        }
    }

    TextureStreamingQueueSnapshot TextureCache::captureStreamingQueue() const
    {
        TextureStreamingQueueSnapshot snapshot;
        ScopedLock<mutex>             lock{ _impl->_streamingMutex };
        snapshot._listWaiting    = _impl->_listStreamingUpgrade;
        snapshot._readingCount   = static_cast<uint32>( _impl->_listReading.size() );
        snapshot._uploadingCount = static_cast<uint32>( _impl->_listUpgrading.size() );
        return snapshot;
    }

    bool TextureCache::hasPendingStreamingUpgrades() const
    {
        ScopedLock<mutex> lock{ _impl->_streamingMutex };
        return _impl->_listStreamingUpgrade.empty() == false || _impl->_listReading.empty() == false || _impl->_listUpgrading.empty() == false;
    }

    void TextureCache::releaseRetiredTextures( IRHIDevice* pDevice )
    {
        if ( pDevice != nullptr )
            _impl->_retired.releaseForDevice( pDevice );
    }

    uint32 TextureCache::countLoadsInFlight() const
    {
        uint32 count = 0;
        {
            // 풀 예산에 막혀 쉬는 줄은 진행 중이 아니다 — 세면 예산을 넘는 장면에서 기다리는 쪽(시나리오의 스크린샷)이 끝나지 않는다.
            ScopedLock<mutex> lock{ _impl->_streamingMutex };
            if ( _impl->_bStreamingParked == false )
                count += static_cast<uint32>( _impl->_listStreamingUpgrade.size() );
        }
        ScopedSharedLock<shared_mutex> lock{ _impl->_mutex };
        for ( const auto& [key, entry] : _impl->_mapEntry )
        {
            if ( entry._bLoading || entry._bUpgrading )
                ++count;
        }
        return count;
    }

    bool TextureCache::isRenderTargetPath( string_view relativePath )
    {
        return StringUtil::startsWith( relativePath, string_view{ kRenderTargetPrefix }, true );
    }

    bool TextureCache::isCPUTexturePath( string_view relativePath )
    {
        return StringUtil::startsWith( relativePath, string_view{ kCPUTexturePrefix }, true );
    }

    string TextureCache::makeUniqueCPUTexturePath( string_view stem )
    {
        StringBuilder<constant::buffer::kMax256> path;
        path.appendFormat( "%#%#%#", kCPUTexturePrefix, stem, _cpuTextureSerial.fetch_add( 1u, std::memory_order_relaxed ) );
        return string( path.view() );
    }

    bool TextureCache::createCPUTexture( string_view relativePath, uint32 width, uint32 height, vector<uint8> rgbaBytes )
    {
        const bool bSizeMatches  = width > 0 && height > 0 && rgbaBytes.size() == static_cast<size_t>( width ) * height * 4;
        const bool bValidRequest = isCPUTexturePath( relativePath ) && bSizeMatches;
        if ( _impl == nullptr || bValidRequest == false )
            return false;
        const string             key = AssetRegistry::toCacheKey( relativePath );
        UniqueLock<shared_mutex> lock{ _impl->_mutex };
        if ( _impl->_mapEntry.find( key ) != _impl->_mapEntry.end() )
            return false;
        Impl::Entry& entry = _impl->_mapEntry[key];
        entry._texture     = make_unique<Texture2D>();
        entry._texture->setCPUBytes( key, width, height, std::move( rgbaBytes ) );
        entry._bPublished = true;
        entry._bPinned    = true;
        return true;
    }

    void TextureCache::releaseCPUTexture( string_view relativePath )
    {
        if ( _impl == nullptr || isCPUTexturePath( relativePath ) == false )
            return;
        const string             key = AssetRegistry::toCacheKey( relativePath );
        unique_ptr<Texture2D>    pErased; // 픽셀 바이트(수 MB)는 잠금을 놓은 뒤 푼다
        UniqueLock<shared_mutex> lock{ _impl->_mutex };
        const auto               it = _impl->_mapEntry.find( key );
        if ( it == _impl->_mapEntry.end() )
            return;
        it->second._bPinned = false;
        // 참조가 0 이면 GPU 자원도 없다(`release` 가 내렸고 `initRHI` 는 CPU 텍스처를 올리지 않는다).
        if ( it->second._refCount == 0 && it->second._bLoading == false )
        {
            pErased = std::move( it->second._texture );
            _impl->_mapEntry.erase( it );
        }
    }

    void TextureCache::declareRenderTarget( string_view relativePath, uint32 width, uint32 height )
    {
        const bool bInvalidDeclaration = _impl == nullptr || isRenderTargetPath( relativePath ) == false || width == 0 || height == 0;
        if ( bInvalidDeclaration )
            return;
        const string             key = AssetRegistry::toCacheKey( relativePath );
        UniqueLock<shared_mutex> lock{ _impl->_mutex };
        Impl::RenderTargetSize&  size = _impl->_mapRenderTargetSize[key];
        size._width                   = width;
        size._height                  = height;
    }

    bool TextureCache::resizeRenderTarget( string_view relativePath, uint32 width, uint32 height, IRHIDevice* pDevice )
    {
        const ScopedMemoryTag memoryScope{ kMemoryTag };
        const bool            bInvalidRenderTargetRequest = _impl == nullptr || pDevice == nullptr || isRenderTargetPath( relativePath ) == false || width == 0 || height == 0;
        if ( bInvalidRenderTargetRequest )
            return false;
        const string key = AssetRegistry::toCacheKey( relativePath );

        // 크기를 먼저 알린다 — 아직 만들지 않았으면 다음 acquire 가 이 크기로 만든다. 다시 만드는 일은 reload 와 같이 잠금 밖에서 한다.
        UniqueLock<shared_mutex> lock{ _impl->_mutex };
        Impl::RenderTargetSize&  size = _impl->_mapRenderTargetSize[key];
        size._width                   = width;
        size._height                  = height;
        Texture2D* pTexture           = nullptr;
        for ( ;; )
        {
            const auto it = _impl->_mapEntry.find( key );
            if ( it == _impl->_mapEntry.end() || it->second._texture == nullptr )
                return false;
            if ( it->second._bLoading )
            {
                _impl->_cvLoaded.wait( lock );
                continue;
            }
            const Texture2D& existing              = *it->second._texture;
            const bool       bRenderTargetUpToDate = existing.isRHIValid() && existing.getWidth() == width && existing.getHeight() == height;
            if ( bRenderTargetUpToDate )
                return false;
            it->second._bLoading = true;
            pTexture             = it->second._texture.get();
            break;
        }
        lock.unlock();

        // 이전 프레임이 옛 텍스처를 읽고 있을 수 있다. 기다리지 않고 옛 텍스처를 해제 줄에 넘긴다(RHIAsyncUploads U13 — `waitIdle` 을 지웠다): 게임 스레드가
        // `releaseRetiredTextures` 로 `releaseHandle` 하면 이미 만든 패킷이 끝난 뒤에 돌아간다(스트리밍 교체와 같은 길). SRV 번호는 지켜 새 텍스처로 다시 묶는다 —
        // 번호의 디스크립터는 고리 칸마다 따로라 이전 프레임의 칸은 옛 텍스처를 가리킨 채 끝난다(R-6).
        Texture2DRetiredResource retired{};
        pTexture->detachRHIKeepingSRV( pDevice, retired ); // 새 렌더 타깃이 같은 SRV 번호를 받는다(R-6)
        const bool bReady = pTexture->createRenderTarget( pDevice, key, width, height, constant::render::kOffscreenColorFormat );
        if ( bReady == false )
        {
            SW_LOG_ERROR_ID( "TextureCache.FailedResizeRenderTexture", "Failed to resize render texture %# to %#x%#", key.c_str(), width, height );
            // 지키던 번호는 해제 줄로 간 옛 타깃을 가리킨다 — 누락 체커로 다시 묶는다(그 번호를 쥔 머티리얼이 해제된 텍스처를 그리지 않게). 텍스처는 계속 그 번호를 지킨다.
            const RHITextureSRVIndex kept = pTexture->getKeptSRV();
            if ( kept != kInvalidDescriptorIndex && _impl->pointAtMissingChecker( *pDevice, kept, key ) == false )
                (void)pTexture->takeKeptSRV(); // 놓았다
        }

        lock.lock();
        const auto            it = _impl->_mapEntry.find( key );
        unique_ptr<Texture2D> pErased;
        SW_ASSERT( it != _impl->_mapEntry.end() && it->second._bLoading );
        it->second._bLoading = false;
        _impl->_retired.add( pDevice, retired );
        if ( it->second._refCount == 0 )
        {
            pErased = std::move( it->second._texture );
            _impl->_mapEntry.erase( it );
        }
        lock.unlock();
        if ( pErased != nullptr )
            pErased->releaseRHI( pDevice ); // 장치 호출은 잠금 밖에서(잠금은 잎 잠금, 결정 5-3) — 지키던 번호도 함께 놓는다
        _impl->_cvLoaded.notify_all();
        return bReady;
    }

    const Texture2D* TextureCache::find( string_view relativePath ) const
    {
        if ( relativePath.empty() || _impl == nullptr )
            return nullptr;
        const string                   key = AssetRegistry::toCacheKey( relativePath );
        ScopedSharedLock<shared_mutex> lock{ _impl->_mutex };
        const auto                     it = _impl->_mapEntry.find( key );
        // 올리는 중(처음 acquire · reload)인 항목은 내주지 않는다 — 그 스레드가 잠금 없이 같은 객체의 핸들 · SRV 를 바꾸고 있다.
        const bool bReady = it != _impl->_mapEntry.end() && it->second._bPublished && it->second._bLoading == false;
        if ( bReady == false )
            return nullptr;
        return it->second._texture.get();
    }

    void TextureCache::requestRegionUpdate( string_view relativePath, uint32 x, uint32 y, uint32 width, uint32 height, vector<uint8> rgbaBytes )
    {
        const bool bEmpty = relativePath.empty() || width == 0 || height == 0;
        if ( _impl == nullptr || bEmpty )
            return;
        Impl::RegionUpdate update;
        update._key       = AssetRegistry::toCacheKey( relativePath );
        update._x         = x;
        update._y         = y;
        update._width     = width;
        update._height    = height;
        update._rgbaBytes = std::move( rgbaBytes );
        ScopedLock<mutex> lock{ _impl->_regionMutex };
        _impl->_listPendingRegion.push_back( std::move( update ) );
    }

    uint32 TextureCache::flushRegionUpdates( IRHIDevice* pDevice )
    {
        if ( _impl == nullptr )
            return 0;
        {
            ScopedLock<mutex> lock{ _impl->_regionMutex };
            if ( _impl->_listPendingRegion.empty() )
                return 0;
            _impl->_listFlushingRegion.swap( _impl->_listPendingRegion );
        }
        // 프레임 경로라 GPU 완료를 기다리지 않는다(`...AndWait` 를 부르면 Debug 단언). 등록된 텍스처의 구간 갱신이므로 제출만 하면 된다 — 같은 텍스처의 전송은
        // 낸 순서대로, 뒤에 낼 프레임 커맨드보다 먼저 실행된다(전송 계약 3).
        // 실패 규칙: `Busy` 가 한 번 나면 이번 차례의 나머지는 모두 다음 프레임으로 미룬다(뒤의 갱신이 먼저 들어가 같은 자리를 옛 내용으로 덮지 않게 순서를 지킨다).
        // `Rejected` 는 다시 내도 안 되는 요청이라 오류를 남기고 버린다.
        const RHITransferUtil::ScopedForbidBlockingTransfer forbidBlocking;
        uint32                                              uploadedCount{ 0 };
        bool                                                bBusy{ false };
        vector<Impl::RegionUpdate>                          listRetry;
        for ( Impl::RegionUpdate& update : _impl->_listFlushingRegion )
        {
            if ( bBusy )
            {
                listRetry.push_back( std::move( update ) );
                continue;
            }
            switch ( _impl->submitRegionUpdate( update, pDevice ) )
            {
                case Impl::RegionFlushResult::Uploaded:
                    ++uploadedCount;
                    break;
                case Impl::RegionFlushResult::RetryBusy:
                    bBusy = true;
                    listRetry.push_back( std::move( update ) );
                    break;
                case Impl::RegionFlushResult::Retry:
                    listRetry.push_back( std::move( update ) );
                    break;
                case Impl::RegionFlushResult::Dropped:
                    break;
            }
        }
        _impl->_listFlushingRegion.clear();
        if ( listRetry.empty() == false )
        {
            // 미룬 것은 새로 들어온 것보다 앞에 둔다 — 같은 자리를 다시 칠했으면 나중 것이 이긴다.
            ScopedLock<mutex> lock{ _impl->_regionMutex };
            listRetry.insert( listRetry.end(), std::make_move_iterator( _impl->_listPendingRegion.begin() ), std::make_move_iterator( _impl->_listPendingRegion.end() ) );
            _impl->_listPendingRegion.swap( listRetry );
        }
        return uploadedCount;
    }
    void TextureCache::requestLayerBake( TextureLayerBakeRequest request )
    {
        if ( _impl == nullptr || request._albedoTarget.empty() )
            return;
        ScopedLock<mutex> lock{ _impl->_bakeMutex };
        for ( TextureLayerBakeRequest& pending : _impl->_listPendingBake )
        {
            if ( pending._albedoTarget == request._albedoTarget )
            {
                pending = std::move( request ); // 같은 결과의 앞 요청은 낡았다
                return;
            }
        }
        _impl->_listPendingBake.push_back( std::move( request ) );
    }

    void TextureCache::deferLayerBake( TextureLayerBakeRequest request )
    {
        if ( _impl == nullptr || request._albedoTarget.empty() )
            return;
        ScopedLock<mutex> lock{ _impl->_bakeMutex };
        for ( const TextureLayerBakeRequest& pending : _impl->_listPendingBake )
        {
            if ( pending._albedoTarget == request._albedoTarget )
                return; // 그 사이 게임 스레드가 새 요청을 냈다 — 미룬 옛 요청은 버린다
        }
        _impl->_listPendingBake.push_back( std::move( request ) );
    }

    void TextureCache::takeLayerBakes( vector<TextureLayerBakeRequest>& outListRequest )
    {
        outListRequest.clear();
        if ( _impl == nullptr )
            return;
        ScopedLock<mutex> lock{ _impl->_bakeMutex };
        outListRequest.swap( _impl->_listPendingBake );
    }

    void TextureCache::recordLayerBaked( const string& albedoTarget )
    {
        if ( _impl == nullptr )
            return;
        ScopedLock<mutex> lock{ _impl->_bakeMutex };
        ++_impl->_bakeSerial;
        _impl->_listBakedTarget.push_back( pair<string, uint32>{ albedoTarget, _impl->_bakeSerial } );
        if ( _impl->_listBakedTarget.size() > kBakeHistoryCount )
            _impl->_listBakedTarget.erase( _impl->_listBakedTarget.begin() );
    }

    uint32 TextureCache::getLayerBakeSerial() const
    {
        if ( _impl == nullptr )
            return 0;
        ScopedLock<mutex> lock{ _impl->_bakeMutex };
        return _impl->_bakeSerial;
    }

    bool TextureCache::wasLayerBakedAfter( string_view albedoTarget, uint32 serial ) const
    {
        if ( _impl == nullptr )
            return false;
        ScopedLock<mutex> lock{ _impl->_bakeMutex };
        for ( const pair<string, uint32>& baked : _impl->_listBakedTarget )
        {
            if ( baked.second > serial && StringUtil::equals( baked.first, albedoTarget ) )
                return true;
        }
        return false;
    }

    void TextureCache::release( string_view relativePath )
    {
        if ( relativePath.empty() || _impl == nullptr )
            return;
        _impl->releaseReference( AssetRegistry::toCacheKey( relativePath ), _impl->_pBoundDevice.load( std::memory_order_acquire ) );
    }

    bool TextureCache::isCached( string_view relativePath ) const
    {
        if ( relativePath.empty() || _impl == nullptr )
            return false;

        const string                   key = AssetRegistry::toCacheKey( relativePath );
        ScopedSharedLock<shared_mutex> lock{ _impl->_mutex };
        return _impl->_mapEntry.find( key ) != _impl->_mapEntry.end();
    }

    size_t TextureCache::getCachedCount() const
    {
        if ( _impl == nullptr )
            return 0;
        ScopedSharedLock<shared_mutex> lock{ _impl->_mutex };
        return _impl->_mapEntry.size();
    }

    void TextureCache::collectMemoryEntries( vector<AssetMemoryEntry>& outListEntry ) const
    {
        if ( _impl == nullptr )
            return;
        ScopedSharedLock<shared_mutex> lock{ _impl->_mutex };
        for ( const auto& [key, entry] : _impl->_mapEntry )
        {
            const Texture2D* pTexture = entry._texture.get();
            if ( pTexture == nullptr )
                continue;
            // GPU 몫은 서술로 계산한 논리 크기다 — 드라이버 할당 크기는 GPU 자원 표가 핸들로 보인다.
            RHITextureDesc desc{};
            desc._width           = pTexture->getWidth();
            desc._height          = pTexture->getHeight();
            desc._format          = pTexture->getFormat();
            desc._mipLevels       = pTexture->getMipCount() > 0 ? pTexture->getMipCount() : 1u;
            const uint64 gpuBytes = pTexture->getHandle() != 0 ? RHIMemoryTracker::computeTextureLogicalBytes( desc ) : 0;
            const uint64 cpuBytes = sizeof( Texture2D ) + pTexture->getCPUBytes().size();
            outListEntry.push_back( AssetMemoryEntry{ key, cpuBytes, gpuBytes, entry._refCount } );
        }
    }

    /**
     * @brief 표를 통째로 비웁니다. **GPU 자원을 돌려주지는 않습니다.**
     * @warning **디바이스가 죽은 뒤에만 부를 수 있습니다.** `AssetCacheBase::clear()` 는 디바이스를
     *          인자로 받지 않고(그 이유는 `MaterialCache.h` 머리말에 있습니다. 캐시가 디바이스를
     *          들고 있으면 백엔드 교체 때 죽은 포인터가 됩니다), 캐시도 들고 있지 않으므로 여기서
     *          `releaseRHI` 를 부를 방법이 없습니다. 살아 있는 디바이스에서 부르면 텍스처 핸들과
     *          **bindless SRV 인덱스**가 그대로 샙니다. 후자는 프리리스트로 영영 안 돌아옵니다.
     *
     *          지금 이 함수로 오는 길은 하나뿐이고 그 순서가 이 계약을 지킵니다:
     *          `Engine::shutdown` 이 `_rhi->shutdown()` 을 **먼저** 부르고, 그것이
     *          `RHIRenderResource` 레지스트리 전체에 `releaseRHI` 를 밀어 둔 뒤에야
     *          `AssetManager::shutdown` → `clearAssetCaches()` 가 여기에 닿습니다.
     *          참조가 0 이 되어 내리는 평소 경로는 `release()` 이고, 그쪽은 제대로 돌려줍니다.
     */
    void TextureCache::clear()
    {
        if ( _impl == nullptr )
            return;
        {
            ScopedLock<mutex> awaitingLock{ _impl->_awaitingMutex };
            _impl->_listAwaitingUpload.clear(); // 올리지 않은 로드는 버린다 — 그 로드 작업을 쥔 핸들은 `Loading` 으로 남는다(종료 때만 온다)
            _impl->_listUploading.clear();      // 제출만 하고 끝을 확인하지 못한 로드도 같다(GPU 자원은 호출 전에 해제되어 있다)
        }
        {
            ScopedLock<mutex> streamingLock{ _impl->_streamingMutex };
            _impl->_listStreamingUpgrade.clear(); // 위쪽 밉을 기다리던 텍스처도 함께 사라진다
            _impl->_listUpgrading.clear();        // 위쪽 밉을 올리는 중이던 것도 같다
            _impl->_listReading.clear();          // 읽는 중인 구간은 IO 완료가 결과에만 쓴다(결과는 공유 포인터라 남아 있다)
            _impl->_bytesInFlight = 0;
        }
        {
            ScopedLock<mutex> standInLock{ _impl->_standInMutex };
            _impl->_pLoadingStandIn.store( nullptr, std::memory_order_release ); // 항목과 함께 사라진다
            _impl->_pMissingStandIn.store( nullptr, std::memory_order_release );
        }
        UniqueLock<shared_mutex> lock{ _impl->_mutex };
        _impl->_mapEntry.clear();
        getTextureCacheStats().recordClear();
    }
} // namespace sw

namespace sw
{
    TextureLoadOperation::TextureLoadOperation( string_view key, IRHIDevice* pDevice, Source source, Texture2D* pTexture, weak_ptr<TextureCache::Impl> pCache )
        : AssetLoadOperation{ TextureCache::kAssetKindName, key, MemoryTag::Texture }
        , _pImage{}
        , _readPath{}
        , _mipLayout{}
        , _tailBytes{}
        , _tailFirstMip{ 0 }
        , _loadError{}
        , _pCache{ std::move( pCache ) }
        , _pDevice{ pDevice }
        , _pTexture{ pTexture }
        , _renderTargetWidth{ TextureCache::kDefaultRenderTargetSize }
        , _renderTargetHeight{ TextureCache::kDefaultRenderTargetSize }
        , _source{ source }
        , _residency{ TextureResidency::Full }
        , _failedSRV{ kInvalidDescriptorIndex }
        , _pFailedSRVDevice{ nullptr }
        , _bHoldsReference{ SW_FALSE }
    {
    }

    TextureLoadOperation::~TextureLoadOperation()
    {
        // 캐시가 먼저 사라졌으면(종료) 놓을 항목 · 번호가 없다.
        const shared_ptr<TextureCache::Impl> pCache = _pCache.lock();
        if ( pCache == nullptr )
            return;
        // 실패한 로드가 맡은 번호(누락 체커)를 놓는다 — 그 번호를 받은 디바이스가 아직 붙어 있을 때만(바뀌었으면 옛 디바이스와 함께 갔다).
        const RHITextureSRVIndex failedSRV        = _failedSRV.load( std::memory_order_acquire );
        IRHIDevice* const        pFailedSRVDevice = _pFailedSRVDevice.load( std::memory_order_acquire );
        const bool               bReleaseFailed   = failedSRV != kInvalidDescriptorIndex && pFailedSRVDevice != nullptr &&
                                    pFailedSRVDevice == pCache->_pBoundDevice.load( std::memory_order_acquire );
        if ( bReleaseFailed )
            pFailedSRVDevice->releaseBindlessIndex( failedSRV );
        if ( _bHoldsReference )
            pCache->releaseReference( getPath(), _pDevice.load( std::memory_order_acquire ) );
    }

    RHITextureSRVIndex TextureLoadOperation::getStableSRV() const
    {
        const RHITextureSRVIndex failedSRV = _failedSRV.load( std::memory_order_acquire );
        if ( failedSRV != kInvalidDescriptorIndex )
            return failedSRV;
        // 실패해 번호를 맡기 전이면 텍스처가 지키는 번호다 — 맡을 때 이 값을 먼저 적고 텍스처의 번호를 지우므로 그 사이에 빈 번호를 보지 않는다.
        return _pTexture != nullptr ? _pTexture->getStableSRV() : kInvalidDescriptorIndex;
    }

    shared_ptr<Texture2D> TextureLoadOperation::getAsset() const
    {
        // 별칭 shared_ptr — 로드 작업(곧 참조)을 쥔 채 텍스처를 가리킨다. 텍스처 객체는 항목이 소유한다.
        return shared_ptr<Texture2D>( shared_from_this(), _pTexture );
    }

    uint64 TextureLoadOperation::getResidentIdentity() const
    {
        return getState() == AssetLoadState::Ready && _pTexture != nullptr ? _pTexture->getHandle().getPacked() : 0u;
    }

    expected<void> TextureLoadOperation::runLoad()
    {
        // 렌더 타깃 · CPU 텍스처는 읽을 파일이 없다 — 업로드 단계만 있다.
        if ( _source != Source::File )
            return {};
        // 읽기와 머리 풀기는 한 함수라 `IO` 하나에 적는다(`AssetLoadPhase` 규칙). 올리기는 완료 단계의 따로 된 기록(`TextureUpload`)이다.
        AssetLoadScope loadScope( "Texture", getPath() );
        _readPath = Texture2D::findReadPath( AssetID{ getPath() } ); // 위쪽 밉을 범위로 다시 읽을 파일(MR-2)
        // 스트리밍 로드는 머리와 꼬리 밉 구간만 읽는다(MR-3) — 위쪽 밉은 올리기 줄이 뒤에 범위로 읽는다. 동기 로드(도구 · 미리보기)와 밉 전부는 통째로 읽는다.
        if ( _residency == TextureResidency::StreamingBase )
        {
            const expected<uint64> bytesRead = readHeadAndTail();
            if ( bytesRead.has_value() == false )
            {
                StringBuilder<constant::buffer::kMax256> loadError;
                loadError.appendFormat( "%# - %#", bytesRead.error().message(), bytesRead.error().detail() );
                _loadError = loadError.view();
                return {};
            }
            loadScope.setBytes( bytesRead.value() );
            loadScope.setSucceeded();
            return {};
        }
        _pImage = make_unique<DDSImageData>();
        if ( Texture2D::readImage( AssetID{ getPath() }, *_pImage ) == false )
        {
            _loadError = "the DDS could not be read or uses a format the engine cannot upload";
            _pImage.reset();
        }
        else
        {
            loadScope.setBytes( _pImage->_bytes.size() );
            loadScope.setSucceeded();
        }
        // 실패도 완료 단계로 넘긴다 — 진행 중 표시는 완료 단계 하나가 내린다(헤더).
        return {};
    }

    expected<uint64> TextureLoadOperation::readHeadAndTail()
    {
        // 워커에서 기다린다 — 전체 읽기(`readImage`)도 이 자리에서 동기로 읽는다. 완료 콜백까지 끝난 뒤 `wait` 가 돌아온다.
        const auto readRange = [this]( uint64 offset, uint64 size, vector<uint8>& outBytes ) -> bool
        {
            bool                  bSucceeded = false;
            const AsyncReadHandle handle     = AssetIO::readBinaryAssetRangeAsync( AssetID{ _readPath }, offset, size, AsyncIOPriority::Normal,
                                                                                   SW_DELEGATE_LAMBDA( AssetReadCompleteDelegate, [&bSucceeded, &outBytes]( bool bSuccess, vector<uint8>& bytes )
                {
                bSucceeded = bSuccess;
                if ( bSuccess )
                    outBytes.swap( bytes );
            } ) );
            if ( handle.isValid() == false )
                return false;
            handle.wait();
            return bSucceeded && outBytes.size() == size;
        };
        const optional<uint64> fileSize = AssetIO::findAssetSize( AssetID{ _readPath } );
        if ( fileSize.has_value() == false )
            return unexpected( Error{ ErrorKind::NotFound, "the DDS could not be found", _readPath } );
        vector<uint8> header;
        if ( readRange( 0, MathUtil::min( uint64( TextureMipLayout::kMaxHeaderBytes ), *fileSize ), header ) == false )
            return unexpected( Error{ ErrorKind::ExternalFailure, "the DDS header could not be read", _readPath } );
        expected<TextureMipLayout> layout = TextureMipLayout::parse( header.data(), header.size(), *fileSize );
        if ( layout.has_value() == false )
            return unexpected( layout.error() );
        if ( Texture2D::toRHIFormatFromDXGI( layout->_dxgiFormat ) == RHIFormat::Unknown )
            return unexpected( Error{ ErrorKind::Unsupported, "the DDS uses a format the engine cannot upload", _readPath } );
        _tailFirstMip = Texture2D::computeResidentFirstMip( *layout );
        uint64 tailOffset{ 0 };
        uint64 tailSize{ 0 };
        layout->getRange( _tailFirstMip, static_cast<uint32>( layout->_listMip.size() ) - _tailFirstMip, tailOffset, tailSize );
        if ( readRange( tailOffset, tailSize, _tailBytes ) == false )
            return unexpected( Error{ ErrorKind::ExternalFailure, "the DDS tail mips could not be read", _readPath } );
        _mipLayout = std::move( *layout );
        return header.size() + _tailBytes.size();
    }

    void TextureLoadOperation::onAwaitingCompletion()
    {
        const shared_ptr<TextureCache::Impl> pCache = _pCache.lock();
        if ( pCache == nullptr )
            return;
        ScopedLock<mutex> lock{ pCache->_awaitingMutex };
        // 단계를 알린 뒤라 기다리던 스레드가 이미 올렸을 수 있다 — 그러면 넣지 않는다(넣으면 펌프가 없는 동안 로드 작업이 참조를 쥐고 남는다).
        // 완료 본문은 같은 잠금 안에서 목록에서 빼므로, 여기서 아직 기다리는 중이면 넣은 것을 그쪽이 뺀다.
        if ( isAwaitingCompletion() == false )
            return;
        // 같은 로드 작업이 두 번 들어가지 않게 한다(`Busy` 로 돌아온 로드 작업이 펌프의 되돌림과 겹칠 수 있다).
        for ( const shared_ptr<TextureLoadOperation>& pQueued : pCache->_listAwaitingUpload )
        {
            if ( pQueued.get() == this )
                return;
        }
        pCache->_listAwaitingUpload.push_back( shared_from_this() );
    }

    void TextureLoadOperation::onUploadSubmitted()
    {
        const shared_ptr<TextureCache::Impl> pCache = _pCache.lock();
        if ( pCache == nullptr )
            return;
        ScopedLock<mutex> lock{ pCache->_awaitingMutex };
        // 제출을 알린 뒤라 기다리던 스레드가 이미 끝냈을 수 있다 — 그러면 넣지 않는다(`onAwaitingCompletion` 과 같은 이유).
        if ( isUploading() == false )
            return;
        for ( const shared_ptr<TextureLoadOperation>& pQueued : pCache->_listUploading )
        {
            if ( pQueued.get() == this )
                return;
        }
        pCache->_listUploading.push_back( shared_from_this() );
    }

    void TextureLoadOperation::removeFromPollLists( TextureCache::Impl& cache )
    {
        // 펌프가 아닌 쪽(`wait`)이 먼저 올렸으면 목록에서 뺀다 — 목록이 로드 작업(곧 참조)을 쥔 채 남지 않게(렌더가 없는 테스트는 펌프가 돌지 않는다).
        ScopedLock<mutex> lock{ cache._awaitingMutex };
        for ( vector<shared_ptr<TextureLoadOperation>>* pList : { &cache._listAwaitingUpload, &cache._listUploading } )
        {
            for ( size_t index = 0; index < pList->size(); ++index )
            {
                if ( ( *pList )[index].get() != this )
                    continue;
                pList->erase( pList->begin() + static_cast<ptrdiff_t>( index ) );
                break;
            }
        }
    }

    expected<AssetCompletionStep> TextureLoadOperation::concludeCompletion( TextureCache::Impl& cache, bool bReady )
    {
        removeFromPollLists( cache );
        if ( bReady == false && _loadError.empty() )
            _loadError = "the GPU upload failed";
        // 위쪽 밉이 남았으면 읽은 이미지를 스트리밍 줄로 넘긴다 — 다시 읽지 않고 렌더 스레드가 프레임 예산 안에서 업로드한다.
        const bool bHasLayout      = _pImage != nullptr || _mipLayout._listMip.empty() == false;
        const bool bHasTopMipsLeft = _source == Source::File && bHasLayout && _pTexture->isFullyResident() == false;
        if ( bReady && bHasTopMipsLeft )
            cache.queueMipRequest( getPath(), _pImage != nullptr ? Texture2D::makeMipLayout( *_pImage ) : std::move( _mipLayout ), std::move( _readPath ) );
        _pImage.reset();
        _tailBytes = vector<uint8>{};
        _mipLayout = TextureMipLayout{};
        // 실패 — 텍스처가 지키던 번호(예약 · 옛 텍스처)를 이 로드 작업이 맡아 누락 체커로 다시 묶는다. 실패한 항목은 지워질 수 있으므로(`finishLoad`) 텍스처에 두지 않는다(R-6).
        IRHIDevice* const pDevice      = _pDevice.load( std::memory_order_acquire );
        const bool        bTakeKeptSRV = bReady == false && _pTexture != nullptr && pDevice != nullptr && _pTexture->getKeptSRV() != kInvalidDescriptorIndex;
        if ( bTakeKeptSRV )
        {
            const RHITextureSRVIndex kept = _pTexture->getKeptSRV();
            _failedSRV.store( kept, std::memory_order_release ); // 먼저 적는다 — `getStableSRV` 가 그 사이에 빈 번호를 보지 않게
            (void)_pTexture->takeKeptSRV();
            if ( cache.pointAtMissingChecker( *pDevice, kept, getPath() ) )
                _pFailedSRVDevice.store( pDevice, std::memory_order_release );
            else
                _failedSRV.store( kInvalidDescriptorIndex, std::memory_order_release ); // 놓았다
        }
        cache.finishLoad( getPath(), bReady );
        if ( bReady == false )
        {
            return unexpected( Error{ ErrorKind::ExternalFailure, "the texture could not be loaded", _loadError } );
        }
        _bHoldsReference = SW_TRUE;
        return AssetCompletionStep::Done;
    }

    expected<AssetCompletionStep> TextureLoadOperation::beginCompletion( bool bBlocking )
    {
        const shared_ptr<TextureCache::Impl> pCache = _pCache.lock();
        if ( pCache == nullptr )
        {
            return unexpected( Error{ ErrorKind::NotFound, "the texture cache was shut down before the upload" } );
        }
        removeFromPollLists( *pCache );
        if ( _loadError.empty() == false )
            return concludeCompletion( *pCache, false );

        IRHIDevice* const pDevice = _pDevice.load( std::memory_order_acquire );
        switch ( _source )
        {
            case Source::RenderTarget:
            {
                // 렌더 타깃은 0 채우기만 올린다 — 파일 · CPU 원본과 같이 제출하고, 블로킹이 아니면 이후 프레임의 펌프가 끝남을 보고 등록한다(진행 중인 프레임을 기다리지 않는다).
                const Texture2DUploadStep step =
                    _pTexture->beginRenderTarget( pDevice, getPath(), _renderTargetWidth, _renderTargetHeight, constant::render::kOffscreenColorFormat, bBlocking );
                if ( step == Texture2DUploadStep::Busy )
                    return AssetCompletionStep::Busy; // 다음 프레임에 다시 제출한다(텍스처는 지웠다)
                if ( step == Texture2DUploadStep::Failed )
                    return concludeCompletion( *pCache, false );
                if ( bBlocking )
                    return pollCompletion( true );
                return AssetCompletionStep::Submitted;
            }
            case Source::File:
            case Source::CPUBytes:
            {
                const bool bAlreadyResident = _source == Source::CPUBytes && _pTexture->isRHIValid();
                if ( bAlreadyResident )
                    return concludeCompletion( *pCache, true );
                // 제출 CPU 시간(장치 자원 만들기 · 스테이징 복사 · 전송 제출). GPU 전송 완료까지는 아니다(이후 프레임의 폴링). `Busy`(스테이징이 참)는 실패 수로 보인다.
                AssetLoadScope uploadScope( "TextureUpload", getPath() );
                uploadScope.beginPhase( AssetLoadPhase::Upload );
                const bool                bTail = _source == Source::File && _pImage == nullptr; // 머리 + 꼬리로 읽은 스트리밍 로드(MR-3)
                const size_t              bytes = bTail ? _tailBytes.size() : ( _pImage != nullptr ? _pImage->_bytes.size() : 0u );
                const Texture2DUploadStep step  = bTail                   ? _pTexture->beginUploadTail( pDevice, getPath(), _mipLayout, _tailFirstMip, _tailBytes, pCache->_qualityMipDrop.load( std::memory_order_relaxed ), bBlocking )
                                                : _source == Source::File ? _pTexture->beginUploadFromImage( pDevice, getPath(), *_pImage, _residency, pCache->_qualityMipDrop.load( std::memory_order_relaxed ), bBlocking )
                                                                          : _pTexture->beginUploadFromCPUBytes( pDevice, bBlocking );
                if ( step == Texture2DUploadStep::Submitted && _source == Source::File )
                    uploadScope.setBytes( bytes );
                if ( step == Texture2DUploadStep::Submitted )
                    uploadScope.setSucceeded();
                if ( step == Texture2DUploadStep::Busy )
                    return AssetCompletionStep::Busy; // 다음 프레임에 다시 제출한다(텍스처는 지웠다)
                if ( step == Texture2DUploadStep::Failed )
                    return concludeCompletion( *pCache, false );
                // 제출했다. 블로킹이면 곧바로 끝까지 기다린다(`beginUpload → 대기 → finishUpload`) — 아니면 이후 프레임의 펌프가 끝낸다.
                if ( bBlocking )
                    return pollCompletion( true );
                return AssetCompletionStep::Submitted;
            }
        }
        return unexpected( Error{ ErrorKind::Unsupported, "unknown texture source" } );
    }

    expected<AssetCompletionStep> TextureLoadOperation::pollCompletion( bool bBlocking )
    {
        const shared_ptr<TextureCache::Impl> pCache = _pCache.lock();
        if ( pCache == nullptr )
        {
            return unexpected( Error{ ErrorKind::NotFound, "the texture cache was shut down before the upload" } );
        }
        IRHIDevice* const           pDevice = _pDevice.load( std::memory_order_acquire );
        const Texture2DUploadStatus status  = _pTexture->finishUpload( pDevice, bBlocking );
        if ( status == Texture2DUploadStatus::Pending )
            return AssetCompletionStep::Submitted;
        return concludeCompletion( *pCache, status == Texture2DUploadStatus::Ready );
    }
} // namespace sw

namespace sw
{
    SW_BUILT_IN_ASSET_CACHE( TextureCache, TextureCache );

    ReloadResult TextureCache::reload( const AssetID& assetID )
    {
        const string_view     relativePath = assetID.view();
        const ScopedMemoryTag memoryScope{ kMemoryTag };
        IRHIDevice* const     pDevice = _impl != nullptr ? _impl->_pBoundDevice.load( std::memory_order_acquire ) : nullptr;
        // 렌더 텍스처 · CPU 텍스처는 디스크에 없다 — 다시 읽을 것이 없다.
        const bool bNotOnDisk = relativePath.empty() || isRenderTargetPath( relativePath ) || isCPUTexturePath( relativePath );
        const bool bCanReload = _impl != nullptr && pDevice != nullptr;
        if ( bNotOnDisk || bCanReload == false )
            return ReloadResult::NotTracked;
        const string key = AssetRegistry::toCacheKey( relativePath );

        // 렌더 스레드 비우기와 다시 올리기는 잠금 밖에서 한다. 잠금을 쥔 채 비우면 같은 잠금을 기다리는
        // 렌더 스레드와 서로 기다릴 수 있다. 진행 중 표시를 세워 두면 그사이 acquire 는 기다리고 release 는 지우지 않는다.
        UniqueLock<shared_mutex> lock{ _impl->_mutex };
        Texture2D*               pTexture = nullptr;
        for ( ;; )
        {
            const auto it = _impl->_mapEntry.find( key );
            if ( it == _impl->_mapEntry.end() || it->second._texture == nullptr )
                return ReloadResult::NotTracked;
            // 스트리밍이 위쪽 밉을 바꿔 끼우는 중이면 그 끝을 기다린다(렌더 스레드가 같은 텍스처의 핸들을 바꾸고 있다).
            if ( it->second._bLoading || it->second._bUpgrading )
            {
                _impl->_cvLoaded.wait( lock );
                continue;
            }
            // 자리 텍스처(로드 중 · 누락 체커)는 다시 읽지 않는다 — 예약한 번호들이 빌린 칸으로 그 GPU 텍스처를 가리키므로(`RHIBindlessLink::Borrowed`) 옛 텍스처를 부수면
            // 그 디스크립터가 해제된 자원을 가리킨다. 다음 실행에 반영된다(엔진 필수 데이터라 바꿀 일이 드물다).
            const Texture2D* const pFound     = it->second._texture.get();
            const bool             bIsStandIn = pFound == _impl->_pLoadingStandIn.load( std::memory_order_acquire ) || pFound == _impl->_pMissingStandIn.load( std::memory_order_acquire );
            if ( bIsStandIn )
            {
                SW_LOG_WARNING_ID( "TextureCache.StandInHotReloadSkipped", "Hot-Reload skipped for stand-in texture %# - restart to see the change", key.c_str() );
                return ReloadResult::NotTracked;
            }
            it->second._bLoading = true;
            pTexture             = it->second._texture.get();
            break;
        }
        lock.unlock();

        // 옛 텍스처를 놓기 전에 파일을 읽는다 — 읽지 못하면(오류는 `readImage` 가 남겼다) 옛 텍스처로 계속 그린다(언리얼 리임포트 실패도 옛 에셋을 둔다).
        DDSImageData image;
        const bool   bRead     = Texture2D::readImage( AssetID{ key }, image );
        bool         bReloaded = false;
        if ( bRead )
        {
            // 이전 프레임이 아직 이 텍스처와 그 bindless SRV 인덱스를 읽고 있을 수 있어도 GPU 를 기다리지 않는다. `releaseRHI` 의 `releaseHandle` 이 이미 만든 패킷 뒤로 반환을 미루므로
            // 새 텍스처는 새 인덱스를 받고, DX12 · Vulkan 은 그 뒤 펜스까지, DX11 · GL 은 기록 때 슬롯으로 풀었으니 그 패킷이 끝난 것으로 충분하다. 렌더 스레드만 비운다 — 등록은 병렬 기록 중에 할 수 없다.
            pDevice->flushRenderThread();
            // 옛 텍스처만 해제 줄로 보내고 SRV 번호는 지킨다 — 다시 읽은 텍스처가 같은 번호를 받아 머티리얼을 다시 묶지 않는다(R-6).
            Texture2DRetiredResource retired{};
            pTexture->detachRHIKeepingSRV( pDevice, retired );
            _impl->_retired.add( pDevice, retired );
            bReloaded = pTexture->loadFromImage( pDevice, key, image, TextureResidency::Full, _impl->_qualityMipDrop.load( std::memory_order_relaxed ) );
        }
        if ( bRead && bReloaded == false )
        {
            SW_LOG_ERROR_ID( "TextureCache.HotReloadFailedTexture", "Hot-Reload failed for Texture %#", key.c_str() );
            // 지키던 번호는 해제 줄로 간 옛 텍스처를 가리킨다 — 누락 체커로 다시 묶는다. 텍스처는 그 번호를 계속 지키고 다음 업로드가 다시 묶는다(R-6).
            const RHITextureSRVIndex kept = pTexture->getKeptSRV();
            if ( kept != kInvalidDescriptorIndex && _impl->pointAtMissingChecker( *pDevice, kept, key ) == false )
                (void)pTexture->takeKeptSRV(); // 놓았다
        }

        lock.lock();
        const auto            it = _impl->_mapEntry.find( key );
        unique_ptr<Texture2D> pErased;
        SW_ASSERT( it != _impl->_mapEntry.end() && it->second._bLoading );
        it->second._bLoading = false;
        // 다시 올리는 사이 마지막 참조가 놓였으면 release 대신 여기서 지우고, 장치 호출은 잠금 밖에서 한다(잠금은 잎 잠금, 결정 5-3).
        if ( it->second._refCount == 0 )
        {
            pErased = std::move( it->second._texture );
            _impl->_mapEntry.erase( it );
        }
        lock.unlock();
        if ( pErased != nullptr )
            pErased->releaseRHI( pDevice ); // 지키던 번호도 함께 놓는다
        _impl->_cvLoaded.notify_all();
        return bReloaded ? ReloadResult::Reloaded : ReloadResult::Failed;
    }
} // namespace sw

namespace sw
{
    // 렌더 타깃 크기를 받는 쪽 — 카메라(컴포넌트 모델 코어, Graphics 아래 층)가 인터페이스로 찾는다. 캐시는 에셋 매니저와 수명이 같다 — 공장이 그 매니저를 찾으므로 먼저 만든다.
    SW_ENGINE_SUBSYSTEM( IRenderTargetDeclarationSink, Startup, ( AssetManager ), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        (void)build;
        AssetManager& assetManager = scope.get<AssetManager>();
        (void)scope.exposeMember( assetManager, assetManager.getAssetCache<TextureCache>().getRenderTargetDeclarationSink() ); // 주인은 캐시를 가진 에셋 매니저다
    } );
} // namespace sw

namespace sw
{
    Texture2D* AssetRefCache<Texture2D>::acquire( string_view path )
    {
        return engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>().acquire( path );
    }

    TextureHandle AssetRefCache<Texture2D>::request( string_view path )
    {
        return engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>().request( path, AssetLoadStart::Async, TextureStreamingGroup::NeverStream );
    }
} // namespace sw
