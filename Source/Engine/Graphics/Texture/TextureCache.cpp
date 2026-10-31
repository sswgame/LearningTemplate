#include "pch.h"

#include "Engine/Graphics/Texture/TextureCache.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Diagnostics/CacheStats.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/BuiltInAssetCache.h"
#include "Engine/Asset/Image/DDSLoader.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/Transfer/RHITransferUtil.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCacheImpl.h"
#include "Engine/Profiling/FrameProfiler.h"

#include <condition_variable>

namespace sw
{
    SW_LOG_CALLER( "TextureCache" );

    namespace
    {
        /** @brief 텍스처 캐시의 계측 칸입니다(`CacheStats`). 캐시는 에셋 매니저마다 하나지만 칸은 프로세스에 하나라 매니저를 다시 세워도 이어 센다. */
        const CacheStatsHandle& getTextureCacheStats()
        {
            static const CacheStatsHandle s_stats = CacheStats::getOrCreate( TextureCache::kAssetKindName );
            return s_stats;
        }

    } // namespace

    /**
     * @brief `-gv_textureStreaming=0` — 비동기로 읽는 텍스처를 처음부터 밉 사슬 전부로 업로드합니다(밉 스트리밍을 끈다). 비교 측정 · 진단용입니다.
     * @details 켜져 있으면 비동기 로드는 아래쪽 밉만 먼저 업로드하고 위쪽 밉은 렌더 스레드가 프레임 예산 안에서 뒤에 업로드한다(`TextureCache::updateStreaming`).
     */
    SW_GLOBAL_VARIABLE( bool, gv_textureStreaming, true, "Upload async-loaded textures with their low mips first and stream the top mips in later (false = upload every mip at once)" );

    TextureCache::TextureCache()
        : IAssetCache{ CacheTracking::counted( kAssetKindName ) }
        , _impl{ sw::make_shared<Impl>() }
        , _renderTargetDeclarationSink{ *this }
    {
    }

    TextureCache::~TextureCache() = default;

