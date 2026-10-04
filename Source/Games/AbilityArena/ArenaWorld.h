/**
 * @file ArenaWorld.h
 * @brief AbilityArena 의 게임 규칙 — 유닛 · 웨이브 · 적 AI · 투사체 · 입력 · 카메라입니다.
 *
 * @details 게임 규칙은 이 한 클래스에 둡니다. 어빌리티 · 이펙트 · 어트리뷰트는 전부 프레임워크(`AbilitySystemComponent`)와 데이터
 *          (`Resource/game/abilityarena/data/abilities.xml`)가 맡고, 여기는 "누가 어디로 움직이고 언제 어떤 입력을 누르는가" 만 압니다.
 *          어빌리티(`ArenaAbilities.h`)가 대상 찾기 · 투사체 발사를 부탁하는 창구라 게임 서비스로 걸립니다(`game::getService<ArenaWorld>()`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Ability/GameplayEffect.h"

namespace sw
{
    class AbilityCatalog;
    class AbilitySystemComponent;
    class GameObject;
    class GameObjectManager;
    class InputManager;
    class MaterialInstance;
    class Mesh;
    class MeshComponent;
    class Scene;

    /** @brief 아레나 유닛의 종류입니다 — 어빌리티 세트 · 모양 · AI 를 고릅니다. */
    enum class ArenaUnitKind : uint8
    {
        Player = 0, ///< 입력(또는 자동 전투)으로 움직인다 — 세트 "Player"
        Grunt,      ///< 다가와 때린다, 맞으면 가시로 되갚는다 — 세트 "Grunt"
        Caster      ///< 거리를 두고 화염탄을 쏜다 — 세트 "Caster"
    };

    /**
     * @class ArenaWorld
     * @brief 아레나 한 판의 상태와 규칙입니다.
     * @details 오브젝트는 핸들로만 듭니다(프레임을 넘겨 포인터를 들지 않는다). 활성 씬이 바뀌면(에디터가 다른 씬을 열면) 다음 갱신에서 새 씬에
     *          다시 세웁니다. 게임 갱신(`update`)은 씬 틱 밖에서 돌므로 여기서는 오브젝트를 바로 만들고 지웁니다 — 틱 안에서 불릴 수 있는
     *          `launchProjectile` 만 틱 직후로 미룹니다.
     */
    class ArenaWorld
    {
    public:
        /** @brief 근접 · 화염구 · 회복 · 대시의 입력 번호입니다(abilities.xml 의 세트와 같다). */
        static constexpr int32 kInputMelee    = 1;
        static constexpr int32 kInputFireball = 2;
        static constexpr int32 kInputHeal     = 3;
        static constexpr int32 kInputDash     = 4;

        ArenaWorld();
        ~ArenaWorld();

        ArenaWorld( const ArenaWorld& )            = delete;
        ArenaWorld& operator=( const ArenaWorld& ) = delete;

        /** @brief 활성 씬(없으면 새 빈 씬)에 바닥 · 빛 · 플레이어 · 첫 웨이브를 세웁니다. 이미 세웠으면 그대로 true 입니다. */
        [[nodiscard]] bool spawn( const AbilityCatalog* pCatalog );
        /** @brief 세운 오브젝트를 모두 걷습니다(핫 리로드 스냅샷 전 · 종료). */
        void despawn();
        /** @brief 한 프레임 — 입력 · AI · 투사체 · 쓰러짐 · 웨이브 · 카메라. */
        void update( float32 deltaTime );

        /** @brief 세워져 있으면 true 입니다. */
        bool isSpawned() const { return _bSpawned == SW_TRUE; }
        /** @brief 지금 웨이브 번호(1 부터)입니다. */
        uint32 getWave() const { return _wave; }
        /** @brief 쓰러뜨린 적 수입니다. */
        uint32 getKillCount() const { return _killCount; }

        // --------------------------------------------------------------------------
        // 어빌리티가 부탁하는 것
        // --------------------------------------------------------------------------
        /** @brief @p from 과 적대이고 살아 있는 가장 가까운 유닛의 어빌리티 시스템입니다. @p maxRange 밖이면 nullptr 입니다. */
        AbilitySystemComponent* findNearestHostile( const AbilitySystemComponent& from, float32 maxRange ) const;
        /** @brief 유닛이 바라보는 방향(XZ 평면의 단위 벡터)입니다. 모르는 유닛이면 +Z 입니다. */
        float3 getFacing( const AbilitySystemComponent& unit ) const;
        /**
         * @brief @p from 이 바라보는 쪽으로 투사체를 쏩니다. 처음 닿은 적대 유닛에 @p spec(과 있으면 @p extraSpec)을 겁니다.
         * @details 스펙은 쏜 순간 만든 것이라 쏜 쪽이 그 사이 쓰러져도 공격력 스냅샷이 남습니다. 틱 중이면 만들기를 틱 직후로 미룹니다.
         */
        void launchProjectile( const AbilitySystemComponent& from, const GameplayEffectSpec& spec, const GameplayEffectSpec& extraSpec, float32 speed,
                               float32 range );

    private:
        /** @brief 유닛 하나 — 오브젝트(메시 + 어빌리티 시스템 + HP 바)의 핸들과 AI 상태입니다. */
        struct ArenaUnit
        {
            GameObjectHandle _object{};
            float3           _facing{ 0.0f, 0.0f, 1.0f };
            float32          _deathTimer{ -1.0f }; ///< 0 이상이면 쓰러진 뒤 걷기까지 남은 시간
            ArenaUnitKind    _kind{ ArenaUnitKind::Grunt };
        };

        /** @brief 날아가는 투사체 하나입니다. */
        struct ArenaProjectile
        {
            GameObjectHandle   _object{};
            GameplayEffectSpec _spec{};
            GameplayEffectSpec _extraSpec{};
            float3             _position{};
            float3             _velocity{};
            float32            _remainingRange{ 0.0f };
            uint8              _bFromPlayer{ SW_FALSE };
        };

        Scene*                  findActiveScene() const;
        GameObjectManager*      findObjectManager() const;
        AbilitySystemComponent* findAbilitySystem( GameObjectHandle object ) const;
        MeshComponent*          findMesh( GameObjectHandle object ) const;
        ArenaUnit*              findUnit( const AbilitySystemComponent& abilitySystem );
        const ArenaUnit*        findUnit( const AbilitySystemComponent& abilitySystem ) const;
        /** @brief 두 어빌리티 시스템이 서로 다른 팀이면 true 입니다(팀은 세트가 주는 `Team.*` 태그). */
        bool isHostile( const AbilitySystemComponent& lhs, const AbilitySystemComponent& rhs ) const;

        /** @brief 메시 하나짜리 오브젝트를 만듭니다. */
        GameObject* createMeshObject( GameObjectManager& manager, const utf8* pName, const shared_ptr<Mesh>& mesh,
                                      const shared_ptr<MaterialInstance>& material, const float3& position, const float3& scale );
        /** @brief 메시 에셋(`game/abilityarena/models/<pModel>.mesh`) 하나짜리 오브젝트를 키트 배율로 만듭니다. 읽지 못하면 nullptr 입니다. */
        GameObject* createModelObject( GameObjectManager& manager, const utf8* pName, const utf8* pModel, const shared_ptr<MaterialInstance>& material,
                                       const float3& position, float32 yaw );
        /** @brief 무대 소품(바닥 · 벽 · 장식) 모델 하나를 세우고 걷을 목록에 넣습니다. */
        void addStageModel( GameObjectManager& manager, const utf8* pModel, const float3& position, float32 yaw );
        /** @brief 유닛을 하나 세웁니다 — 메시 · 어빌리티 시스템(세트) · HP 바. */
        [[nodiscard]] bool spawnUnit( ArenaUnitKind kind, const float3& position, int32 level );
        /** @brief 다음 웨이브의 적을 원 위에 세웁니다. */
        void spawnWave();
        /** @brief 바닥 · 벽 · 장식 · 빛을 세웁니다. */
        void spawnStage( GameObjectManager& manager );
        /** @brief 메시 · 머티리얼 인스턴스를 (처음 한 번) 만듭니다. */
        bool ensureRenderAssets( Scene& scene );

        void updatePlayer( float32 deltaTime, const InputManager* pInput );
        void updateAutoPlayer( ArenaUnit& player, AbilitySystemComponent& abilitySystem, float32 deltaTime );
        void updateEnemies( float32 deltaTime );
        void updateProjectiles( float32 deltaTime );
        void updateDeaths( float32 deltaTime );
        /** @brief 살아 있는 유닛 모델을 바라보는 쪽(`_facing`)으로 돌립니다. */
        void updateUnitFacing();
        void updateCamera();
        void logStatus( float32 deltaTime );
        /** @brief 유닛을 @p direction 으로 이동 속도(`MoveSpeed`)만큼 움직이고 아레나 안에 가둡니다. 바라보는 방향도 바꿉니다. */
        void moveUnit( ArenaUnit& unit, const AbilitySystemComponent& abilitySystem, const float3& direction, float32 deltaTime );
        /** @brief 입력 번호 하나를 한 번 눌렀다 뗍니다(AI · 자동 전투). */
        static void tapInput( AbilitySystemComponent& abilitySystem, int32 inputId );

        vector<ArenaUnit>            _listUnit;
        vector<ArenaProjectile>      _listProjectile;
        vector<GameObjectHandle>     _listStageObject; ///< 바닥 · 벽 · 장식 · 빛
        shared_ptr<Mesh>             _pProjectileMesh;
        shared_ptr<MaterialInstance> _pPlayerMaterial;
        shared_ptr<MaterialInstance> _pGruntMaterial;
        shared_ptr<MaterialInstance> _pCasterMaterial;
        shared_ptr<MaterialInstance> _pProjectileMaterial;
        shared_ptr<MaterialInstance> _pStageMaterial; ///< 팔레트 텍스처만(색 흰색) — 무대 소품
        const AbilityCatalog*        _pCatalog;
        uint64                       _sceneGeneration;
        float32                      _playerRespawnTimer; ///< 0 이상이면 플레이어가 다시 서기까지 남은 시간
        float32                      _statusLogTimer;
        uint32                       _wave;
        uint32                       _killCount;
        uint8                        _bSpawned;
    };
} // namespace sw
