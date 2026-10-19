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
#include "Engine/Vfx/VfxParticle.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class VfxComponent
     * @brief 효과의 방출기마다 시뮬레이션 하나(`VfxCPUSimulation`)를 돌리고, 틱마다 살아 있는 입자(`GPUParticle`)를 방출기별 새 배열로 냅니다.
     * @details 렌더러는 씬의 컴포넌트 목록(`ComponentRegistry`)에서 이 컴포넌트를 찾아 방출기마다 투명 그리기 하나를 만든다(`GPUSceneBuilder::populateParticleBatches`).
     *          낸 배열은 고쳐 쓰지 않는다 — 렌더 스레드가 몇 프레임 뒤까지 읽으므로 다음 틱은 새 배열을 만든다.
     *
     *          효과는 `_effectPath` 를 읽을 때(상태 로드 · 프로퍼티 변경 · `setEffectPath`) 다시 만든다. 읽지 못하면 방출기가 없고 아무것도 그리지 않는다(효과 데이터가 오류를 남긴다).
     *          방출기 씨앗은 `_seed` 와 방출기 번호로 정해진다(같은 씨앗이면 같은 입자 — `VfxCPUSimulation`). 플레이를 시작하면 `_bPlayOnBegin` 일 때 재생하고,
     *          플레이 밖(에디터 미리보기 창)에서는 부르는 쪽이 `play` 를 부른다.
     *
     *          shortcut: 컴포넌트마다 효과 파일을 따로 읽는다(효과 캐시가 없다) — 같은 효과를 자주 스폰하게 되면 `IAssetCache` 로 공유한다.
     */
    REFLECT( Category = "Effects", DisplayName = "Vfx", Tooltip = "Plays a particle effect (.vfx.xml); each emitter draws as one transparent billboard batch" )
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
        /** @brief 방출기 @p emitterIndex 가 마지막 틱에 낸 입자입니다. 입자가 없으면 nullptr 입니다. 번호는 `getEmitterCount` 보다 작아야 합니다. */
        const shared_ptr<const vector<GPUParticle>>& getEmitterParticles( uint32 emitterIndex ) const;
        /** @brief 모든 방출기의 살아 있는 입자 수입니다. */
        uint32 getAliveParticleCount() const;

    private:
        /** @brief `_effectPath` 를 읽어 방출기마다 시뮬레이션을 만듭니다. 재생은 멈춥니다. 경로가 비었거나 읽지 못하면 방출기가 없습니다. */
        void loadEffect();
        /** @brief 방출기마다 살아 있는 입자를 새 배열로 냅니다(입자가 없으면 nullptr). */
        void publishParticles();

        PROPERTY( Category = "Vfx", DisplayName = "Effect", AssetPath, FileFilter = "*.vfx.xml", Tooltip = "Particle effect (.vfx.xml)" )
        string                                        _effectPath;
        vector<VfxCPUSimulation>                      _listSimulation;
        vector<shared_ptr<const vector<GPUParticle>>> _listPublished;
        PROPERTY( Category = "Vfx", DisplayName = "Seed", Tooltip = "Random seed of the emitters; the same seed replays the same particles" )
        uint32 _seed;
        PROPERTY( Category = "Playback", DisplayName = "Play On Begin", Tooltip = "Start playing when gameplay begins" )
        bool _bPlayOnBegin;
        bool _bPlaying;
    };
} // namespace sw
