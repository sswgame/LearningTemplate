/**
 * @file TextureStreamingSystem.cpp
 * @brief 텍스처 밉 요청 우선순위입니다 — 화면에서 큰 메시가 쓰는 텍스처의 위쪽 밉을 먼저 올리게 `TextureCache` 에 화면 크기를 알립니다. 씬 서비스(`ISceneService`)라 매니저마다 하나 있고,
 *        `SceneFrameStep::FlushEnd` 단계(그리기 전)에서 돕니다.
 * @details 텍스처 밉 스트리밍(`TextureCache::updateStreaming`)은 아래쪽 밉으로 먼저 보이고 위쪽 밉을 프레임에 둘씩 바꿔 끼운다. 순서가 읽힌 차례뿐이면 멀리 있는 작은 물체의 텍스처가 가까운 큰 물체보다
 *          먼저 선명해질 수 있다(언리얼 텍스처 스트리머는 보이는 것의 wanted mip 부터 올린다). 여기서는 메시 컴포넌트마다 `MeshLODSystem` 이 쓰는 같은 화면 크기(경계 구의 화면 높이 비율, 주 시점 ·
 *          추가 뷰 · 대기 뷰 중 가장 큰 것)를 구해 그 머티리얼의 텍스처마다 가장 큰 값을 `TextureCache::recordStreamingDemand` 로 알린다. 대기 뷰(컷 프리롤)가 있으면 곧 볼 것도 같이 앞선다.
 *          화면 크기 앞에 메시 자리의 스트리밍 우선순위(`SceneStreamingSubsystem::computePriorityAt` — 셀 로드 차례와 같은 값)를 둔다: 급한 스트리밍 소스가 덮는 자리의 텍스처를
 *          먼저 올리고, 소스 반경 밖(HLOD 대리가 그리는 먼 영역)은 `Lowest` 로 뒤에 선다. 스트리밍하지 않는 씬은 모두 `Normal` 이라 화면 크기 차례 그대로다.
 *          **싸게 만든다**: 위쪽 밉을 기다리는 텍스처가 없으면 메시를 훑지 않는다(`TextureCache::hasPendingStreamingUpgrades`) — 스트리밍이 몰리는 때(씬 로드 직후 · 새 오브젝트)만 돈다.
 *          UV 밀도(텍셀 대 화면 픽셀)는 모르므로 화면 크기만 쓴다 — 같은 머티리얼의 텍스처는 같은 우선도를 받는다.
 */
#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Object/GameObject/Service/ISceneService.h"
#include "Engine/Object/GameObject/Service/SceneServiceRegistry.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"
#include "Engine/Visual/Component/MeshComponent.h"
#include "Engine/Visual/MeshLODSystem.h"

namespace sw
{
    /** @class TextureStreamingSystem @brief 씬 하나의 텍스처 밉 요청 우선순위 알림입니다. 파일 머리말 참고. */
    class TextureStreamingSystem final : public ISceneService
    {
    public:
        /** @brief 씬 서비스 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "TextureStreamingSystem";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        explicit TextureStreamingSystem( GameObjectManager& manager );
        ~TextureStreamingSystem() override                                 = default;
        TextureStreamingSystem( const TextureStreamingSystem& )            = delete;
        TextureStreamingSystem& operator=( const TextureStreamingSystem& ) = delete;

        /** @brief 프레임 단계 `FlushEnd` 에서 위쪽 밉을 기다리는 텍스처가 있으면 메시의 화면 크기를 `TextureCache` 에 알립니다. */
        void update( float32 deltaTime ) override;
        /** @brief 씬을 비울 때 놓을 것이 없습니다. */
        void onSceneCleared() override {}

    private:
        GameObjectManager&                                     _manager;
        unordered_map<const Material*, TextureStreamingDemand> _mapMaterialDemand; ///< 머티리얼마다 이번 프레임의 합친 요구(재사용). 같은 머티리얼을 쓰는 메시가 경로 글을 되풀이하지 않는다
        vector<TextureStreamingDemand>                         _listMeshDemand;    ///< 프리미티브 목록(`PrimitiveRegistry::getAll`)과 같은 순서의 요구(재사용). 워커는 자기 칸에만 쓴다
        vector<string>                                         _listPathScratch;   ///< 머티리얼 하나의 텍스처 경로(재사용)
    };

    namespace
    {
        /** @brief 감시 값 `Texture.StreamingQueueCount` 입니다. 위쪽 밉을 기다리거나 읽는 중이거나 업로드 중인 텍스처 요청 수(텍스처 로드 큐 — `TextureCache::captureStreamingQueue`)입니다. */
        [[nodiscard]] bool readTextureStreamingQueueCount( const GameObjectManager* pManager, float64& outValue )
        {
            AssetManager* pAssets = pManager != nullptr ? pManager->findAssetManager() : nullptr;
            if ( pAssets == nullptr )
                return false;
            const TextureStreamingQueueSnapshot snapshot = pAssets->getAssetCache<TextureCache>().captureStreamingQueue();
            outValue                                     = static_cast<float64>( snapshot._listWaiting.size() + snapshot._readingCount + snapshot._uploadingCount );
            return true;
        }
    } // namespace

    SW_AUTOMATION_WATCH( textureStreamingQueueCount, "Texture.StreamingQueueCount",
                         "Texture mip requests waiting, reading or uploading (the texture load queue, ordered by streaming priority then screen size; world.streamqueue lists them)",
                         &readTextureStreamingQueueCount );

