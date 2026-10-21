/**
 * @file VfxComponent.h
 * @brief 파티클 효과(`*.vfx.xml`) 하나를 씬에서 재생하는 컴포넌트입니다(유니티 ParticleSystem, 언리얼 UNiagaraComponent 자리).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Vfx/VfxCPUSimulation.h"
#include "Engine/Vfx/VfxEffectDef.h"
#include "Engine/Vfx/VfxGPUEmitter.h"
#include "Engine/Vfx/VfxParticle.h"

#include <variant>

namespace sw
{
    struct GPULight;

    class EnvironmentMaterial;
    class GameObjectManager;
    class MeshInstanceBatch;
    class PrimitiveRegistry;

    /**
     * @class VfxComponent
     * @brief 효과의 방출기마다 시뮬레이션 하나를 돌립니다. CPU 방출기(`VfxCPUSimulation`)는 틱마다 살아 있는 입자(`GPUParticle`)를 새 배열로 내고,
     *        GPU 방출기(`VfxGPUEmitter`)는 누적 단계 수의 요청(`VfxGPUEmitterRequest`)을 내며 입자는 렌더 스레드의 컴퓨트가 만든다.
     * @details 방출기마다 시뮬레이션 대상(`VfxSimulationTarget`)을 효과를 읽을 때 정한다(언리얼 Niagara 의 방출기별 Sim Target). 대상은 모듈이 허락한
     *          곳(`VfxEmitterDef::computeSupportedTargets` — 물리 충돌 · 라이트는 CPU, 깊이 충돌은 GPU)이고, 둘 다 되면 Shipping 에서 GPU,
     *          개발 빌드에서 `gv_vfxSimulation`(기본 GPU — Shipping 과 같다)이 고른다. 결정 기록 5-12 `D2-11`.
     *          GPU 메시 방출기는 인스턴스 배치를 `MeshInstanceWriter::GPU` 로 만들고 항목을 쓰지 않는다 — 컴퓨트가 인스턴스 칸에 쓴다(불투명 머티리얼만).
     *          렌더러는 씬의 컴포넌트 목록(`ComponentRegistry`)에서 이 컴포넌트를 찾아 빌보드 · 띠 방출기마다 투명 그리기 하나를 만든다(`GPUSceneBuilder::populateParticleBatches`).
     *          낸 배열은 고쳐 쓰지 않는다 — 렌더 스레드가 몇 프레임 뒤까지 읽으므로 다음 틱은 새 배열을 만든다.
     *
     *          메시 방출기(`VfxRenderMode::Mesh`)는 풀 크기만큼 항목을 가진 메시 인스턴스 배치(`MeshInstanceBatch`)를 프리미티브 레지스트리에 올리고
     *          틱마다 항목 행렬을 고친다 — 메시 경로(머티리얼 · 그림자 · 컬링 · 투명 정렬)를 그대로 탄다(언리얼 Niagara 메시 렌더러가 메시 드로우를 내는 것과 같다).
     *          shortcut: 죽은 자리는 크기 0 행렬로 둔다(`setEntryVisible` 을 매 틱 바꾸면 빌더가 그 프레임을 전체 수집으로 돈다). 풀이 크고 대부분 비어 있는
     *          방출기가 많아지면 항목 수를 살아 있는 수에 맞추는 배치 재구성을 따로 둔다.
     *          충돌은 물리 씬(3D)을 방출기 로컬로 감싼 질의(`IVfxCollisionScene`)로 묻는다. 파티클 라이트는 `appendGPULights` 가 씬 라이트 목록에 붙인다.
     *
     *          효과는 `_effectPath` 를 읽을 때(상태 로드 · 프로퍼티 변경 · `setEffectPath`) 다시 만든다. 읽지 못하면 방출기가 없고 아무것도 그리지 않는다(효과 데이터가 오류를 남긴다).
     *          방출기 씨앗은 `_seed` 와 방출기 번호로 정해진다(같은 씨앗이면 같은 입자 — `VfxCPUSimulation`). 플레이를 시작하면 `_bPlayOnBegin` 일 때 재생하고,
     *          플레이 밖(에디터 미리보기 창)에서는 부르는 쪽이 `play` 를 부른다.
     *
     *          shortcut: 컴포넌트마다 효과 파일을 따로 읽는다(효과 캐시가 없다) — 같은 효과를 자주 스폰하게 되면 `IAssetCache` 로 공유한다.
     */
    REFLECT( Category = "Effects", DisplayName = "Vfx", Tooltip = "Plays a particle effect (.vfx.xml); billboard and ribbon emitters draw as one transparent batch each, mesh emitters as mesh instances" )
    class SW_API VfxComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        VfxComponent();
        virtual ~VfxComponent() override;

        /** @brief 씬의 효과 목록(`ComponentRegistry`)에 등록합니다. 렌더러가 씬 전체를 훑지 않고 이 목록을 봅니다. */
        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief `_bPlayOnBegin` 이면 재생을 시작합니다. */
        void onBeginPlay() override;
        /** @brief 재생 중이면 시뮬레이션을 프레임 시간만큼 돌리고 입자를 냅니다. 방출이 끝나고 입자가 모두 사라지면 재생을 멈춥니다. */
        void onTick( float32 deltaTime ) override;
        /** @brief 읽어 들인 `_effectPath` 로 효과를 다시 만듭니다. */
        void onPostLoad() override;
        /** @brief `_effectPath` · `_seed` 가 바뀌면 효과를 다시 만듭니다. */
        void onPropertyChanged( hashed_string propertyName ) override;

        /** @brief 처음부터 다시 재생합니다(입자를 지우고 방출기 시간을 0 으로). 효과가 없으면 재생하지 않습니다. */
        FUNCTION( Category = "Playback", DisplayName = "Play", CallInEditor )
        void play();
        /** @brief 재생을 멈추고 입자를 지웁니다. */
        FUNCTION( Category = "Playback", DisplayName = "Stop", CallInEditor )
        void stop();

        /** @brief 효과 파일을 바꾸고 바로 읽습니다. 재생은 멈춥니다. */
        void setEffectPath( string_view path );
        /** @brief 효과 파일(리소스 id)입니다. */
        const string& getEffectPath() const { return _effectPath; }
        /** @brief 씨앗을 바꿉니다. 효과를 다시 만들고 재생은 멈춥니다. */
        void setSeed( uint32 seed );
        /** @brief 방출기 씨앗의 바탕입니다. */
        uint32 getSeed() const { return _seed; }
        /** @brief 재생 중이면 true 입니다. */
        bool isPlaying() const { return _bPlaying; }

        /** @brief 효과의 방출기 수입니다. 효과를 읽지 못했으면 0 입니다. */
        uint32 getEmitterCount() const { return static_cast<uint32>( _listSimulation.size() ); }
        /** @brief 방출기 @p emitterIndex 의 데이터입니다. 번호는 `getEmitterCount` 보다 작아야 합니다. */
        const VfxEmitterDef& getEmitterDef( uint32 emitterIndex ) const;
        /** @brief 방출기 @p emitterIndex 를 시뮬레이션하는 곳입니다(효과를 읽을 때 정했다). 번호는 `getEmitterCount` 보다 작아야 합니다. */
        VfxSimulationTarget getEmitterSimulationTarget( uint32 emitterIndex ) const;
        /**
         * @brief CPU 방출기 @p emitterIndex 가 마지막 틱에 낸 입자입니다. 입자가 없거나 GPU 방출기면 nullptr 입니다. 번호는 `getEmitterCount` 보다 작아야 합니다.
         */
        const shared_ptr<const vector<GPUParticle>>& getEmitterParticles( uint32 emitterIndex ) const;
        /** @brief GPU 방출기 @p emitterIndex 의 이번 요청입니다. 재생 중이 아니거나 CPU 방출기면 nullptr 입니다. 번호는 `getEmitterCount` 보다 작아야 합니다. */
        const VfxGPUEmitterRequest* findEmitterGPURequest( uint32 emitterIndex ) const;
        /** @brief CPU 방출기들의 살아 있는 입자 수입니다. GPU 방출기의 입자는 GPU 버퍼에만 있어 세지 않습니다. */
        uint32 getCPUAliveParticleCount() const;
        /** @brief 모든 방출기가 이번 재생에서 번호를 쓴 입자 수(풀이 차서 버린 입자 포함)입니다. 생성 일정에서 오므로 CPU · GPU 방출기가 같다. */
        uint64 getSpawnedParticleCount() const;
        /** @brief 메시 방출기 @p emitterIndex 의 인스턴스 배치입니다. 메시 방출기가 아니거나 등록 전 · 메시를 잡지 못했으면 nullptr 입니다. */
        const MeshInstanceBatch* findEmitterMeshBatch( uint32 emitterIndex ) const;
        /** @brief 메시 방출기 @p emitterIndex 의 메시 반지름(원점에서 가장 먼 바운드 모서리, 크기 1 기준)입니다. 배치가 없으면 0 입니다. */
        float32 getEmitterMeshRadius( uint32 emitterIndex ) const;

        /**
         * @brief 파티클 라이트(`VfxLightDef`)를 @p outList 끝에 점광으로 붙입니다(마지막 틱에 낸 입자로). 꺼져 있거나 입자가 없으면 붙이지 않습니다.
         * @details 씬 라이트 수집(`collectSceneLights`)이 라이트 컴포넌트 뒤에 부른다. 방출기마다 낸 순서의 앞 `_maxLights` 개다.
         */
        void appendGPULights( vector<GPULight>& outList ) const;

    private:
        /** @brief 방출기 하나의 시뮬레이션입니다. 대상(`VfxSimulationTarget`)마다 하나뿐이다 — 둘을 함께 들 수 없다. */
        using EmitterSimulation = std::variant<VfxCPUSimulation, VfxGPUEmitter>;

        /** @brief 메시 방출기 하나의 인스턴스 배치와 그 머티리얼입니다. 메시 방출기가 아니면 둘 다 비어 있습니다. */
        struct MeshEmitter
        {
            unique_ptr<MeshInstanceBatch>   _batch;
            unique_ptr<EnvironmentMaterial> _material;
            float32                         _meshRadius{ 0.0f }; ///< 메시 원점에서 가장 먼 꼭짓점까지(크기 1 기준) — 항목 바운드에 곱한다
        };

        /** @brief `_effectPath` 를 읽어 방출기마다 시뮬레이션 대상을 정하고 시뮬레이션을 만듭니다. 재생은 멈춥니다. 경로가 비었거나 읽지 못하면 방출기가 없습니다. */
        void loadEffect();
        /**
         * @brief CPU 방출기는 살아 있는 입자를 새 배열로(입자가 없으면 nullptr), GPU 방출기는 재생 중일 때 요청을 냅니다. 메시 방출기는 인스턴스 배치의 항목도 고칩니다.
         */
        void publishParticles();
        /** @brief 방출기마다 시뮬레이션을 처음으로 되돌립니다(GPU 방출기는 새 재생 번호를 받는다). */
        void restartSimulations();
        /** @brief 메시 방출기마다 인스턴스 배치를 만들어 레지스트리에 올립니다. 등록 전(레지스트리 없음)이면 아무것도 하지 않습니다. */
        void rebuildMeshEmitters();
        /** @brief 메시 방출기의 배치를 레지스트리에서 내리고 머티리얼을 놓습니다. */
        void releaseMeshEmitters();
        /** @brief 메시 방출기 @p emitterIndex 의 항목을 @p listParticle(비면 모두 크기 0)로 고칩니다. */
        void writeMeshEntries( uint32 emitterIndex, const vector<GPUParticle>* pListParticle );

        PROPERTY( Category = "Vfx", DisplayName = "Effect", AssetPath, FileFilter = "*.vfx.xml", Tooltip = "Particle effect (.vfx.xml)" )
        string                                        _effectPath;
        vector<EmitterSimulation>                     _listSimulation;
        vector<shared_ptr<const vector<GPUParticle>>> _listPublished;      ///< CPU 방출기마다 마지막 틱의 입자(GPU 방출기는 늘 nullptr)
        vector<VfxGPUEmitterRequest>                  _listGPURequest;     ///< GPU 방출기마다 이번 요청(재생 중이 아니거나 CPU 방출기면 `_pDef` 가 nullptr)
        vector<MeshEmitter>                           _listMeshEmitter;    ///< 방출기마다 하나(`_listSimulation` 과 길이가 같다)
        PrimitiveRegistry*                            _pPrimitiveRegistry; ///< 등록된 씬의 프리미티브 레지스트리(빌림). 등록 전이면 nullptr
        PROPERTY( Category = "Vfx", DisplayName = "Seed", Tooltip = "Random seed of the emitters; the same seed replays the same particles" )
        uint32 _seed;
        PROPERTY( Category = "Playback", DisplayName = "Play On Begin", Tooltip = "Start playing when gameplay begins" )
        bool _bPlayOnBegin;
        bool _bPlaying;
    };
} // namespace sw