    TextureHandle TextureCache::request( string_view relativePath, IRHIDevice* pDevice, AssetLoadStart start )
    {
        SW_MEMORY_SCOPE( Texture );
        const string key = FileUtil::normalizePath( relativePath );
        if ( key.empty() || pDevice == nullptr )
        {
            shared_ptr<TextureLoadBlock> pFailed = sw::make_shared<TextureLoadBlock>( key, pDevice, TextureLoadBlock::Source::File, nullptr, _impl );
            pFailed->markFailedWithoutLoad( key.empty() ? "the texture path is empty" : "there is no device to upload to" );
            return TextureHandle{ std::move( pFailed ) };
        }

        // 디스크 읽기와 GPU 생성은 잠금 밖에서 한다. 잠금 안에서는 "진행 중" 표시(_bLoading)와 그 로드의 블록만 적고, 같은 키를 동시에
        // 원한 쪽은 같은 블록을 받는다(이중 생성 없음). Texture2D 는 unique_ptr 이라 맵이 원소를 옮겨도 주소가 그대로다. 진행 중인 항목은
        // release · 실패 정리가 지우지 않는다.
        UniqueLock<shared_mutex> lock{ _impl->_mutex };
        Texture2D*               pTexture = nullptr;
        TextureLoadBlock::Source source   = TextureLoadBlock::Source::File;
        for ( ;; )
        {
            const auto it = _impl->_mapEntry.find( key );
            if ( it == _impl->_mapEntry.end() )
            {
                // CPU 텍스처는 원본이 있어야 만든다 — 소유자가 이미 놓았다.
                if ( isCPUTexturePath( key ) )
                {
                    lock.unlock();
                    shared_ptr<TextureLoadBlock> pFailed = sw::make_shared<TextureLoadBlock>( key, pDevice, TextureLoadBlock::Source::CPUBytes, nullptr, _impl );
                    pFailed->markFailedWithoutLoad( "the CPU texture was already released by its owner" );
                    return TextureHandle{ std::move( pFailed ) };
                }
                Impl::Entry& entry = _impl->_mapEntry[key];
                entry._texture     = make_unique<Texture2D>();
                entry._bLoading    = true;
                pTexture           = entry._texture.get();
                break;
            }
            Impl::Entry& entry = it->second;
            if ( entry._bLoading )
            {
                shared_ptr<TextureLoadBlock> pInFlight = entry._pLoadBlock.lock();
                if ( pInFlight != nullptr )
                {
                    // 기다린 쪽이 완료를 이 디바이스로 실행한다 — 앞선 요청의 디바이스는 내려갔을 수 있다(펌프 없는 테스트의 디바이스 차례).
                    pInFlight->_pDevice.store( pDevice, std::memory_order_release );
                    getTextureCacheStats().recordHit();
                    return TextureHandle{ std::move( pInFlight ) };
                }
                // 로드 블록이 아닌 길(`reload` · `resizeRenderTarget`, 디바이스 스레드)이 올리는 중이다 — 짧게 기다린다.
                _impl->_cvLoaded.wait( lock );
                continue;
            }
            if ( entry._texture->isRHIValid() )
            {
                // 캐시 적중 — 단계를 거치지 않고 참조를 쥔 `Ready` 블록을 준다.
                ++entry._refCount;
                shared_ptr<TextureLoadBlock> pReady = sw::make_shared<TextureLoadBlock>( key, pDevice, TextureLoadBlock::Source::File, entry._texture.get(), _impl );
                pReady->_bHoldsReference            = true;
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
            source = TextureLoadBlock::Source::RenderTarget;
        else if ( pTexture->hasCPUBytes() )
            source = TextureLoadBlock::Source::CPUBytes;
        shared_ptr<TextureLoadBlock> pBlock = sw::make_shared<TextureLoadBlock>( key, pDevice, source, pTexture, _impl );
        // 비동기 로드(씬의 머티리얼)는 아래쪽 밉부터 — 위쪽 밉은 렌더 스레드가 뒤에 업로드한다. 동기 로드(도구 · 미리보기)는 받는 자리에서 밉 전부를 쓴다.
        const bool bStreamed = start == AssetLoadStart::Async && gv_textureStreaming;
        pBlock->_residency   = bStreamed ? TextureResidency::StreamingBase : TextureResidency::Full;
        if ( source == TextureLoadBlock::Source::RenderTarget )
        {
            const auto sizeIter = _impl->_mapRenderTargetSize.find( key );
            if ( sizeIter != _impl->_mapRenderTargetSize.end() )
            {
                pBlock->_renderTargetWidth  = sizeIter->second._width;
                pBlock->_renderTargetHeight = sizeIter->second._height;
            }
        }
        _impl->_mapEntry[key]._pLoadBlock = pBlock;
        lock.unlock();

        AssetLoadBlock::startLoad( pBlock, start );
        return TextureHandle{ std::move( pBlock ) };
    }

    Texture2D* TextureCache::acquire( string_view relativePath, IRHIDevice* pDevice )
    {
        const TextureHandle handle = request( relativePath, pDevice, AssetLoadStart::OnCallingThread );
        // 기다림이 계약이다 — `acquire` 는 포인터를 바로 돌려주는 동기 API 이고, 기다리지 않을 호출자는 `request` 핸들을 들고 뒤 프레임에 본다.
        const shared_ptr<Texture2D> pTexture = handle.wait();
        if ( pTexture == nullptr )
            return nullptr;
        // 핸들 몫의 참조는 이 함수를 나가며 놓인다 — 돌려주는 포인터 몫(짝 `release`)을 하나 더 센다. 핸들이 쥐고 있으므로 항목은 있다.
        const string             key = FileUtil::normalizePath( relativePath );
        UniqueLock<shared_mutex> lock{ _impl->_mutex };
        const auto               it = _impl->_mapEntry.find( key );
        SW_ASSERT( it != _impl->_mapEntry.end() );
        ++it->second._refCount;
        return pTexture.get();
    }

    uint32 TextureCache::completePendingLoads( IRHIDevice* pDevice, uint32 maxCount )
    {
        return pumpPendingLoads( pDevice, maxCount, false );
    }

    uint32 TextureCache::finishPendingLoads( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr )
            return 0;
        uint32 completedCount = 0;
        for ( ;; )
        {
            completedCount += pumpPendingLoads( pDevice, UINT32_MAX, true );
            ScopedLock<mutex> lock{ _impl->_awaitingMutex };
            if ( _impl->_listAwaitingUpload.empty() && _impl->_listUploading.empty() )
                return completedCount;
        }
    }

    uint32 TextureCache::pumpPendingLoads( IRHIDevice* pDevice, uint32 maxCount, bool bBlocking )
    {
        if ( pDevice == nullptr )
            return 0;
        // 1) 이미 제출한 로드의 끝남 확인 — 값싼 폴링이라 상한 없이 모두 한다. 끝나지 않은 블록은 `onUploadSubmitted` 가 목록에 다시 넣는다.
        vector<shared_ptr<TextureLoadBlock>> listUploading;
        {
            ScopedLock<mutex> lock{ _impl->_awaitingMutex };
            listUploading.swap( _impl->_listUploading );
        }
        uint32 completedCount = 0;
        for ( const shared_ptr<TextureLoadBlock>& pBlock : listUploading )
        {
            pBlock->_pDevice.store( pDevice, std::memory_order_release );
            // 다른 스레드의 `wait()` 가 이미 집었으면 건너뛴다(집은 쪽이 끝낸다).
            (void)pBlock->runUploadPollIfPending( bBlocking );
        }
        // 폴링이 끝낸 수는 블록이 `Done` 이 된 것을 센다(아직 `Loading` 인 것은 목록에 남아 있다).
        for ( const shared_ptr<TextureLoadBlock>& pBlock : listUploading )
        {
            if ( pBlock->getState() != AssetLoadState::Loading )
                ++completedCount;
        }

        // 2) 새로 제출한다 — 프레임당 상한은 제출 수다.
        vector<shared_ptr<TextureLoadBlock>> listBlock;
        {
            ScopedLock<mutex> lock{ _impl->_awaitingMutex };
            const size_t      takeCount = MathUtil::min( static_cast<size_t>( maxCount ), _impl->_listAwaitingUpload.size() );
            if ( takeCount == 0 )
                return completedCount;
            listBlock.assign( std::make_move_iterator( _impl->_listAwaitingUpload.begin() ), std::make_move_iterator( _impl->_listAwaitingUpload.begin() + static_cast<ptrdiff_t>( takeCount ) ) );
            _impl->_listAwaitingUpload.erase( _impl->_listAwaitingUpload.begin(), _impl->_listAwaitingUpload.begin() + static_cast<ptrdiff_t>( takeCount ) );
        }
        // 이미 다른 스레드가 기다리며(`wait`) 올렸으면 건너뛴다 — 완료 단계는 먼저 집은 쪽 하나만 실행한다.
        for ( size_t index = 0; index < listBlock.size(); ++index )
        {
            const shared_ptr<TextureLoadBlock>& pBlock = listBlock[index];
            pBlock->_pDevice.store( pDevice, std::memory_order_release );
            if ( pBlock->runCompletionIfPending() == false )
                continue;
            // `Busy`(스테이징이 참)면 블록이 스스로 대기 목록으로 돌아갔다 — 이 프레임의 나머지도 같은 이유로 못 내므로 순서를 지켜 앞에 되돌려 놓고 멈춘다.
            if ( pBlock->isAwaitingCompletion() == false )
                continue;
            ScopedLock<mutex> lock{ _impl->_awaitingMutex };
            _impl->_listAwaitingUpload.insert( _impl->_listAwaitingUpload.begin(), std::make_move_iterator( listBlock.begin() + static_cast<ptrdiff_t>( index ) + 1 ), std::make_move_iterator( listBlock.end() ) );
            break;
        }
        return completedCount;
    }

    uint32 TextureCache::updateStreaming( IRHIDevice* pDevice, uint32 maxCount )
    {
        if ( pDevice == nullptr )
            return 0;
        uint32 upgradedCount = 0;
        // 항목의 올리는 중 표시를 내리고 결과를 반영한다 — 마지막 참조가 그사이 놓였으면 표에서 지우고 잠금 밖에서 내린다(`reload` 와 같다).
        const auto settleUpgrade = [this, pDevice, &upgradedCount]( const string& key, bool bUpgraded, const Texture2DRetiredResource& retired )
        {
            unique_ptr<Texture2D> pErased;
            {
                UniqueLock<shared_mutex> lock{ _impl->_mutex };
                const auto               it = _impl->_mapEntry.find( key );
                SW_ASSERT( it != _impl->_mapEntry.end() && it->second._bUpgrading );
                it->second._bUpgrading = false;
                if ( bUpgraded )
                    _impl->_retired.add( pDevice, retired );
                if ( it->second._refCount == 0 && it->second._bPinned == false )
                {
                    pErased = std::move( it->second._texture );
                    _impl->_mapEntry.erase( it );
                }
            }
            if ( pErased != nullptr )
                pErased->releaseRHI( pDevice );
            if ( bUpgraded )
            {
                ++upgradedCount;
                // 새 SRV 번호가 생겼다 — 빌려 간 머티리얼이 다시 받게 한다(표시를 내린 뒤에 올린다: `reload` 와 같은 순서).
                _impl->_reloadGeneration.fetch_add( 1u, std::memory_order_acq_rel );
            }
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
            const Texture2DUploadStatus status = pTexture->finishUpgradeResidency( pDevice, false, retired );
            if ( status == Texture2DUploadStatus::Pending )
            {
                listStillPending.push_back( key );
                continue;
            }
            settleUpgrade( key, status == Texture2DUploadStatus::Ready, retired );
        }
        if ( listStillPending.empty() == false )
        {
            ScopedLock<mutex> lock{ _impl->_streamingMutex };
            _impl->_listUpgrading.insert( _impl->_listUpgrading.end(), std::make_move_iterator( listStillPending.begin() ), std::make_move_iterator( listStillPending.end() ) );
        }
        if ( maxCount == 0 )
            return upgradedCount;

        // 2) 새로 제출한다 — 프레임당 상한은 제출 수다.
        vector<Impl::StreamingUpgrade> listUpgrade;
        {
            ScopedLock<mutex> lock{ _impl->_streamingMutex };
            const size_t      takeCount = MathUtil::min( static_cast<size_t>( maxCount ), _impl->_listStreamingUpgrade.size() );
            if ( takeCount == 0 )
            {
                _impl->_mapScreenSize.clear();
                return upgradedCount;
            }
            // 화면에서 큰 텍스처부터 올린다. 알리지 않은 것은 0 이고, 같은 값끼리는 먼저 읽힌 순서를 지킨다(안정 정렬).
            if ( _impl->_mapScreenSize.empty() == false )
            {
                const unordered_map<string, float32>& mapScreenSize = _impl->_mapScreenSize;
                const auto                            sizeOf        = [&mapScreenSize]( const Impl::StreamingUpgrade& upgrade )
                {
                    const auto it = mapScreenSize.find( upgrade._key );
                    return it != mapScreenSize.end() ? it->second : 0.0f;
                };
                VectorUtil::stableSort( _impl->_listStreamingUpgrade, [&sizeOf]( const Impl::StreamingUpgrade& lhs, const Impl::StreamingUpgrade& rhs )
                { return sizeOf( lhs ) > sizeOf( rhs ); } );
                _impl->_mapScreenSize.clear();
            }
            listUpgrade.assign( std::make_move_iterator( _impl->_listStreamingUpgrade.begin() ),
                                std::make_move_iterator( _impl->_listStreamingUpgrade.begin() + static_cast<ptrdiff_t>( takeCount ) ) );
            _impl->_listStreamingUpgrade.erase( _impl->_listStreamingUpgrade.begin(), _impl->_listStreamingUpgrade.begin() + static_cast<ptrdiff_t>( takeCount ) );
        }
        SW_PROFILE_SCOPE( "RT.TextureStreaming" );
        for ( size_t index = 0; index < listUpgrade.size(); ++index )
        {
            Impl::StreamingUpgrade& upgrade  = listUpgrade[index];
            Texture2D*              pTexture = nullptr;
            {
                UniqueLock<shared_mutex> lock{ _impl->_mutex };
                const auto               it = _impl->_mapEntry.find( upgrade._key );
                // 놓였으면 버린다. 다시 읽는 중(`reload` · 디바이스 교체)이면 그쪽이 밉 전부를 올린다.
                const bool bUpgradable = it != _impl->_mapEntry.end() && it->second._bLoading == false && it->second._bUpgrading == false && it->second._bPublished &&
                                         it->second._texture != nullptr && it->second._texture->isFullyResident() == false;
                if ( bUpgradable == false )
                    continue;
                it->second._bUpgrading = true;
                pTexture               = it->second._texture.get();
            }
            // 제출은 잠금 밖에서 — 게임 스레드의 find · acquire 를 붙잡지 않는다. 표시가 항목을 지키고(놓여도 지우지 않는다), 다시 읽기는 끝을 기다린다.
            // 표시는 끝남 확인(`finishUpgradeResidency`)이 끝낼 때 내린다 — 몇 프레임 뒤다.
            const Texture2DUploadStep step = pTexture->beginUpgradeResidency( pDevice, *upgrade._pImage, false );
            if ( step == Texture2DUploadStep::Submitted )
            {
                ScopedLock<mutex> lock{ _impl->_streamingMutex };
                _impl->_listUpgrading.push_back( upgrade._key );
                continue;
            }
            settleUpgrade( upgrade._key, false, Texture2DRetiredResource{} );
            if ( step != Texture2DUploadStep::Busy )
                continue; // 실패했다(오류를 남겼다) — 아래쪽 밉 그대로 둔다
            // 스테이징이 찼다 — 이 프레임의 나머지도 같은 이유로 못 내므로 순서를 지켜 줄 앞에 되돌려 놓고 멈춘다(다음 프레임에 다시 낸다).
            ScopedLock<mutex> lock{ _impl->_streamingMutex };
            _impl->_listStreamingUpgrade.insert( _impl->_listStreamingUpgrade.begin(), std::make_move_iterator( listUpgrade.begin() + static_cast<ptrdiff_t>( index ) ),
                                                 std::make_move_iterator( listUpgrade.end() ) );
            break;
        }
        return upgradedCount;
    }

    void TextureCache::noteScreenSize( string_view relativePath, float32 screenSize )
    {
        if ( screenSize <= 0.0f )
            return;
        const string      key = FileUtil::normalizePath( relativePath );
        ScopedLock<mutex> lock{ _impl->_streamingMutex };
        float32&          noted = _impl->_mapScreenSize[key];
        noted                   = MathUtil::max( noted, screenSize );
    }

    bool TextureCache::hasPendingStreamingUpgrades() const
    {
        ScopedLock<mutex> lock{ _impl->_streamingMutex };
        return _impl->_listStreamingUpgrade.empty() == false || _impl->_listUpgrading.empty() == false;
    }

    void TextureCache::releaseRetiredTextures( IRHIDevice* pDevice )
    {
        if ( pDevice != nullptr )
            _impl->_retired.releaseFor( pDevice );
    }

    uint32 TextureCache::countLoadsInFlight() const
    {
        uint32 count = 0;
        {
            ScopedLock<mutex> lock{ _impl->_streamingMutex };
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

    bool TextureCache::createCPUTexture( string_view relativePath, uint32 width, uint32 height, vector<uint8> rgbaBytes )
    {
        const bool bSizeMatches  = width > 0 && height > 0 && rgbaBytes.size() == static_cast<size_t>( width ) * height * 4;
        const bool bValidRequest = isCPUTexturePath( relativePath ) && bSizeMatches;
        if ( _impl == nullptr || bValidRequest == false )
            return false;
        const string             key = FileUtil::normalizePath( relativePath );
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
        const string             key = FileUtil::normalizePath( relativePath );
        UniqueLock<shared_mutex> lock{ _impl->_mutex };
        const auto               it = _impl->_mapEntry.find( key );
        if ( it == _impl->_mapEntry.end() )
            return;
        it->second._bPinned = false;
        // 참조가 0 이면 GPU 자원도 없다(`release` 가 내렸고 `initRHI` 는 CPU 텍스처를 올리지 않는다).
        if ( it->second._refCount == 0 && it->second._bLoading == false )
            _impl->_mapEntry.erase( it );
    }

    void TextureCache::declareRenderTarget( string_view relativePath, uint32 width, uint32 height )
    {
        const bool bInvalidDeclaration = _impl == nullptr || isRenderTargetPath( relativePath ) == false || width == 0 || height == 0;
        if ( bInvalidDeclaration )
            return;
        const string             key = FileUtil::normalizePath( relativePath );
        UniqueLock<shared_mutex> lock{ _impl->_mutex };
        Impl::RenderTargetSize&  size = _impl->_mapRenderTargetSize[key];
        size._width                   = width;
        size._height                  = height;
    }

    bool TextureCache::resizeRenderTarget( string_view relativePath, uint32 width, uint32 height, IRHIDevice* pDevice )
    {
        SW_MEMORY_SCOPE( Texture );
        const bool bInvalidRenderTargetRequest = _impl == nullptr || pDevice == nullptr || isRenderTargetPath( relativePath ) == false || width == 0 || height == 0;
        if ( bInvalidRenderTargetRequest )
            return false;
        const string key = FileUtil::normalizePath( relativePath );

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

        // 이전 프레임이 옛 텍스처와 그 bindless 인덱스를 읽고 있을 수 있다. 기다리지 않고 옛 자원을 해제 줄에 넘긴다(RHIAsyncUploads U13 — `waitIdle` 을 지웠다):
        // 게임 스레드가 `releaseRetiredTextures` 로 `releaseHandle` 하면 이미 만든 패킷이 끝난 뒤에 핸들 · 인덱스가 돌아가고(스트리밍 교체와 같은 길), 새 텍스처는 새 핸들 · 인덱스를 받는다.
        // 그래서 옛 인덱스를 새 텍스처가 곧바로 물려받아 이전 프레임을 오염시키는 일이 없다. 세대를 올려 빌려 간 머티리얼이 새 인덱스를 다시 받게 한다(아래).
        Texture2DRetiredResource retired{};
        pTexture->detachRHI( retired );
        const bool bReady = pTexture->createRenderTarget( pDevice, key, width, height, constant::kOffscreenColorFormat );
        if ( bReady == false )
            SW_LOG_ERROR( "Failed to resize render texture %# to %#x%#", key.c_str(), width, height );

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
            pErased->releaseRHI( pDevice ); // 장치 호출은 잠금 밖에서(잠금은 잎 잠금, 결정 5-3)
        // 새 SRV 인덱스를 받았다 — 빌려 간 머티리얼이 다시 받게 한다(reload 와 같은 순서: 진행 중 표시를 내린 뒤에 올린다).
        _impl->_reloadGeneration.fetch_add( 1u, std::memory_order_acq_rel );
        _impl->_cvLoaded.notify_all();
        return bReady;
    }

    const Texture2D* TextureCache::find( string_view relativePath ) const
    {
        if ( relativePath.empty() || _impl == nullptr )
            return nullptr;
        const string                   key = FileUtil::normalizePath( relativePath );
        ScopedSharedLock<shared_mutex> lock{ _impl->_mutex };
        const auto                     it = _impl->_mapEntry.find( key );
        // 올리는 중(처음 acquire · reload)인 항목은 내주지 않는다 — 그 스레드가 잠금 없이 같은 객체의 핸들 · SRV 를 바꾸고 있다.
        const bool bReady = it != _impl->_mapEntry.end() && it->second._bPublished && it->second._bLoading == false;
        if ( bReady == false )
            return nullptr;
        return it->second._texture.get();
    }

    uint32 TextureCache::getReloadGeneration() const
    {
        return _impl != nullptr ? _impl->_reloadGeneration.load( std::memory_order_acquire ) : 0u;
    }

    void TextureCache::requestRegionUpdate( string_view relativePath, uint32 x, uint32 y, uint32 width, uint32 height, vector<uint8> rgbaBytes )
    {
        const bool bEmpty = relativePath.empty() || width == 0 || height == 0;
        if ( _impl == nullptr || bEmpty )
            return;
        Impl::RegionUpdate update;
        update._key       = FileUtil::normalizePath( relativePath );
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
    void TextureCache::release( string_view relativePath, IRHIDevice* pDevice )
    {
        if ( relativePath.empty() || _impl == nullptr )
            return;
        _impl->releaseReference( FileUtil::normalizePath( relativePath ), pDevice );
    }

    bool TextureCache::isCached( string_view relativePath ) const
    {
        if ( relativePath.empty() || _impl == nullptr )
            return false;

        const string                   key = FileUtil::normalizePath( relativePath );
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

    /**
     * @brief 표를 통째로 비웁니다. **GPU 자원을 돌려주지는 않습니다.**
     * @warning **디바이스가 죽은 뒤에만 부를 수 있습니다.** `IAssetCache::clear()` 는 디바이스를
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
            _impl->_listAwaitingUpload.clear(); // 올리지 않은 로드는 버린다 — 그 블록을 쥔 핸들은 `Loading` 으로 남는다(종료 때만 온다)
            _impl->_listUploading.clear();      // 제출만 하고 끝을 확인하지 못한 로드도 같다(GPU 자원은 호출 전에 해제되어 있다)
        }
        {
            ScopedLock<mutex> streamingLock{ _impl->_streamingMutex };
            _impl->_listStreamingUpgrade.clear(); // 위쪽 밉을 기다리던 텍스처도 함께 사라진다
            _impl->_listUpgrading.clear();        // 위쪽 밉을 올리는 중이던 것도 같다
        }
        UniqueLock<shared_mutex> lock{ _impl->_mutex };
        _impl->_mapEntry.clear();
        getTextureCacheStats().recordClear();
    }
} // namespace sw

namespace sw
{
    TextureLoadBlock::TextureLoadBlock( string_view key, IRHIDevice* pDevice, Source source, Texture2D* pTexture, weak_ptr<TextureCache::Impl> pCache )
        : AssetLoadBlock{ TextureCache::kAssetKindName, key }
        , _pImage{}
        , _loadError{}
        , _pCache{ std::move( pCache ) }
        , _pDevice{ pDevice }
        , _pTexture{ pTexture }
        , _renderTargetWidth{ TextureCache::kDefaultRenderTargetSize }
        , _renderTargetHeight{ TextureCache::kDefaultRenderTargetSize }
        , _source{ source }
        , _residency{ TextureResidency::Full }
        , _bHoldsReference{ false }
    {
    }

    TextureLoadBlock::~TextureLoadBlock()
    {
        if ( _bHoldsReference == false )
            return;
        // 캐시가 먼저 사라졌으면(종료) 놓을 항목이 없다.
        const shared_ptr<TextureCache::Impl> pCache = _pCache.lock();
        if ( pCache != nullptr )
            pCache->releaseReference( getPath(), _pDevice.load( std::memory_order_acquire ) );
    }

    shared_ptr<Texture2D> TextureLoadBlock::getAsset() const
    {
        // 별칭 shared_ptr — 블록(곧 참조)을 쥔 채 텍스처를 가리킨다. 텍스처 객체는 항목이 소유한다.
        return shared_ptr<Texture2D>( shared_from_this(), _pTexture );
    }

    bool TextureLoadBlock::runLoad( string& /*outError*/ )
    {
        // 렌더 타깃 · CPU 텍스처는 읽을 파일이 없다 — 업로드 단계만 있다.
        if ( _source != Source::File )
            return true;
        _pImage = make_unique<DDSImageData>();
        if ( Texture2D::readImage( AssetID{ getPath() }, *_pImage ) == false )
        {
            _loadError = "the DDS could not be read or uses a format the engine cannot upload";
            _pImage.reset();
        }
        // 실패도 완료 단계로 넘긴다 — 진행 중 표시는 완료 단계 하나가 내린다(헤더).
        return true;
    }

    void TextureLoadBlock::onAwaitingCompletion()
    {
        const shared_ptr<TextureCache::Impl> pCache = _pCache.lock();
        if ( pCache == nullptr )
            return;
        ScopedLock<mutex> lock{ pCache->_awaitingMutex };
        // 단계를 알린 뒤라 기다리던 스레드가 이미 올렸을 수 있다 — 그러면 넣지 않는다(넣으면 펌프가 없는 동안 블록이 참조를 쥐고 남는다).
        // 완료 본문은 같은 잠금 안에서 목록에서 빼므로, 여기서 아직 기다리는 중이면 넣은 것을 그쪽이 뺀다.
        if ( isAwaitingCompletion() == false )
            return;
        // 같은 블록이 두 번 들어가지 않게 한다(`Busy` 로 돌아온 블록이 펌프의 되돌림과 겹칠 수 있다).
        for ( const shared_ptr<TextureLoadBlock>& pQueued : pCache->_listAwaitingUpload )
        {
            if ( pQueued.get() == this )
                return;
        }
        pCache->_listAwaitingUpload.push_back( shared_from_this() );
    }

    void TextureLoadBlock::onUploadSubmitted()
    {
        const shared_ptr<TextureCache::Impl> pCache = _pCache.lock();
        if ( pCache == nullptr )
            return;
        ScopedLock<mutex> lock{ pCache->_awaitingMutex };
        // 제출을 알린 뒤라 기다리던 스레드가 이미 끝냈을 수 있다 — 그러면 넣지 않는다(`onAwaitingCompletion` 과 같은 이유).
        if ( isUploading() == false )
            return;
        for ( const shared_ptr<TextureLoadBlock>& pQueued : pCache->_listUploading )
        {
            if ( pQueued.get() == this )
                return;
        }
        pCache->_listUploading.push_back( shared_from_this() );
    }

    void TextureLoadBlock::removeFromPumpLists( TextureCache::Impl& cache )
    {
        // 펌프가 아닌 쪽(`wait`)이 먼저 올렸으면 목록에서 뺀다 — 목록이 블록(곧 참조)을 쥔 채 남지 않게(렌더가 없는 테스트는 펌프가 돌지 않는다).
        ScopedLock<mutex> lock{ cache._awaitingMutex };
        for ( vector<shared_ptr<TextureLoadBlock>>* pList : { &cache._listAwaitingUpload, &cache._listUploading } )
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

    AssetCompletionStep TextureLoadBlock::concludeCompletion( TextureCache::Impl& cache, bool bReady, string& outError )
    {
        removeFromPumpLists( cache );
        if ( bReady == false && _loadError.empty() )
            _loadError = "the GPU upload failed";
        // 위쪽 밉이 남았으면 읽은 이미지를 스트리밍 줄로 넘긴다 — 다시 읽지 않고 렌더 스레드가 프레임 예산 안에서 업로드한다.
        const bool bHasTopMipsLeft = _source == Source::File && _pImage != nullptr && _pTexture->isFullyResident() == false;
        if ( bReady && bHasTopMipsLeft )
            cache.queueStreamingUpgrade( getPath(), std::move( _pImage ) );
        _pImage.reset();
        cache.finishLoad( getPath(), bReady );
        if ( bReady == false )
        {
            outError = _loadError;
            return AssetCompletionStep::Failed;
        }
        _bHoldsReference = true;
        return AssetCompletionStep::Done;
    }

    AssetCompletionStep TextureLoadBlock::beginCompletion( bool bBlocking, string& outError )
    {
        SW_MEMORY_SCOPE( Texture );
        const shared_ptr<TextureCache::Impl> pCache = _pCache.lock();
        if ( pCache == nullptr )
        {
            outError = "the texture cache was shut down before the upload";
            return AssetCompletionStep::Failed;
        }
        removeFromPumpLists( *pCache );
        if ( _loadError.empty() == false )
            return concludeCompletion( *pCache, false, outError );

        IRHIDevice* const pDevice = _pDevice.load( std::memory_order_acquire );
        switch ( _source )
        {
            case Source::RenderTarget:
            {
                // 렌더 타깃은 올릴 내용이 없다 — 만들고 바로 등록한다.
                const bool bReady = _pTexture->createRenderTarget( pDevice, getPath(), _renderTargetWidth, _renderTargetHeight, constant::kOffscreenColorFormat );
                return concludeCompletion( *pCache, bReady, outError );
            }
            case Source::File:
            case Source::CPUBytes:
            {
                const bool bAlreadyResident = _source == Source::CPUBytes && _pTexture->isRHIValid();
                if ( bAlreadyResident )
                    return concludeCompletion( *pCache, true, outError );
                const Texture2DUploadStep step = _source == Source::File ? _pTexture->beginUploadFromImage( pDevice, getPath(), *_pImage, _residency, bBlocking )
                                                                         : _pTexture->beginUploadFromCPUBytes( pDevice, bBlocking );
                if ( step == Texture2DUploadStep::Busy )
                    return AssetCompletionStep::Busy; // 다음 프레임에 다시 제출한다(텍스처는 지웠다)
                if ( step == Texture2DUploadStep::Failed )
                    return concludeCompletion( *pCache, false, outError );
                // 제출했다. 블로킹이면 곧바로 끝까지 기다린다(`beginUpload → 대기 → finishUpload`) — 아니면 이후 프레임의 펌프가 끝낸다.
                if ( bBlocking )
                    return pollCompletion( true, outError );
                return AssetCompletionStep::Submitted;
            }
        }
        outError = "unknown texture source";
        return AssetCompletionStep::Failed;
    }

    AssetCompletionStep TextureLoadBlock::pollCompletion( bool bBlocking, string& outError )
    {
        SW_MEMORY_SCOPE( Texture );
        const shared_ptr<TextureCache::Impl> pCache = _pCache.lock();
        if ( pCache == nullptr )
        {
            outError = "the texture cache was shut down before the upload";
            return AssetCompletionStep::Failed;
        }
        IRHIDevice* const           pDevice = _pDevice.load( std::memory_order_acquire );
        const Texture2DUploadStatus status  = _pTexture->finishUpload( pDevice, bBlocking );
        if ( status == Texture2DUploadStatus::Pending )
            return AssetCompletionStep::Submitted;
        return concludeCompletion( *pCache, status == Texture2DUploadStatus::Ready, outError );
    }
} // namespace sw

namespace sw
{
    SW_BUILT_IN_ASSET_CACHE( TextureCache );

    void TextureCache::reload( const AssetID& assetID, IRHIDevice* pDevice )
    {
        const string_view relativePath = assetID.view();
        SW_MEMORY_SCOPE( Texture );
        // 렌더 텍스처 · CPU 텍스처는 디스크에 없다 — 다시 읽을 것이 없다.
        const bool bNotOnDisk = relativePath.empty() || isRenderTargetPath( relativePath ) || isCPUTexturePath( relativePath );
        const bool bCanReload = _impl != nullptr && pDevice != nullptr;
        if ( bNotOnDisk || bCanReload == false )
            return;
        const string key = FileUtil::normalizePath( relativePath );

        // 기다림(waitIdle)과 다시 올리기는 잠금 밖에서 한다. 잠금을 쥔 채 waitIdle 을 부르면 같은 잠금을 기다리는
        // 렌더 스레드와 서로 기다릴 수 있다. 진행 중 표시를 세워 두면 그사이 acquire 는 기다리고 release 는 지우지 않는다.
        UniqueLock<shared_mutex> lock{ _impl->_mutex };
        Texture2D*               pTexture = nullptr;
        for ( ;; )
        {
            const auto it = _impl->_mapEntry.find( key );
            if ( it == _impl->_mapEntry.end() || it->second._texture == nullptr )
                return;
            // 스트리밍이 위쪽 밉을 바꿔 끼우는 중이면 그 끝을 기다린다(렌더 스레드가 같은 텍스처의 핸들을 바꾸고 있다).
            if ( it->second._bLoading || it->second._bUpgrading )
            {
                _impl->_cvLoaded.wait( lock );
                continue;
            }
            it->second._bLoading = true;
            pTexture             = it->second._texture.get();
            break;
        }
        lock.unlock();

        // 이전 프레임이 아직 이 텍스처와 그 bindless SRV 인덱스를 읽고 있을 수 있다. 인덱스를 돌려주는 때는 백엔드마다 다르다:
        // DX12 · Vulkan 은 해제 큐(enqueueGPURelease)를 거쳐 GPU 펜스가 지난 뒤에 프리리스트로 돌려주지만,
        // DX11 · GL 은 unregisterBindlessTexture 에서 곧바로 돌려준다. 그 둘에서 기다리지 않고 다시 올리면 같은 인덱스를 받은
        // 다른 텍스처를 읽는 조용한 오염이 된다(MaterialCache::reload 와 같은 이유). 텍스처 객체 해제도 같은 대기에 기댄다.
        pDevice->waitIdle( RHIWaitIdleReason::HotReload );
        pTexture->releaseRHI( pDevice );
        if ( pTexture->loadFromAsset( pDevice, AssetID{ key } ) == false )
            SW_LOG_ERROR( "Hot-Reload failed for Texture %#", key.c_str() );

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
            pErased->releaseRHI( pDevice );
        // 다시 올린 텍스처는 새 SRV 인덱스를 받았다(실패했으면 인덱스가 없다). 옛 인덱스는 이미 돌려줬으므로 빌려 간 머티리얼이 다시 받게 한다.
        // 세대는 진행 중 표시를 내린 **뒤에** 올린다 — 먼저 올리면 새 세대를 본 쪽의 find 가 아직 nullptr 를 받고 그 세대를 소비해 버린다.
        _impl->_reloadGeneration.fetch_add( 1u, std::memory_order_acq_rel );
        _impl->_cvLoaded.notify_all();
    }
} // namespace sw