    SW_SCENE_SERVICE( TextureStreamingSystem, FlushEnd, makeSceneFrameAccess( SceneDataKindSet{}, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Structure, SceneDataKind::Transform } ) )

    TextureStreamingSystem::TextureStreamingSystem( GameObjectManager& manager )
        : _manager{ manager }
        , _mapMaterialDemand{}
        , _listMeshDemand{}
        , _listPathScratch{}
    {
    }

    void TextureStreamingSystem::update( float32 deltaTime )
    {
        (void)deltaTime;
        AssetManager* pAssets = _manager.findAssetManager();
        if ( pAssets == nullptr )
            return;
        // 기다리는 올리기가 없어도 알린다 — 내리기(R6-6)가 오래 안 보인 텍스처와 화면에서 작은 텍스처를 이 알림으로 가린다.
        TextureCache&                  textures  = pAssets->getAssetCache<TextureCache>();
        const MeshLODSystem&           lodSystem = _manager.getSubsystem<MeshLODSystem>();
        const SceneStreamingSubsystem& streaming = _manager.getSubsystem<SceneStreamingSubsystem>(); // 원점은 이 단계 동안 바뀌지 않는다(갱신은 `DestroyPost`)
        if ( lodSystem.areViewsSet() == false )
            return; // 렌더가 뷰를 넣기 전에는 화면 크기를 모른다
        SW_PROFILE_SCOPE( "GT.TextureStreaming.recordScreenSize" );
        // 화면 크기 재기는 메시마다 독립이라 워커로 나눈다 — 본문은 뷰(`MeshLODSystem`, 이 단계 동안 바뀌지 않는다)와 메시의 월드 캐시를 읽고 자기 칸 `_listMeshDemand[index]` 에만 쓴다.
        // 머티리얼별 최댓값 모으기는 이 스레드가 목록 순서로 한다(맵은 공유 쓰기다). 최댓값 규칙이라 결과는 나누는 방법과 무관하다.
        // 메시 LOD(`MeshLODSystem::update`)의 결과는 읽지 않는다 — 둘 다 뷰만 입력이라 사이에 기다릴 의존이 없다(같은 단계에서 차례로 돈다).
        // 월드 캐시는 읽기만 해야 한다 — 남은 더티가 있으면 먼저 펼친다(`MeshLODSystem::update` 와 같은 규칙).
        if ( _manager.hasDirtySceneTransforms() )
            _manager.flushSceneTransforms();
        const vector<PrimitiveComponent*>& listPrimitive = _manager.getPrimitiveRegistry().getAll(); // 메시 계열만 든다(`PrimitiveComponent.h` 의 불변식)
        const uint32                       count         = static_cast<uint32>( listPrimitive.size() );
        _listMeshDemand.resize( count );
        engine::runParallelIntoByCost<TextureStreamingDemand>( _listMeshDemand, [&lodSystem, &streaming, &listPrimitive]( uint32 index, TextureStreamingDemand& outDemand )
        {
            // 화면 크기 × UV 밀도 / 반지름 — 화면에서 UV 한 단위가 덮는 픽셀의 대리값이다(배율은 밀도 · 반지름에 같이 들어 지워진다). 같은 화면 크기라도
            // UV 를 촘촘히 펼친 메시가 위쪽 밉을 먼저 받는다(언리얼 텍스처 스트리밍의 UV density). UV 가 없는 메시는 0 이라 알리지 않는다.
            // 빈 칸(지운 프리미티브 — 레지스트리는 칸을 옮기지 않고 비워 둔다)은 0 이라 알리지 않는다.
            if ( listPrimitive[index] == nullptr )
            {
                outDemand = TextureStreamingDemand{};
                return;
            }
            const MeshComponent& mesh    = static_cast<const MeshComponent&>( *listPrimitive[index] );
            const Mesh* const    pMesh   = mesh.getMesh().get();
            const float32        density = pMesh != nullptr ? pMesh->getUVDensity() / MathUtil::max( pMesh->getBoundingRadius(), MathUtil::kEpsilon ) : 0.0f;
            outDemand._screenSize        = lodSystem.computeScreenSize( mesh ) * density;
            outDemand._priority          = streaming.computePriorityAt( mesh.getWorldPosition() ); // 셀 로드 차례와 같은 값(그 자리 셀을 덮는 원점 중 가장 급한 것)
        } );
        // 메시 패스는 머티리얼별 최댓값만 구한다. 글 만들기 · 경로 정규화 · 잠금은 머티리얼마다 한 번이다.
        _mapMaterialDemand.clear();
        for ( uint32 index = 0; index < count; ++index )
        {
            if ( _listMeshDemand[index]._screenSize <= 0.0f ) // 빈 칸도 여기서 빠진다
                continue;
            const Material* pMaterial = static_cast<MeshComponent&>( *listPrimitive[index] ).getMaterial();
            if ( pMaterial == nullptr )
                continue;
            const auto [it, bInserted] = _mapMaterialDemand.emplace( pMaterial, _listMeshDemand[index] );
            if ( bInserted == false )
                it->second.merge( _listMeshDemand[index] );
        }
        for ( const auto& [pMaterial, demand] : _mapMaterialDemand )
        {
            _listPathScratch.clear();
            pMaterial->collectTexturePaths( _listPathScratch );
            textures.recordStreamingDemand( _listPathScratch, demand );
        }
    }
} // namespace sw
