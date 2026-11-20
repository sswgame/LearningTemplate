#include "pch.h"

#include "DevTools/Profiling/Memory/MemorySnapshotSources.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Diagnostics/CallStackCapture.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/Registry/RegistrationList.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "DevTools/Diagnostics/DevSceneIterationAccess.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/AssetCacheBase.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIMemoryTracker.h"
#include "Engine/Graphics/RHI/RHIFormatInfo.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    namespace
    {
        struct MemorySnapshotSourcesInternal
        {
            /** @brief 글 칸 하나입니다. */
            static MemoryCell text( string_view value ) { return MemoryCell{ string{ value }, 0 }; }
            /** @brief 숫자 칸 하나입니다. */
            static MemoryCell number( uint64 value ) { return MemoryCell{ string{}, value }; }

            /** @brief 키 공간의 표시 이름입니다(GPU 자원 줄의 정체 글). */
            static const utf8* getKeySpaceName( RHIMemoryKeySpace space )
            {
                switch ( space )
                {
                    case RHIMemoryKeySpace::Buffer:
                        return "Buffer";
                    case RHIMemoryKeySpace::Texture:
                        return "Texture";
                    case RHIMemoryKeySpace::DeviceObject:
                        return "Device";
                    case RHIMemoryKeySpace::MaxSpaces:
                        break;
                }
                return "Invalid";
            }

            /** @brief 기호를 푼 콜스택 여러 줄을 한 줄로 접습니다(표 칸 · CSV 칸 하나). 빈 줄과 앞뒤 공백은 버린다. */
            static string foldStackText( string_view stackText )
            {
                string folded;
                size_t start = 0;
                while ( start <= stackText.size() )
                {
                    size_t end = stackText.find( '\n', start );
                    if ( end == string_view::npos )
                        end = stackText.size();
                    const string_view line = StringUtil::trim( stackText.substr( start, end - start ) );
                    if ( line.empty() == false )
                    {
                        if ( folded.empty() == false )
                            folded += " < ";
                        folded += line;
                    }
                    start = end + 1;
                }
                return folded;
            }

            /** @brief 형식 이름 하나의 수 · 바이트입니다(형식 시트의 한 줄). */
            struct TypeCount
            {
                uint64 _count{ 0 };
                uint64 _bytes{ 0 };
            };

            /** @brief @p mapNameToCount 를 이름 순 줄로 시트에 붙입니다. */
            static void appendTypeRows( string_view category, const unordered_map<string, TypeCount>& mapNameToCount, MemorySheet& outSheet )
            {
                vector<string> listName;
                listName.reserve( mapNameToCount.size() );
                for ( const auto& [name, count] : mapNameToCount )
                {
                    listName.push_back( name );
                }
                VectorUtil::sort( listName );
                for ( const string& name : listName )
                {
                    const TypeCount& count = mapNameToCount.find( name )->second;
                    outSheet._listRow.push_back( MemoryRow{
                        { text( category ), text( name ), number( count._count ), number( count._bytes ) }
                    } );
                }
            }

            /** @brief @p pType 한 개를 @p inoutMap 에 셉니다. 리플렉션 형식이 없으면 `<unreflected>` 로 센다. */
            static void countType( const TypeInfo* pType, unordered_map<string, TypeCount>& inoutMap )
            {
                TypeCount& count = inoutMap[pType != nullptr ? string{ pType->_name.view() } : string{ "<unreflected>" }];
                ++count._count;
                count._bytes += pType != nullptr ? static_cast<uint64>( pType->_size ) : 0u;
            }
        };
    } // namespace

    MemorySnapshot MemorySnapshotSources::collect( string_view label, string_view source ) const
    {
        MemorySnapshot snapshot{};
        snapshot._label  = string{ label };
        snapshot._source = string{ source };
        collectTags( snapshot.getSheet( MemorySheetKind::Tags ) );
        collectAllocationSites( snapshot.getSheet( MemorySheetKind::AllocationSites ) );
        collectAssets( snapshot.getSheet( MemorySheetKind::Assets ) );
        snapshot._gpuLiveBytes = collectGPUResources( snapshot.getSheet( MemorySheetKind::GPUResources ) );
        collectTypes( snapshot.getSheet( MemorySheetKind::Types ) );
        collectCaches( snapshot.getSheet( MemorySheetKind::Caches ) );
        for ( const MemoryRow& row : snapshot.getSheet( MemorySheetKind::Tags )._listRow )
        {
            snapshot._cpuLiveBytes += row._listCell[1]._value; // Live 칸
        }
        if ( _pGPUTracker != nullptr )
        {
            snapshot._gpuDescription = _gpuBackendName;
            snapshot._gpuDescription += " (";
            snapshot._gpuDescription += RHIMemoryTracker::getSizeBasisName( _pGPUTracker->getSizeBasis() );
            snapshot._gpuDescription += " size)";
        }
        return snapshot;
    }

    void MemorySnapshotSources::collectTags( MemorySheet& outSheet ) const
    {
        if ( _pProfiler == nullptr )
            return;
        using Internal = MemorySnapshotSourcesInternal;
        outSheet._listRow.reserve( kMemoryTagCount );
        for ( uint32 tagIndex = 0; tagIndex < kMemoryTagCount; ++tagIndex )
        {
            const MemoryTag          tag   = static_cast<MemoryTag>( tagIndex );
            const MemoryProfileStats stats = _pProfiler->getStats( tag );
            outSheet._listRow.push_back( MemoryRow{
                { Internal::text( MemoryProfiler::getMemoryTagName( tag ) ), Internal::number( stats._currentAllocatedBytes ),
                 Internal::number( stats._peakAllocatedBytes ), Internal::number( stats._budgetBytes ),
                 Internal::number( stats._currentAllocationCount ), Internal::number( stats._totalAllocationCount ) }
            } );
        }
    }

    void MemorySnapshotSources::collectAllocationSites( MemorySheet& outSheet ) const
    {
        // 콜스택 집계는 세부 추적이 켜져 있을 때만 찬다(`memory.tracking` 과 별개 — 언리얼 Memory Insights 의 콜스택 추적처럼 비용이 커서 늘 켜지 않는다).
        if ( _pProfiler == nullptr || _pProfiler->isDetailedTrackingEnabled() == false || _allocationSiteLimit == 0 )
            return;
        using Internal                            = MemorySnapshotSourcesInternal;
        const vector<CallStackAllocInfo> listSite = _pProfiler->getTopCallStacks( TopCallStackOrder::LiveBytes );
        const size_t                     rowCount = listSite.size() < _allocationSiteLimit ? listSite.size() : _allocationSiteLimit;
        outSheet._listRow.reserve( rowCount );
        for ( size_t index = 0; index < rowCount; ++index )
        {
            const CallStackAllocInfo& site = listSite[index];
            // 기호는 여기서 푼다 — 원격 스냅숏은 주소가 아니라 글로 와야 한다(받는 프로세스의 모듈 배치가 다르다).
            outSheet._listRow.push_back( MemoryRow{
                { Internal::text( Internal::foldStackText( CallStackCapture::symbolize( site._stack ) ) ), Internal::number( site._currentBytes ),
                 Internal::number( site._currentCount ), Internal::number( site._totalBytes ), Internal::number( site._totalCount ) }
            } );
        }
    }

    void MemorySnapshotSources::collectAssets( MemorySheet& outSheet ) const
    {
        using Internal = MemorySnapshotSourcesInternal;
        vector<AssetMemoryEntry> listEntry;
        for ( const AssetCacheBase* pCache : _listAssetCache )
        {
            if ( pCache == nullptr )
                continue;
            listEntry.clear();
            pCache->collectMemoryEntries( listEntry );
            const string_view kindName = pCache->getAssetKindName();
            for ( const AssetMemoryEntry& entry : listEntry )
            {
                outSheet._listRow.push_back( MemoryRow{
                    { Internal::text( kindName ), Internal::text( entry._path ), Internal::number( entry._cpuBytes + entry._gpuBytes ),
                     Internal::number( entry._cpuBytes ), Internal::number( entry._gpuBytes ), Internal::number( entry._referenceCount ) }
                } );
            }
        }
    }

    uint64 MemorySnapshotSources::collectGPUResources( MemorySheet& outSheet ) const
    {
        if ( _pGPUTracker == nullptr )
            return 0;
        using Internal = MemorySnapshotSourcesInternal;
        vector<RHIMemoryResourceEntry> listEntry;
        _pGPUTracker->collectResources( listEntry );
        // 표 순서는 정체 글 순 — 해시 표의 순서가 파일 · 화면에 나오지 않게.
        VectorUtil::sort( listEntry, []( const RHIMemoryResourceEntry& lhs, const RHIMemoryResourceEntry& rhs )
        { return lhs._key._space != rhs._key._space ? lhs._key._space < rhs._key._space : lhs._key._id < rhs._key._id; } );
        uint64 totalBytes{ 0 };
        outSheet._listRow.reserve( listEntry.size() );
        for ( const RHIMemoryResourceEntry& entry : listEntry )
        {
            StringBuilder<constant::buffer::kMax64> resource;
            if ( entry._key._space == RHIMemoryKeySpace::DeviceObject )
                resource.appendFormat( "%s %llx", Internal::getKeySpaceName( entry._key._space ), entry._key._id );
            else
                resource.appendFormat( "%s %llu", Internal::getKeySpaceName( entry._key._space ), entry._key._id );
            const bool        bTexture = entry._key._space == RHIMemoryKeySpace::Texture;
            const string_view format   = bTexture ? string_view{ RHIFormatUtil::getName( entry._shape._format ) } : string_view{};
            outSheet._listRow.push_back( MemoryRow{
                { Internal::text( resource.view() ), Internal::text( entry._label ), Internal::text( RHIMemoryTracker::getKindName( entry._kind ) ),
                 Internal::text( format ), Internal::number( entry._shape._width ), Internal::number( entry._shape._height ),
                 Internal::number( entry._shape._arraySize ), Internal::number( entry._shape._mipLevels ), Internal::number( entry._bytes ) }
            } );
            totalBytes += entry._bytes;
        }
        return totalBytes;
    }

    void MemorySnapshotSources::collectTypes( MemorySheet& outSheet ) const
    {
        using Internal = MemorySnapshotSourcesInternal;
        unordered_map<string, Internal::TypeCount> mapObjectType;
        unordered_map<string, Internal::TypeCount> mapComponentType;
        for ( const GameObjectManager* pManager : _listObjectManager )
        {
            if ( pManager == nullptr )
                continue;
            // 공유 잠금 안에서는 이름을 세기만 한다(구조를 바꾸지 않는다 — `IterationScope`).
            pManager->forEachGameObject( DevSceneIterationAccess::getKey(), [&mapObjectType]( const GameObject* pObject )
            { Internal::countType( pObject->getTypeInfo(), mapObjectType ); } );
            pManager->forEachComponent( DevSceneIterationAccess::getKey(), [&mapComponentType]( const Component* pComponent )
            { Internal::countType( pComponent->findCachedTypeInfo(), mapComponentType ); } );
        }
        Internal::appendTypeRows( "GameObject", mapObjectType, outSheet );
        Internal::appendTypeRows( "Component", mapComponentType, outSheet );
    }

    void MemorySnapshotSources::collectCaches( MemorySheet& outSheet ) const
    {
        using Internal = MemorySnapshotSourcesInternal;
        for ( const CacheStatsSnapshot& stats : _listCacheStats )
        {
            // 항목 수를 레지스트리에 알리지 않는 캐시는 같은 이름의 에셋 캐시에게 묻는다(`CacheStatsSnapshot::_bItemCountKnown`).
            uint64 itemCount = stats._bItemCountKnown && stats._itemCount > 0 ? static_cast<uint64>( stats._itemCount ) : 0;
            if ( stats._bItemCountKnown == false )
            {
                for ( const AssetCacheBase* pCache : _listAssetCache )
                {
                    if ( pCache != nullptr && stats._name == pCache->getAssetKindName() )
                        itemCount = static_cast<uint64>( pCache->getCachedCount() );
                }
            }
            outSheet._listRow.push_back(
                MemoryRow{
                    { Internal::text( stats._name ), Internal::number( itemCount ), Internal::number( stats._hitCount ), Internal::number( stats._missCount ), Internal::number( stats._clearCount ) }
            } );
        }
    }

    MemorySnapshotSources MemorySnapshotSources::makeEngine( uint32 allocationSiteLimit )
    {
        MemorySnapshotSources sources{};
        sources._pProfiler           = MemoryProfiler::getActive();
        sources._allocationSiteLimit = allocationSiteLimit;
        CacheStats::collect( sources._listCacheStats );
        if ( engine::areEngineServicesBound() == false )
            return sources;
        if ( const AssetManager* pAssets = engine::findSubsystem<AssetManager>(); pAssets != nullptr )
        {
            for ( AssetCacheBase* pCache : pAssets->getAllAssetCache() )
            {
                sources._listAssetCache.push_back( pCache );
            }
        }
        if ( const SceneManager* pScenes = engine::findSubsystem<SceneManager>(); pScenes != nullptr )
        {
            vector<const Scene*> listScene;
            pScenes->collectAllScenes( listScene );
            for ( const Scene* pScene : listScene )
            {
                if ( pScene != nullptr && pScene->getObjectManager() != nullptr )
                    sources._listObjectManager.push_back( pScene->getObjectManager() );
            }
        }
        MemorySnapshotSourceRegistration::fillAll( sources ); // 구성마다 다른 원천(클라이언트의 GPU 자원)은 그 구성의 파일이 등록한다
        return sources;
    }

    namespace
    {
        struct MemorySnapshotSourceInternal
        {
            /** @brief 등록된 원천 채우기(등록 순서). DevTools 이미지의 함수 정적이다. */
            static SW_DEFINE_REGISTRATION_LIST( getRegistrations, const MemorySnapshotSourceRegistration, "MemorySnapshotSourceRegistry", RegistrationOrder::Insertion, true )
        };
    } // namespace

    bool MemorySnapshotSourceRegistration::registerSource( const MemorySnapshotSourceRegistration* pRegistration )
    {
        if ( pRegistration == nullptr || pRegistration->_pName == nullptr || pRegistration->_pFunction == nullptr )
            return false;
        return MemorySnapshotSourceInternal::getRegistrations().addLogged( pRegistration, "Memory snapshot source", pRegistration->_pName ) == RegistrationResult::Added;
    }

    void MemorySnapshotSourceRegistration::unregisterSource( const MemorySnapshotSourceRegistration* pRegistration )
    {
        (void)MemorySnapshotSourceInternal::getRegistrations().remove( pRegistration ); // 등록되어 있지 않은 항목을 빼는 것은 할 일이 없다
    }

    void MemorySnapshotSourceRegistration::fillAll( MemorySnapshotSources& inoutSources )
    {
        const RegistrationList<const MemorySnapshotSourceRegistration>& registrations = MemorySnapshotSourceInternal::getRegistrations();
        for ( uint32 index = 0; index < registrations.getCount(); ++index )
        {
            registrations.getAt( index )->_pFunction( inoutSources );
        }
    }
} // namespace sw
