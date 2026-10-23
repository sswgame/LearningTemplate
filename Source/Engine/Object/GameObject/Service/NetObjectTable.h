/**
 * @file NetObjectTable.h
 * @brief 서버와 클라이언트가 같은 오브젝트를 같은 번호로 부르는 번호표입니다(씬마다 하나, `manager.getSubsystem<NetObjectTable>()`).
 * @details 파괴 복제(`DestructionNetSession`)와 페어드 애니메이션 복제(`PairedAnimationReplication`)가 이 표 하나를 쓴다. 번호는 두 분기다.
 *
 *          - **씬 번호**: 씬 문서에서 온 오브젝트는 이름의 해시(`hashed_string::getHash`, 대소문자 무시)에서 맨 위 비트를 끈 값이다. 서버와 클라이언트가 같은 씬
 *            문서를 읽으므로 같은 번호다. 예전처럼 "이름순 몇 번째" 가 아니어서 한쪽에만 있는 오브젝트가 다른 번호를 밀지 않는다(언리얼의 정적 액터 경로 이름과 같다).
 *            씬 오브젝트는 `captureSceneObjects` 가 그때 있는 오브젝트로 잡는다 — 그 뒤에 생긴 오브젝트는 씬 번호를 받지 않는다. 이름(해시)이 같은 씬 오브젝트가
 *            둘 이상이면 그 번호는 아무도 갖지 않고 오류를 남긴다(다른 오브젝트에 조용히 묶지 않는다).
 *          - **생성 번호**: 플레이 중에 생긴 오브젝트(투사체, 스폰한 파괴물)는 서버가 `registerSpawned` 로 맨 위 비트가 켜진 새 번호를 받고, 게임의 스폰 복제가 그 번호를
 *            클라이언트에 알려 `bindSpawned` 로 로컬 오브젝트에 묶는다(언리얼의 동적 NetGUID). 표는 스폰을 복제하지 않는다 — 무엇을 어떻게 스폰하는지는 게임이 안다.
 *
 *          클라이언트-서버 · MMO 키트의 엔티티 번호(`ReplicationServer::setEntity` · `MMOReplicator`)는 서버가 매겨 스냅숏마다 싣는 것이라 순서로 세지 않는다(같은 결함이 아니다).
 *          그 엔티티를 클라이언트가 로컬 오브젝트로 세울 때 서버의 생성 번호를 엔티티 상태에 실어 `bindSpawned` 로 묶으면 파괴 · 페어드 복제가 같은 오브젝트를 부른다.
 *
 *          표를 쓰는 기능은 시작할 때 `captureSceneObjects` 를 부르고, 생성 번호가 늘었는지는 `getSpawnedRevision` 으로 본다. 씬을 비우면 모두 지운다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"

#include "Engine/Object/GameObject/Service/ISceneService.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;

    /**
     * @class NetObjectTable
     * @brief 씬 하나의 네트워크 오브젝트 번호표입니다. 규칙은 파일 머리말을 보세요.
     */
    class SW_API NetObjectTable final : public ISceneService
    {
    public:
        /** @brief 씬 서비스 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "NetObjectTable";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        /** @brief 생성 번호에 켜는 비트입니다. 씬 번호는 이 비트가 꺼져 있습니다. 0 은 번호 없음입니다. */
        static constexpr uint32 kSpawnedBit = 0x80000000u;

        explicit NetObjectTable( GameObjectManager& manager );
        ~NetObjectTable() override = default;

        NetObjectTable( const NetObjectTable& )            = delete;
        NetObjectTable& operator=( const NetObjectTable& ) = delete;

        /** @brief 프레임마다 할 일이 없습니다(번호는 부르는 쪽이 정할 때 바뀐다). */
        void update( float32 deltaTime ) override { (void)deltaTime; }
        /** @brief 씬 번호와 생성 번호를 모두 지웁니다. */
        void onSceneCleared() override;

        /**
         * @brief 지금 매니저에 있는 오브젝트 가운데 아직 표에 없는 것을 씬 오브젝트로 잡습니다. 여러 번 불러도 됩니다(이미 잡은 것 · 생성 오브젝트는 그대로).
         * @details 씬 문서의 오브젝트가 모두 선 뒤(플레이 시작)에 부른다. 이름이 겹친 번호는 이때 오류로 알린다.
         */
        void captureSceneObjects();
        /** @brief 이름의 씬 번호입니다(맨 위 비트를 끈 해시, 해시가 0 이면 1). 잡혀 있는지는 보지 않습니다. */
        static uint32 computeSceneID( const GameObject& object );

        /** @brief 오브젝트의 번호입니다. 생성 번호가 있으면 그 번호, 잡힌 씬 오브젝트면 씬 번호, 아니면(이름이 겹쳤거나 표에 없음) 0 입니다. */
        [[nodiscard]] uint32 findID( const GameObject& object ) const;
        /** @brief 번호의 오브젝트입니다. 없거나 지워졌거나 이름이 겹친 씬 번호면 nullptr 입니다. */
        [[nodiscard]] GameObject* findObject( uint32 netID ) const;

        /** @brief 서버 — 플레이 중에 생긴 오브젝트에 새 생성 번호를 매깁니다. 이미 번호가 있으면 그 번호입니다. 게임의 스폰 복제가 이 번호를 클라이언트에 보냅니다. */
        uint32 registerSpawned( GameObject& object );
        /**
         * @brief 클라이언트 — 서버가 알린 생성 번호에 로컬 오브젝트를 묶습니다.
         * @return 생성 번호가 아니거나, 그 번호에 다른 살아 있는 오브젝트가 이미 묶였거나, 오브젝트에 다른 번호가 있으면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] bool bindSpawned( uint32 netID, GameObject& object );
        /** @brief 생성 번호를 뗍니다(오브젝트를 지우기 전). 없으면 아무것도 하지 않습니다. */
        void unregisterSpawned( const GameObject& object );
        /** @brief 생성 번호가 더해지거나 빠질 때마다 오르는 값입니다. 표를 쓰는 기능이 새 오브젝트를 등록할 때를 고릅니다. */
        uint32 getSpawnedRevision() const { return _spawnedRevision; }

        /** @brief 번호가 있는 살아 있는 오브젝트를 번호 순서로 모읍니다(씬 오브젝트 다음 생성 오브젝트). */
        void collectObjects( vector<GameObject*>& outListObject ) const;

    private:
        struct Entry
        {
            uint32           _netID{ 0 };
            GameObjectHandle _object{};
            uint8            _bAmbiguous{ SW_FALSE }; ///< 씬 번호 — 이름(해시)이 같은 오브젝트가 둘 이상이다. 아무도 이 번호를 갖지 않는다
        };

        const Entry* findSceneEntry( uint32 netID ) const;
        const Entry* findSpawnedEntryByObject( GameObjectHandle object ) const;

        GameObjectManager* _pManager;
        vector<Entry>      _listScene;   ///< 번호 순서(이분 탐색)
        vector<Entry>      _listSpawned; ///< 번호 순서(새 번호가 늘 크다 — 클라이언트의 묶기는 끼워 넣는다)
        uint32             _nextSpawned;
        uint32             _spawnedRevision;
    };
} // namespace sw
