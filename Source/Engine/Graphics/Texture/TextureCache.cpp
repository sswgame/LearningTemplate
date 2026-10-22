#include "pch.h"

#include "Engine/Graphics/Texture/TextureCache.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Common/RenderTargetDeclaration.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/Cache/BuiltInAssetCache.h"
#include "Engine/Resource/Image/DDSLoader.h"

#include <condition_variable>

namespace sw
{
    SW_LOG_CALLER( "TextureCache" );

    namespace
    {
        /** @brief `engine::declareRenderTarget` 의 처리 함수입니다. 바인딩된 에셋 매니저의 텍스처 캐시에 넘긴다(부르는 쪽이 바인딩을 확인한다). */
        void forwardRenderTargetDeclaration( string_view relativePath, uint32 width, uint32 height )
        {
            if ( engine::areEngineServicesBound() == false )
                return;
            engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>().declareRenderTarget( relativePath, width, height );
        }

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
                std::scoped_lock<mutex> lock{ _mutex };
                _listItem.push_back( Item{ pDevice, resource } );
            }

            /** @brief @p pDevice 의 것을 모두 `releaseHandle` 로 해제합니다(게임 스레드 — 이미 만든 패킷이 끝날 때까지 미룬다). */
            void releaseFor( IRHIDevice* pDevice )
            {
                vector<Item> listRelease;
                {
                    std::scoped_lock<mutex> lock{ _mutex };
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
                std::scoped_lock<mutex> lock{ _mutex };
                VectorUtil::removeIf( _listItem, [pDevice]( const Item& item )
                { return item._pDevice == pDevice; } );
            }

        private:
            mutable mutex _mutex;
            vector<Item>  _listItem;
        };
    } // namespace

    /**
     * @brief `-gv_textureStreaming=0` — 비동기로 읽는 텍스처를 처음부터 밉 사슬 전부로 업로드합니다(밉 스트리밍을 끈다). 비교 측정 · 진단용입니다.
     * @details 켜져 있으면 비동기 로드는 아래쪽 밉만 먼저 업로드하고 위쪽 밉은 렌더 스레드가 프레임 예산 안에서 뒤에 업로드한다(`TextureCache::updateStreaming`).
     */
    SW_GLOBAL_VARIABLE( int32, gv_textureStreaming, 1, "Upload async-loaded textures with their low mips first and stream the top mips in later (0 = upload every mip at once)" );

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
        };
        unordered_map<string, Entry>            _mapEntry;
        vector<RegionUpdate>                    _listPendingRegion;  ///< 게임 스레드가 쌓고 렌더 스레드가 비운다(`_regionMutex`)
        vector<RegionUpdate>                    _listFlushingRegion; ///< 렌더 스레드만 쓰는 맞바꾸기 자리(저장소를 재사용한다)
        mutex                                   _regionMutex;
        unordered_map<string, RenderTargetSize> _mapRenderTargetSize; ///< `declareRenderTarget` 이 알린 크기
        std::shared_mutex                       _mutex;
        std::condition_variable_any             _cvLoaded;              ///< 항목의 `_bLoading` 이 내려갈 때 깨운다
        atomic<uint32>                          _reloadGeneration{ 0 }; ///< SRV 가 바뀌었을 수 있는 횟수 — 머티리얼이 SRV 인덱스를 다시 받을 때를 안다
        mutex                                   _awaitingMutex;
        vector<shared_ptr<TextureLoadBlock>>    _listAwaitingUpload; ///< 워커가 읽기를 마치고 업로드를 기다리는 로드(`completePendingLoads` 가 비운다)
        mutex                                   _streamingMutex;
        vector<StreamingUpgrade>                _listStreamingUpgrade; ///< 위쪽 밉을 기다리는 텍스처(먼저 들어온 것부터, `updateStreaming` 이 비운다)
        TextureStreamingRetireList              _retired;              ///< 바꿔 끼운 옛 GPU 자원(`releaseRetiredTextures` 가 해제한다)

        /** @brief 위쪽 밉을 기다리는 줄에 넣습니다(로드 블록의 완료 단계). */
        void queueStreamingUpgrade( const string& key, unique_ptr<DDSImageData> pImage )
        {
            std::scoped_lock<mutex> lock{ _streamingMutex };
            _listStreamingUpgrade.push_back( StreamingUpgrade{ key, std::move( pImage ) } );
        }

        /** @brief 참조를 하나 놓습니다. 0 이 되면 GPU 자원까지 해제합니다(`TextureCache::release` · 로드 블록 소멸). */
        void releaseReference( const string& key, IRHIDevice* pDevice )
        {
            std::unique_lock<std::shared_mutex> lock{ _mutex };
            auto                                it = _mapEntry.find( key );
            if ( it == _mapEntry.end() )
                return;
            if ( it->second._refCount > 0 )
                --it->second._refCount;
            // 올리는 중(acquire · reload · 스트리밍)이면 지우지 않는다 — 그 스레드가 끝날 때 참조 수를 보고 정리한다.
            const bool bUnused = it->second._refCount == 0 && it->second._bLoading == false && it->second._bUpgrading == false;
            if ( bUnused )
            {
                if ( it->second._texture != nullptr )
                    it->second._texture->releaseRHI( pDevice );
                // CPU 텍스처는 원본을 남긴다 — 소유자가 `releaseCPUTexture` 로 지운다.
                if ( it->second._bPinned == false )
                    _mapEntry.erase( it );
            }
        }

        /**
         * @brief 로드 블록의 완료 단계가 끝났음을 항목에 적습니다. 성공하면 공개하고 블록 몫의 참조를 하나 셉니다. 실패하면 아무도 쥐지 않은 항목을 지웁니다.
         * @details 진행 중 표시를 내리는 곳은 여기 하나다 — 로드 단계가 실패해도 완료 단계를 거쳐 여기로 온다(`TextureLoadBlock` 머리말).
         */
        void finishLoad( const string& key, bool bReady )
        {
            {
                std::unique_lock<std::shared_mutex> lock{ _mutex };
                const auto                          it = _mapEntry.find( key );
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

    TextureCache::TextureCache()
        : _impl{ sw::make_shared<Impl>() }
    {
        // 컴포넌트 모델 코어(카메라)는 이 헤더를 모르므로 Engine/Common 의 창구로 크기를 알린다 — 처리 함수는 상태가 없어 다시 묶어도 같다.
        engine::bindRenderTargetDeclaration( &forwardRenderTargetDeclaration );
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
        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        Texture2D*                          pTexture = nullptr;
        TextureLoadBlock::Source            source   = TextureLoadBlock::Source::File;
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
                return TextureHandle{ std::move( pReady ) };
            }
            // 들고는 있지만 GPU 자원이 없다(리로드 실패 · CPU 텍스처의 참조가 0 이 되어 내린 뒤) — 같은 객체에 다시 올린다.
            entry._bLoading = true;
            pTexture        = entry._texture.get();
            break;
        }

        // 렌더 텍스처는 파일이 없다. 알린 크기(없으면 기본)로 렌더 타깃을 만든다. CPU 텍스처는 자기 원본을 올린다.
        if ( isRenderTargetPath( key ) )
            source = TextureLoadBlock::Source::RenderTarget;
        else if ( pTexture->hasCPUBytes() )
            source = TextureLoadBlock::Source::CPUBytes;
        shared_ptr<TextureLoadBlock> pBlock = sw::make_shared<TextureLoadBlock>( key, pDevice, source, pTexture, _impl );
        // 비동기 로드(씬의 머티리얼)는 아래쪽 밉부터 — 위쪽 밉은 렌더 스레드가 뒤에 업로드한다. 동기 로드(도구 · 미리보기)는 받는 자리에서 밉 전부를 쓴다.
        const bool bStreamed = start == AssetLoadStart::Async && gv_textureStreaming != 0;
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
        const TextureHandle         handle   = request( relativePath, pDevice, AssetLoadStart::OnCallingThread );
        const shared_ptr<Texture2D> pTexture = handle.wait();
        if ( pTexture == nullptr )
            return nullptr;
        // 핸들 몫의 참조는 이 함수를 나가며 놓인다 — 돌려주는 포인터 몫(짝 `release`)을 하나 더 센다. 핸들이 쥐고 있으므로 항목은 있다.
        const string                        key = FileUtil::normalizePath( relativePath );
        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        const auto                          it = _impl->_mapEntry.find( key );
        SW_ASSERT( it != _impl->_mapEntry.end() );
        ++it->second._refCount;
        return pTexture.get();
    }

    uint32 TextureCache::completePendingLoads( IRHIDevice* pDevice, uint32 maxCount )
    {
        if ( pDevice == nullptr )
            return 0;
        vector<shared_ptr<TextureLoadBlock>> listBlock;
        {
            std::scoped_lock<mutex> lock{ _impl->_awaitingMutex };
            const size_t            takeCount = MathUtil::min( static_cast<size_t>( maxCount ), _impl->_listAwaitingUpload.size() );
            if ( takeCount == 0 )
                return 0;
            listBlock.assign( std::make_move_iterator( _impl->_listAwaitingUpload.begin() ), std::make_move_iterator( _impl->_listAwaitingUpload.begin() + static_cast<ptrdiff_t>( takeCount ) ) );
            _impl->_listAwaitingUpload.erase( _impl->_listAwaitingUpload.begin(), _impl->_listAwaitingUpload.begin() + static_cast<ptrdiff_t>( takeCount ) );
        }
        // 이미 다른 스레드가 기다리며(`wait`) 올렸으면 건너뛴다 — 완료 단계는 먼저 집은 쪽 하나만 실행한다.
        uint32 completedCount = 0;
        for ( const shared_ptr<TextureLoadBlock>& pBlock : listBlock )
        {
            pBlock->_pDevice.store( pDevice, std::memory_order_release );
            if ( pBlock->runCompletionIfPending() )
                ++completedCount;
        }
        return completedCount;
    }

    uint32 TextureCache::updateStreaming( IRHIDevice* pDevice, uint32 maxCount )
    {
        if ( pDevice == nullptr || maxCount == 0 )
            return 0;
        vector<Impl::StreamingUpgrade> listUpgrade;
        {
            std::scoped_lock<mutex> lock{ _impl->_streamingMutex };
            const size_t            takeCount = MathUtil::min( static_cast<size_t>( maxCount ), _impl->_listStreamingUpgrade.size() );
            if ( takeCount == 0 )
                return 0;
            listUpgrade.assign( std::make_move_iterator( _impl->_listStreamingUpgrade.begin() ),
                                std::make_move_iterator( _impl->_listStreamingUpgrade.begin() + static_cast<ptrdiff_t>( takeCount ) ) );
            _impl->_listStreamingUpgrade.erase( _impl->_listStreamingUpgrade.begin(), _impl->_listStreamingUpgrade.begin() + static_cast<ptrdiff_t>( takeCount ) );
        }
        SW_PROFILE_SCOPE( "RT.TextureStreaming" );
        uint32 upgradedCount = 0;
        for ( Impl::StreamingUpgrade& upgrade : listUpgrade )
        {
            Texture2D* pTexture = nullptr;
            {
                std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
                const auto                          it = _impl->_mapEntry.find( upgrade._key );
                // 놓였으면 버린다. 다시 읽는 중(`reload` · 디바이스 교체)이면 그쪽이 밉 전부를 올린다.
                const bool bUpgradable = it != _impl->_mapEntry.end() && it->second._bLoading == false && it->second._bPublished && it->second._texture != nullptr &&
                                         it->second._texture->isFullyResident() == false;
                if ( bUpgradable == false )
                    continue;
                it->second._bUpgrading = true;
                pTexture               = it->second._texture.get();
            }
            // 업로드는 잠금 밖에서 — 게임 스레드의 find · acquire 를 붙잡지 않는다. 표시가 항목을 지키고(놓여도 지우지 않는다), 다시 읽기는 끝을 기다린다.
            Texture2DRetiredResource retired{};
            const bool               bUpgraded = pTexture->upgradeResidency( pDevice, *upgrade._pImage, retired );
            {
                std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
                const auto                          it = _impl->_mapEntry.find( upgrade._key );
                SW_ASSERT( it != _impl->_mapEntry.end() && it->second._bUpgrading );
                it->second._bUpgrading = false;
                if ( bUpgraded )
                    _impl->_retired.add( pDevice, retired );
                // 업로드하는 사이 마지막 참조가 놓였으면 여기서 내린다(`reload` 와 같다).
                if ( it->second._refCount == 0 && it->second._bPinned == false )
                {
                    pTexture->releaseRHI( pDevice );
                    _impl->_mapEntry.erase( it );
                }
            }
            if ( bUpgraded )
            {
                ++upgradedCount;
                // 새 SRV 번호가 생겼다 — 빌려 간 머티리얼이 다시 받게 한다(표시를 내린 뒤에 올린다: `reload` 와 같은 순서).
                _impl->_reloadGeneration.fetch_add( 1u, std::memory_order_acq_rel );
            }
            _impl->_cvLoaded.notify_all();
        }
        return upgradedCount;
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
            std::scoped_lock<mutex> lock{ _impl->_streamingMutex };
            count += static_cast<uint32>( _impl->_listStreamingUpgrade.size() );
        }
        std::shared_lock<std::shared_mutex> lock{ _impl->_mutex };
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
        const string                        key = FileUtil::normalizePath( relativePath );
        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
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
        const string                        key = FileUtil::normalizePath( relativePath );
        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        const auto                          it = _impl->_mapEntry.find( key );
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
        const string                        key = FileUtil::normalizePath( relativePath );
        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        Impl::RenderTargetSize&             size = _impl->_mapRenderTargetSize[key];
        size._width                              = width;
        size._height                             = height;
    }

    bool TextureCache::resizeRenderTarget( string_view relativePath, uint32 width, uint32 height, IRHIDevice* pDevice )
    {
        SW_MEMORY_SCOPE( Texture );
        const bool bInvalidRenderTargetRequest = _impl == nullptr || pDevice == nullptr || isRenderTargetPath( relativePath ) == false || width == 0 || height == 0;
        if ( bInvalidRenderTargetRequest )
            return false;
        const string key = FileUtil::normalizePath( relativePath );

        // 크기를 먼저 알린다 — 아직 만들지 않았으면 다음 acquire 가 이 크기로 만든다. 다시 만드는 일은 reload 와 같이 잠금 밖에서 한다.
        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        Impl::RenderTargetSize&             size = _impl->_mapRenderTargetSize[key];
        size._width                              = width;
        size._height                             = height;
        Texture2D* pTexture                      = nullptr;
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

        // 이전 프레임이 옛 텍스처와 그 bindless 인덱스를 읽고 있을 수 있다(DX11 · GL 은 인덱스를 곧바로 돌려준다) — reload 와 같은 이유로 기다린다.
        pDevice->waitIdle();
        pTexture->releaseRHI( pDevice );
        const bool bReady = pTexture->createRenderTarget( pDevice, key, width, height, constant::kOffscreenColorFormat );
        if ( bReady == false )
            SW_LOG_ERROR( "Failed to resize render texture %# to %#x%#", key.c_str(), width, height );

        lock.lock();
        const auto it = _impl->_mapEntry.find( key );
        SW_ASSERT( it != _impl->_mapEntry.end() && it->second._bLoading );
        it->second._bLoading = false;
        if ( it->second._refCount == 0 )
        {
            pTexture->releaseRHI( pDevice );
            _impl->_mapEntry.erase( it );
        }
        lock.unlock();
        // 새 SRV 인덱스를 받았다 — 빌려 간 머티리얼이 다시 받게 한다(reload 와 같은 순서: 진행 중 표시를 내린 뒤에 올린다).
        _impl->_reloadGeneration.fetch_add( 1u, std::memory_order_acq_rel );
        _impl->_cvLoaded.notify_all();
        return bReady;
    }

    void TextureCache::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        SW_MEMORY_SCOPE( Texture );
        // 렌더 텍스처 · CPU 텍스처는 디스크에 없다 — 다시 읽을 것이 없다.
        const bool bNotOnDisk = relativePath.empty() || isRenderTargetPath( relativePath ) || isCPUTexturePath( relativePath );
        const bool bCanReload = _impl != nullptr && pDevice != nullptr;
        if ( bNotOnDisk || bCanReload == false )
            return;
        const string key = FileUtil::normalizePath( relativePath );

        // 기다림(waitIdle)과 다시 올리기는 잠금 밖에서 한다. 잠금을 쥔 채 waitIdle 을 부르면 같은 잠금을 기다리는
        // 렌더 스레드와 서로 기다릴 수 있다. 진행 중 표시를 세워 두면 그사이 acquire 는 기다리고 release 는 지우지 않는다.
        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        Texture2D*                          pTexture = nullptr;
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
        pDevice->waitIdle();
        pTexture->releaseRHI( pDevice );
        if ( pTexture->loadFromResource( pDevice, key ) == false )
            SW_LOG_ERROR( "Hot-Reload failed for Texture %#", key.c_str() );

        lock.lock();
        const auto it = _impl->_mapEntry.find( key );
        SW_ASSERT( it != _impl->_mapEntry.end() && it->second._bLoading );
        it->second._bLoading = false;
        // 다시 올리는 사이 마지막 참조가 놓였으면 release 대신 여기서 내린다.
        if ( it->second._refCount == 0 )
        {
            pTexture->releaseRHI( pDevice );
            _impl->_mapEntry.erase( it );
        }
        lock.unlock();
        // 다시 올린 텍스처는 새 SRV 인덱스를 받았다(실패했으면 인덱스가 없다). 옛 인덱스는 이미 돌려줬으므로 빌려 간 머티리얼이 다시 받게 한다.
        // 세대는 진행 중 표시를 내린 **뒤에** 올린다 — 먼저 올리면 새 세대를 본 쪽의 find 가 아직 nullptr 를 받고 그 세대를 소비해 버린다.
        _impl->_reloadGeneration.fetch_add( 1u, std::memory_order_acq_rel );
        _impl->_cvLoaded.notify_all();
    }

    const Texture2D* TextureCache::find( string_view relativePath ) const
    {
        if ( relativePath.empty() || _impl == nullptr )
            return nullptr;
        const string                        key = FileUtil::normalizePath( relativePath );
        std::shared_lock<std::shared_mutex> lock{ _impl->_mutex };
        const auto                          it = _impl->_mapEntry.find( key );
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
        std::scoped_lock<mutex> lock{ _impl->_regionMutex };
        _impl->_listPendingRegion.push_back( std::move( update ) );
    }

    uint32 TextureCache::flushRegionUpdates( IRHIDevice* pDevice )
    {
        if ( _impl == nullptr )
            return 0;
        {
            std::scoped_lock<mutex> lock{ _impl->_regionMutex };
            if ( _impl->_listPendingRegion.empty() )
                return 0;
            _impl->_listFlushingRegion.swap( _impl->_listPendingRegion );
        }
        uint32                     uploadedCount{ 0 };
        vector<Impl::RegionUpdate> listRetry;
        for ( Impl::RegionUpdate& update : _impl->_listFlushingRegion )
        {
            // 올리는 동안 같은 텍스처를 놓거나 다시 올리지 못하게 읽기 잠금을 쥔다(release · reload 는 쓰기 잠금을 잡는다).
            std::shared_lock<std::shared_mutex> lock{ _impl->_mutex };
            const auto                          it     = _impl->_mapEntry.find( update._key );
            Texture2D*                          pFound = it != _impl->_mapEntry.end() ? it->second._texture.get() : nullptr;
            RHITextureRegionUploadDesc          desc{};
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
                    listRetry.push_back( std::move( update ) );
                else if ( pFound->writeCPUBytes( pDevice, desc ) )
                    ++uploadedCount;
                continue;
            }
            const bool bResident = pFound != nullptr && it->second._bPublished && it->second._bLoading == false && pFound->isRHIValid();
            if ( bResident == false || pDevice == nullptr )
            {
                SW_LOG_WARNING( "Region update for texture %# dropped - the texture is not resident", update._key.c_str() );
                continue;
            }
            const Texture2D& texture = *pFound;
            if ( texture.getFormat() == RHIFormat::B8G8R8A8_UNORM )
            {
                for ( size_t offset = 0; offset + 3 < update._rgbaBytes.size(); offset += 4 )
                {
                    std::swap( update._rgbaBytes[offset], update._rgbaBytes[offset + 2] );
                }
            }
            else if ( texture.getFormat() != RHIFormat::R8G8B8A8_UNORM )
            {
                SW_LOG_WARNING( "Region update for texture %# dropped - only RGBA8 and BGRA8 textures take RGBA8 bytes", update._key.c_str() );
                continue;
            }
            if ( pDevice->getResourceFactory()->uploadTexture2DRegion( texture.getHandle(), desc ) )
                ++uploadedCount;
        }
        _impl->_listFlushingRegion.clear();
        if ( listRetry.empty() == false )
        {
            // 미룬 것은 새로 들어온 것보다 앞에 둔다 — 같은 자리를 다시 칠했으면 나중 것이 이긴다.
            std::scoped_lock<mutex> lock{ _impl->_regionMutex };
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

        const string                        key = FileUtil::normalizePath( relativePath );
        std::shared_lock<std::shared_mutex> lock{ _impl->_mutex };
        return _impl->_mapEntry.find( key ) != _impl->_mapEntry.end();
    }

    size_t TextureCache::getCachedCount() const
    {
        if ( _impl == nullptr )
            return 0;
        std::shared_lock<std::shared_mutex> lock{ _impl->_mutex };
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
            std::scoped_lock<mutex> awaitingLock{ _impl->_awaitingMutex };
            _impl->_listAwaitingUpload.clear(); // 올리지 않은 로드는 버린다 — 그 블록을 쥔 핸들은 `Loading` 으로 남는다(종료 때만 온다)
        }
        {
            std::scoped_lock<mutex> streamingLock{ _impl->_streamingMutex };
            _impl->_listStreamingUpgrade.clear(); // 위쪽 밉을 기다리던 텍스처도 함께 사라진다
        }
        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        _impl->_mapEntry.clear();
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
        if ( Texture2D::readImage( getPath(), *_pImage ) == false )
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
        std::scoped_lock<mutex> lock{ pCache->_awaitingMutex };
        // 단계를 알린 뒤라 기다리던 스레드가 이미 올렸을 수 있다 — 그러면 넣지 않는다(넣으면 펌프가 없는 동안 블록이 참조를 쥐고 남는다).
        // 완료 본문은 같은 잠금 안에서 목록에서 빼므로, 여기서 아직 기다리는 중이면 넣은 것을 그쪽이 뺀다.
        if ( isAwaitingCompletion() == false )
            return;
        pCache->_listAwaitingUpload.push_back( shared_from_this() );
    }

    bool TextureLoadBlock::runCompletion( string& outError )
    {
        SW_MEMORY_SCOPE( Texture );
        const shared_ptr<TextureCache::Impl> pCache = _pCache.lock();
        if ( pCache == nullptr )
        {
            outError = "the texture cache was shut down before the upload";
            return false;
        }
        {
            // 펌프가 아닌 쪽(`wait`)이 먼저 올렸으면 대기 목록에서 뺀다 — 목록이 블록(곧 참조)을 쥔 채 남지 않게(렌더가 없는 테스트는 펌프가 돌지 않는다).
            std::scoped_lock<mutex>               lock{ pCache->_awaitingMutex };
            vector<shared_ptr<TextureLoadBlock>>& listAwaiting = pCache->_listAwaitingUpload;
            for ( size_t index = 0; index < listAwaiting.size(); ++index )
            {
                if ( listAwaiting[index].get() != this )
                    continue;
                listAwaiting.erase( listAwaiting.begin() + static_cast<ptrdiff_t>( index ) );
                break;
            }
        }
        bool              bReady  = false;
        IRHIDevice* const pDevice = _pDevice.load( std::memory_order_acquire );
        if ( _loadError.empty() )
        {
            switch ( _source )
            {
                case Source::File:
                {
                    bReady = _pTexture->loadFromImage( pDevice, getPath(), *_pImage, _residency );
                    // 위쪽 밉이 남았으면 읽은 이미지를 스트리밍 줄로 넘긴다 — 다시 읽지 않고 렌더 스레드가 프레임 예산 안에서 업로드한다.
                    if ( bReady && _pTexture->isFullyResident() == false )
                        pCache->queueStreamingUpgrade( getPath(), std::move( _pImage ) );
                    break;
                }
                case Source::RenderTarget:
                {
                    bReady = _pTexture->createRenderTarget( pDevice, getPath(), _renderTargetWidth, _renderTargetHeight, constant::kOffscreenColorFormat );
                    break;
                }
                case Source::CPUBytes:
                {
                    bReady = _pTexture->loadFromCPUBytes( pDevice );
                    break;
                }
            }
            if ( bReady == false )
                _loadError = "the GPU upload failed";
        }
        _pImage.reset();
        pCache->finishLoad( getPath(), bReady );
        if ( bReady == false )
        {
            outError = _loadError;
            return false;
        }
        _bHoldsReference = true;
        return true;
    }
} // namespace sw

namespace sw
{
    SW_BUILT_IN_ASSET_CACHE( TextureCache );
} // namespace sw
