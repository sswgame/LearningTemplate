#include "pch.h"

#include "Engine/Graphics/Mesh/MeshCache.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/MemoryProfiler.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Resource/AssetLoadProfiler.h"
#include "Engine/Resource/WeakInternTable.h"

namespace sw
{
    SW_LOG_CALLER( "MeshCache" );

    namespace
    {
        struct MeshCacheInternal
        {
            /** @brief 이미 경고한 경로입니다. 잠금과 함께 둡니다. */
            struct WarnedPathSet
            {
                mutex                 _mutex;
                unordered_set<string> _uniquePath;
            };

            /** @brief 프로세스에 하나인 공유 표입니다(Engine.dll 안 — 모듈 핫 리로드에 사라지지 않습니다). 경로 → 약한 참조. */
            static WeakInternTable<string, Mesh>& getSharedTable()
            {
                static WeakInternTable<string, Mesh> s_table;
                return s_table;
            }

            static WarnedPathSet& getWarnedPathSet()
            {
                static WarnedPathSet s_set;
                return s_set;
            }

            /** @brief 표의 키입니다. 리소스 id 는 소문자로 찾으므로(`normalizePath`) 핫 리로드가 넘긴 철자도 같은 칸에 닿습니다. */
            static string makeKey( string_view path ) { return FileUtil::normalizePath( path ); }

            /** @brief 그 경로를 지금 쥔 메시입니다. 없으면 nullptr 입니다. */
            static shared_ptr<Mesh> findLive( string_view path ) { return getSharedTable().findLive( makeKey( path ) ); }

            /** @brief 읽지 못한 경로를 처음 한 번만 경고합니다. */
            static void warnLoadFailureOnce( string_view path )
            {
                WarnedPathSet&          warned = getWarnedPathSet();
                std::scoped_lock<mutex> lock{ warned._mutex };
                if ( warned._uniquePath.insert( makeKey( path ) ).second )
                    SW_LOG_WARNING( "Mesh asset '%#' could not be loaded - nothing is drawn for it", path );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    shared_ptr<Mesh> MeshCache::acquire( string_view path )
    {
        SW_MEMORY_SCOPE( Mesh );
        if ( path.empty() )
            return nullptr;

        shared_ptr<Mesh> live = MeshCacheInternal::findLive( path );
        if ( live != nullptr )
            return live;

        // 읽기는 잠금 밖에서 한다(파일 IO). 둘이 같은 경로를 동시에 읽으면 먼저 넣은 쪽이 남고 다른 쪽은 그것을 받는다.
        MeshAssetData  data{};
        AssetLoadScope loadScope( "Mesh", path );
        if ( MeshAssetFormat::loadFromResource( path, data ) == false )
        {
            MeshCacheInternal::warnLoadFailureOnce( path );
            return nullptr;
        }
        loadScope.setBytes( data._listVertex.size() * sizeof( RHIVertex ) + data._listSkinVertex.size() * sizeof( MeshSkinVertex ) );
        loadScope.setSucceeded();
        shared_ptr<Mesh> loaded = Mesh::create();
        loaded->setVertices( std::move( data._listVertex ) );
        if ( data._skinBoneCount > 0 )
            loaded->setSkin( std::move( data._listSkinVertex ), data._skinBoneCount );
        if ( data._listMorphTarget.empty() == false )
            loaded->setMorphTargets( std::move( data._listMorphTarget ) );

        return MeshCacheInternal::getSharedTable().insertOrGetLive( MeshCacheInternal::makeKey( path ), std::move( loaded ) );
    }

    bool MeshCache::reloadShared( string_view path, IRHIDevice* pDevice )
    {
        if ( path.empty() )
            return false;
        shared_ptr<Mesh> live = MeshCacheInternal::findLive( path );
        if ( live == nullptr )
            return false;

        // 읽기에 실패하면 옛 정점을 지킨다(반쯤 쓴 파일을 저장 중에 본 경우 — 다음 감시 이벤트가 다시 읽는다).
        MeshAssetData data{};
        if ( MeshAssetFormat::loadFromResource( path, data ) == false )
        {
            SW_LOG_ERROR( "Hot-Reload failed for Mesh %#", path );
            return false;
        }

        // 지난 프레임이 아직 옛 정점 버퍼로 그리고 있을 수 있다. setVertices 는 버퍼를 곧바로 돌려주므로 먼저 기다린다(TextureCache::reload 와 같은 이유).
        if ( pDevice != nullptr )
            pDevice->waitIdle();
        live->setVertices( std::move( data._listVertex ) );
        live->setSkin( std::move( data._listSkinVertex ), data._skinBoneCount );
        live->setMorphTargets( std::move( data._listMorphTarget ) );
        return true;
    }

    bool MeshCache::isCached( string_view relativePath ) const
    {
        return MeshCacheInternal::findLive( relativePath ) != nullptr;
    }

    void MeshCache::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        (void)reloadShared( relativePath, pDevice ); // 쥔 쪽이 없으면 다시 읽을 것이 없다 — 다음에 읽는 쪽이 새 내용을 읽는다
    }

    size_t MeshCache::getCachedCount() const
    {
        return MeshCacheInternal::getSharedTable().countLive();
    }

    void MeshCache::clear()
    {
        MeshCacheInternal::getSharedTable().clear();
        MeshCacheInternal::WarnedPathSet& warned = MeshCacheInternal::getWarnedPathSet();
        std::scoped_lock<mutex>           lock{ warned._mutex };
        warned._uniquePath = unordered_set<string>{};
    }
} // namespace sw
