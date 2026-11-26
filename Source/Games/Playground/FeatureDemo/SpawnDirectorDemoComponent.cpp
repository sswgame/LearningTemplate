#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"
#include "Core/Time/TimeSeconds.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Serializer/StateIO.h"

#include "GameFramework/Runtime/Character/AI/SpawnDirector.h"
#include "GameFramework/Runtime/Foundation/GameMode/BoundAssetRef.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"

namespace sw
{
    /**
     * @class SpawnDirectorDemoComponent
     * @brief 감독을 프레임마다 돌려 낸 것을 세우고, 파괴된 것을 감독에 알려 동시 상한의 자리를 돌려줍니다.
     */
    REFLECT( Category = "FeatureDemo", DisplayName = "Spawn Director Demo", Tooltip = "Spawns a prefab for every entry the budgeted spawn director releases" )
    class SpawnDirectorDemoComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        SpawnDirectorDemoComponent();
        virtual ~SpawnDirectorDemoComponent() override;

    protected:
        void               serializeGameState( StateIO& io ) override;
        [[nodiscard]] bool startGame() override;
        void               updateGame( GameSeconds frameTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        void               onViewsDespawned() override;
        bool               hasPendingSpawn() const override;

    private:
        /** @brief 세운 오브젝트 하나와 감독의 스폰 번호입니다. */
        struct SpawnedView
        {
            GameObjectHandle _object{};
            uint32           _spawnID{ 0 };
        };

        /** @brief 상태 칸(살아 있는 수 · 예산 · 다음 항목)을 감독에서 옮깁니다. */
        void refreshState();

    private:
        PROPERTY( Category = "Data", DisplayName = "Spawn Table", Tooltip = "Budget and entries of the spawn director" )
        AssetRef<SpawnTable> _spawnTable;
        PROPERTY( Category = "Data", DisplayName = "Prefab", Tooltip = "Prefab spawned for every entry" )
        AssetRef<PrefabAsset> _spawnPrefab;
        PROPERTY( Category = "Data", DisplayName = "Seed", Min = 1 )
        uint32 _seed;

        PROPERTY( RuntimeState, Min = 0 )
        int32 _spawnedTotal;
        PROPERTY( RuntimeState, Min = 0 )
        int32 _despawnedTotal;
        PROPERTY( RuntimeState, Min = 0 )
        int32 _aliveCount;
        PROPERTY( RuntimeState, Min = 0 )
        float32 _budget;

        BoundAssetRef<SpawnTable>    _pTable; ///< 감독이 빌려 쓴다
        SW_CODEC_STATE SpawnDirector _director;
        SW_TRANSIENT vector<SpawnedView> _listView;
    };

    namespace
    {
        struct SpawnDirectorDemoComponentInternal
        {
            static constexpr FormatDesc kStateFormat{ "FDSP" };
        };
    } // namespace

    SpawnDirectorDemoComponent::SpawnDirectorDemoComponent()
        : _spawnTable{}
        , _spawnPrefab{}
        , _seed{ 1 }
        , _spawnedTotal{ 0 }
        , _despawnedTotal{ 0 }
        , _aliveCount{ 0 }
        , _budget{ 0.0f }
        , _pTable{}
        , _director{}
        , _listView{}
    {
    }

    SpawnDirectorDemoComponent::~SpawnDirectorDemoComponent() = default;

    bool SpawnDirectorDemoComponent::startGame()
    {
        if ( acquireRequired( _spawnTable, _pTable ) == false )
            return false;
        _director.initialize( _pTable.get(), _seed );
        refreshState();
        return true;
    }

    void SpawnDirectorDemoComponent::refreshState()
    {
        _aliveCount = _director.getTotalAliveCount();
        _budget     = _director.getBudget();
    }

    void SpawnDirectorDemoComponent::updateGame( GameSeconds frameTime )
    {
        const float32 deltaTime = frameTime.count();
        // 사라진 것(파괴 · 죽음)을 먼저 알린다 — 그 자리가 이번 갱신의 동시 상한에 든다.
        for ( SpawnedView& view : _listView )
        {
            if ( findGameObject( view._object ) != nullptr )
                continue;
            if ( _director.notifyDespawned( view._spawnID ) )
                ++_despawnedTotal;
            view._spawnID = 0;
        }
        VectorUtil::removeIf( _listView, []( const SpawnedView& view )
        { return view._spawnID == 0; } );

        _spawnedTotal += _director.update( deltaTime );
        _director.discardEvents(); // 세우는 일은 살아 있는 목록에서 한다(불러온 판과 같은 길)
        refreshState();
        if ( hasPendingSpawn() )
            scheduleFlush();
    }

    bool SpawnDirectorDemoComponent::hasPendingSpawn() const
    {
        return _listView.size() < static_cast<size_t>( _director.getTotalAliveCount() );
    }

    void SpawnDirectorDemoComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        (void)bRespawnViews; // 처음이든 걷은 뒤든 살아 있는 것 가운데 세우지 않은 것을 세운다
        vector<uint32> listAlive;
        _director.collectAliveSpawnIDs( listAlive );
        for ( const uint32 spawnID : listAlive )
        {
            if ( VectorUtil::containsBy<&SpawnedView::_spawnID>( _listView, spawnID ) )
                continue;
            fixed_string<constant::buffer::kMax64> name;
            formatstring( name.data(), name.capacity(), "Spawn.%#", spawnID );
            const GameObject* pObject = spawnPrefab( manager, _spawnPrefab.path(), name.c_str() );
            if ( pObject != nullptr )
                _listView.push_back( SpawnedView{ pObject->getHandle(), spawnID } );
        }
    }

    void SpawnDirectorDemoComponent::onViewsDespawned()
    {
        _listView.clear(); // 걷은 것은 죽은 것이 아니다 — 다음 플러시가 다시 세운다
    }

    void SpawnDirectorDemoComponent::serializeGameState( StateIO& io )
    {
        io.header( SpawnDirectorDemoComponentInternal::kStateFormat );
        (void)io.valueOpened( _director, [this]( SpawnDirector& director )
        {
            director.initialize( _pTable.get(), _seed );
            return true;
        } ); // 실패는 아카이브에 남는다
    }
} // namespace sw

#include "SpawnDirectorDemoComponent.cpp.gen.inl"
