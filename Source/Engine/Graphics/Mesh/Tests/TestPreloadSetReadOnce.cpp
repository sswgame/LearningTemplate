#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/File/ContentFileSource.h"
#include "Core/File/FileUtil.h"
#include "Core/TestFramework/TestFramework.h"
#include "Core/TestFramework/TestWait.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/AssetStreamingQueue.h"
#include "Engine/Asset/PreloadSet.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"

using namespace sw;

// PreloadSetTest — (메시 층에 둔다: 메시 파일을 지어야 한다) 프리로드 세트: 고정하는 종류(메시)는 캐시의 로드가 곧 읽기라 파일을 한 번만 읽는다(감사 A-19). 읽기는 지금 콘텐츠 소스를 감싼 가짜가 센다(nogpu).

namespace
{
    /** @brief 지금 콘텐츠 소스에 넘기며 이름이 @p fileName 으로 끝나는 파일의 읽기(통째 · 글 · 비동기 · 구간)를 셉니다. 열기(크기 찾기)는 읽기가 아니다. */
    class CountingContentFileSource final : public IContentFileSource
    {
    public:
        CountingContentFileSource( const IContentFileSource& inner, string_view fileName )
            : _inner{ inner }
            , _fileName{ fileName }
            , _readCount{ 0 }
        {
        }

        uint32 getReadCount() const { return _readCount.load(); }

        AsyncFileHandle open( string_view path ) const override { return _inner.open( path ); }
        bool            isFile( string_view path ) const override { return _inner.isFile( path ); }
        bool            isFolder( string_view path ) const override { return _inner.isFolder( path ); }
        bool            collectFiles( string_view folder, vector<string>& outListPath ) const override { return _inner.collectFiles( folder, outListPath ); }
        bool            readFile( string_view path, vector<uint8>& outBytes ) const override
        {
            count( path );
            return _inner.readFile( path, outBytes );
        }
        bool readTextFile( string_view path, string& outText ) const override
        {
            count( path );
            return _inner.readTextFile( path, outText );
        }
        AsyncReadHandle readFileAsync( AsyncFileIO& io, string_view path, AsyncIOPriority priority, const AsyncReadCompleteDelegate& onComplete ) const override
        {
            count( path );
            return _inner.readFileAsync( io, path, priority, onComplete );
        }
        AsyncReadHandle readRangeAsync( AsyncFileIO& io, string_view path, uint64 offset, uint64 size, AsyncIOPriority priority,
                                        const AsyncReadCompleteDelegate& onComplete ) const override
        {
            count( path );
            return _inner.readRangeAsync( io, path, offset, size, priority, onComplete );
        }

    private:
        void count( string_view path ) const
        {
            if ( path.size() >= _fileName.size() && path.substr( path.size() - _fileName.size() ) == _fileName )
                ++_readCount; // 읽기는 워커 · IO 스레드에서도 온다
        }

    private:
        const IContentFileSource& _inner;
        string                    _fileName;
        mutable atomic<uint32>    _readCount;
    };

    /** @brief 가짜 소스를 지금 소스로 걸고, 사라질 때 이전 소스로 되돌립니다. */
    class ScopedContentFileSource
    {
    public:
        explicit ScopedContentFileSource( const IContentFileSource& source )
            : _pPrevious{ ContentFileSource::bindCurrent( &source ) }
        {
        }
        ~ScopedContentFileSource() { (void)ContentFileSource::bindCurrent( _pPrevious ); }
        ScopedContentFileSource( const ScopedContentFileSource& )            = delete;
        ScopedContentFileSource& operator=( const ScopedContentFileSource& ) = delete;

    private:
        const IContentFileSource* _pPrevious;
    };

    /** @brief 삼각형 하나짜리 `.mesh` 내용입니다. */
    MeshAssetData makeTriangle()
    {
        MeshAssetData data{};
        data._listVertex.resize( 3 );
        const float32 arrPosition[3][2] = {
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {0.0f, 1.0f}
        };
        for ( uint32 index = 0; index < 3; ++index )
        {
            data._listVertex[index]._arrPosition[0] = arrPosition[index][0];
            data._listVertex[index]._arrPosition[1] = arrPosition[index][1];
            data._listVertex[index]._arrNormal[2]   = 1.0f;
            data._listVertex[index]._arrColor[3]    = 1.0f;
        }
        data._listIndex = vector<uint32>{ 0, 1, 2 };
        return data;
    }
} // namespace

/**
 * @brief [PreloadSetTest] 고정을 켠 프리로드는 메시 파일을 한 번만 읽고 그 메시를 붙든다
 * @details 예전에는 큐가 바이트를 읽어 버린 뒤 고정(`pinAsset`)의 로드가 같은 파일을 다시 읽었다(읽기 2).
 */
SW_TEST_CASE( PreloadSetTest, PinnedAssetIsReadOnce )
{
    constexpr const utf8*           kFileName = "preloadreadonce.mesh";
    const test::ScopedScratchDomain domain;
    vector<uint8>                   bytes;
    MeshAssetFormat::makeBytes( makeTriangle(), bytes );
    SW_REQUIRE_TRUE( FileUtil::writeFile( domain.makeDiskPath( kFileName ), bytes.data(), bytes.size() ) );
    const string path = test::ScopedScratchDomain::makeID( kFileName ).str();

    const CountingContentFileSource counter{ ContentFileSource::getCurrent(), kFileName };
    const ScopedContentFileSource   bind{ counter };

    AssetStreamingQueue queue;
    queue.initialize();
    {
        PreloadSet preload{ vector<string>{ path }, StreamingPriority::Normal, 4 };
        preload.enablePinning( engine::getSubsystem<AssetManager>(), uint64{ 1 } << 20 );
        SW_REQUIRE_TRUE( test::waitUntil( [&]()
        {
            preload.update( queue );
            queue.update();
            return preload.isComplete();
        } ) );

        SW_EXPECT_TRUE( preload.getFailedAssets().empty() );
        SW_REQUIRE_TRUE( preload.findPins() != nullptr );
        SW_EXPECT_TRUE( preload.findPins()->contains( path ) );
        SW_EXPECT_EQUAL( static_cast<uint64>( bytes.size() ), preload.findPins()->getPinnedBytes() );
        SW_EXPECT_EQUAL( 1u, counter.getReadCount() );
    }
    queue.shutdown();
}
