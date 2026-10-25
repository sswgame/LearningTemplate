#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/GameObject/Service/SceneEngineServices.h"
#include "Engine/Scene/SceneDocument.h"

namespace sw
{
    class CameraComponent;
    class GameObject;
    class GameObjectManager;
    class IRHIDevice;
    class Material;

    /**
     * @class Scene
     * @brief 씬 하나의 데이터(이름 · 원본 경로 · 프리팹 출처 · 저장 ID 테이블 · 카메라 선택 · 오브젝트 매니저)를 소유합니다. 언리얼 Level/World · 유니티 Scene 에 해당하고, 오브젝트 수명은 자기 `GameObjectManager` 가 관리합니다.
     */
    class SW_API Scene
    {
    public:
        /**
         * @brief 이름과 엔진 서비스 묶음으로 씬을 만듭니다. 씬 매니저가 자기 묶음을 넘깁니다.
         * @details 묶음은 씬이 들고(기본 머티리얼 · 프리팹 캐시) 자기 오브젝트 매니저에 그대로 넘깁니다(물리 · 오디오 · 에셋). 빈 칸의 일은 건너뜁니다.
         */
        Scene( string_view name, const SceneEngineServices& services );
        /** @brief 이름으로 씬을 만듭니다. 서비스 묶음은 바인딩된 표에서 한 번 읽습니다(`SceneEngineServices::makeFromBoundEngine`) — 테스트용입니다. 엔진 · 게임 프레임워크 코드(쿠커 · 초상화 · 넷 시뮬레이션 하니스)는 묶음을 받는 생성자를 씁니다. */
        explicit Scene( string_view name );
        /** @brief 씬을 해제합니다. */
        virtual ~Scene();

        /** @brief 씬을 초기화합니다. */
        virtual bool initialize( IRHIDevice* pRHIDevice );
        /** @brief 붙들고 있던 GPU · 머티리얼 자원을 놓습니다. 파괴하기 전이나 비동기 로드 결과를 버릴 때 부릅니다. */
        virtual void shutdown();

        /**
         * @brief 씬 문서(SceneDocument)의 엔티티 · 프리팹을 스폰하고 계층 구조를 만듭니다.
         * @details 모든 엔티티를 하나의 묶음(`ObjectStateBatch`, 파일 id 공간)으로 읽고 끝에서 한 번에 잇습니다 — 자식이 부모보다 앞에 적혀도 된다.
         *          엔티티의 파일 id 는 오브젝트의 런타임 id 와 짝지어 들고 있다가(`collectSavedIDMap`) 저장할 때 같은 값을 다시 씁니다.
         *          프리팹 엔티티는 프리팹의 원형 상태에 덮어쓴 것(`SceneObjectNode::_prefabOverrideXML`)을 얹어 짓습니다 — 프리팹을 고치면 퍼집니다.
         *          전체 상태가 실린 프리팹 엔티티는 그 상태가 기준입니다. 프리팹 엔티티를 만든 뒤 그 프리팹의 중첩 인스턴스를 부품으로 스폰합니다
         *          (`PrefabCache::spawnNestedInstances`) — 부품은 씬 파일에 없고 로드할 때마다 원본에서 다시 만듭니다.
         */
        [[nodiscard]] bool instantiate( const SceneDocument& doc );
        /**
         * @brief 스트리밍 셀 하나의 엔티티를 이 씬에 더해 짓습니다(`WorldStreamingSubsystem` 이 부릅니다). 지은 오브젝트 id 를 @p outListObjectID 에 더합니다.
         * @details 셀의 엔티티는 같은 셀이나 이미 지은 늘 올라 있는 엔티티를 파일 id 로 가리킨다 — 지금 씬에 있는 파일 id 의 오브젝트를 같은 배치에 기록해
         *          그 참조를 잇는다. 실패해도 그때까지 지은 오브젝트 id 는 @p outListObjectID 에 남는다(부르는 쪽이 `unloadCell` 로 내린다).
         */
        [[nodiscard]] bool instantiateCell( const vector<SceneDocument::SceneObjectNode>& listNode, vector<uint64>& outListObjectID );
        /** @brief `instantiateCell` 이 지은 오브젝트를 자식(중첩 프리팹 부품)까지 지우고 파일 id · 프리팹 기록을 뺍니다. */
        void unloadCell( const vector<uint64>& listObjectID );
        /**
         * @brief 현재 씬의 오브젝트 상태를 씬 문서(SceneDocument)로 직렬화합니다. 부착은 부모의 파일 id 로 적습니다.
         * @details 프리팹 인스턴스는 프리팹 경로와 프리팹 원형과 다른 것만 적습니다(`PrefabOverrides` — 언리얼 · 유니티의 프리팹 인스턴스와 같다).
         *          프리팹을 읽지 못하면 전체 상태를 적습니다(다음 로드가 그 상태로 짓는다).
         *          중첩 프리팹 부품(`PrefabInstanceRegistry`)은 그것을 다시 스폰할 인스턴스 아래에 있으면 적지 않습니다. 인스턴스 밖으로 옮겼거나 인스턴스가
         *          프리팹 연결을 끊었으면 그 부품을 자기 프리팹의 인스턴스 엔티티로 적습니다(유니티의 Unpack 과 같다).
         *          주의: 씬에서 부품을 고친 것은 남지 않고(부품을 고치려면 바깥 프리팹에 적용한다), 부품에 붙인 다른 오브젝트는 다시 열면 루트로 떨어집니다.
         *          셀로 나눈 쿠킹본에서 지은 씬(셀을 스트리밍하는 씬)은 쓰지 않고 false 입니다 — 내려가 있는 셀의 엔티티가 빠진다.
         */
        [[nodiscard]] bool serializeToDocument( SceneDocument& outDoc ) const;
        /**
         * @brief 살아 있는 오브젝트마다의 파일 id 표(런타임 id → 파일 id)를 채웁니다. id 가 없는 오브젝트(새로 만든 것)에는 여기서 줍니다.
         * @details 파일 id 는 한 번 정하면 그 오브젝트가 사라져도 다시 쓰지 않습니다 — 남은 참조가 새 오브젝트를 가리키지 않게. 쿠커 · 저장이 씁니다.
         */
        void collectSavedIDMap( ObjectSavedIDMap& outMap ) const;
        /**
         * @brief 오브젝트의 파일 id 를 반환합니다. 아직 없으면(새로 만든 오브젝트) 지금 줍니다 — `collectSavedIDMap` 과 같은 규칙(이 씬이 쓴 어느 id 보다 크다)입니다.
         * @details 파일을 넘는 참조(`SceneObjectReference`)가 대상을 적을 때 씁니다. 없는 오브젝트나, 인스턴스가 다시 스폰하는 중첩 프리팹 부품(씬이 엔티티로 저장하지 않는다)이면 0 입니다. 다음 저장이 그 id 를 그대로 씁니다.
         */
        [[nodiscard]] uint64 getOrAssignFileID( uint64 objectID ) const;
        /** @brief 오브젝트에 이미 준 파일 id 입니다. 주지 않았으면 0 입니다(`getOrAssignFileID` 와 달리 새로 주지 않는다 — 참조가 있는지 묻는 쪽이 씁니다). */
        [[nodiscard]] uint64 findFileID( uint64 objectID ) const;
        /** @brief 파일 id 의 살아 있는 오브젝트 id 입니다. 이 씬에 없으면(지운 오브젝트 · 내려간 스트리밍 셀) 0 입니다. */
        [[nodiscard]] uint64 findObjectIDByFileID( uint64 fileID ) const;

        /**
         * @brief 활성 씬의 GameObject 를 병렬로 틱합니다.
         * @note 짝이 되는 `render()` 는 **없습니다.** 씬은 그리는 쪽을 모릅니다. 게임 스레드가 씬에서 스냅샷을 뽑아
         *       (`GPUSceneBuilder`) 패킷으로 넘기고, 렌더 스레드가 그것만 보고 그립니다.
         */
        virtual void update( float32 deltaTime );
        /**
         * @brief 활성 게임 카메라를 (다시) 고르고, 쓸 만한 게임 카메라가 하나도 없으면 GameCamera 를 만듭니다.
         * @details 게임 스레드가 프레임마다 부릅니다(카메라 레지스트리의 몇 개를 도는 값입니다). 직접 고른 카메라(`setActiveGameCamera`)가
         *          살아 있고 켜져 있으면 그것이, 아니면 레지스트리의 규칙(`CameraRegistry::selectCamera`, 역할 Game · 우선순위)이 고릅니다.
         *          매 프레임 다시 고르므로 나중에 생긴 더 높은 우선순위의 카메라 · 꺼진 카메라 · 역할이 바뀐 카메라를 따라갑니다. 이미 있는
         *          "GameCamera" 의 위치 · 렌즈는 건드리지 않습니다(만들 때만 기본값을 씁니다).
         */
        bool ensureDefaultCameras();

        /** @brief 씬 이름을 설정합니다. */
        void setName( string_view name ) { _name = name; }
        /** @brief 마지막 로드/저장 경로를 설정합니다. */
        void setSourcePath( string_view path ) { _sourcePath = path; }
        /** @brief 활성 게임 카메라를 직접 고릅니다. 그 카메라가 살아 있고 켜져 있는 동안 레지스트리의 선택보다 먼저입니다(nullptr 이면 해제). */
        void setActiveGameCamera( CameraComponent* pCamera );
        /** @brief 엔티티가 스폰된 프리팹 에셋 경로를 설정합니다. 비우면 연결을 끊습니다. */
        void setEntityPrefabPath( uint64 objectID, string_view prefabPath );
        /**
         * @brief 틱이 오디오 엔진에 리스너 · 에미터를 넣을지 정합니다(기본 켬). 미리보기 씬은 끕니다 — 게임의 소리를 흔들지 않게(`ScenePreviewPolicy::_bAudio`).
         */
        void setAudioUpdateEnabled( bool bEnabled ) { _bAudioUpdateEnabled = bEnabled; }
        /** @brief 틱이 오디오 엔진을 갱신하면 true 입니다. */
        bool isAudioUpdateEnabled() const { return _bAudioUpdateEnabled; }

        /** @brief 월드 설정을 바꿉니다(에디터). 다음 저장에 적힙니다 — 이미 지은 씬의 스트리밍은 바뀌지 않습니다(셀은 쿠킹이 나눈다). */
        void setWorldSettings( const WorldSettings& settings ) { _worldSettings = settings; }
        /** @brief 씬 파일에서 읽은 월드 설정입니다. */
        const WorldSettings& getWorldSettings() const { return _worldSettings; }

        /** @brief 씬 이름을 반환합니다. */
        const string& getName() const { return _name; }
        /** @brief 마지막 로드/저장 경로(리소스 상대 또는 절대)를 반환합니다. */
        const string& getSourcePath() const { return _sourcePath; }
        /** @brief 씬이 소유한 GameObjectManager 를 반환합니다. */
        GameObjectManager* getObjectManager() const { return _objectManager.get(); }
        /** @brief 만들 때 받은 엔진 서비스 묶음입니다. */
        const SceneEngineServices& getEngineServices() const { return _services; }
        /** @brief 씬 기본 머티리얼입니다(MaterialCache 에서 빌린 포인터, 소유하지 않음). */
        Material* getMaterial() const { return _pMaterial; }
        /** @brief 활성 게임 카메라를 반환합니다. */
        CameraComponent* getActiveGameCamera() const;

        /**
         * @brief 씬의 주광입니다. 없으면 nullptr 이고, 렌더러가 기본값으로 폴백합니다.
         * @details 카메라와 달리 캐시하지 않고 그때그때 찾습니다(빛 레지스트리만 봅니다). 프레임당 한 번만 불리고
         *          (RenderFramePacket 을 채울 때) 라이트는 보통 한두 개입니다.
         */
        class DirectionalLightComponent* findActiveDirectionalLight() const;
        /**
         * @brief 그림자 맵을 가져가는 방향광입니다 — 켜져 있고 그림자를 드리우는 첫 방향광(등록 순서). 없으면 nullptr 입니다.
         * @details 그림자 행렬을 만드는 쪽(`Engine` · `FrameRenderer`)과 그림자 플래그를 싣는 쪽(`collectSceneLights`)이
         *          **둘 다 이것을 부릅니다.** 주의: 둘이 따로 고르면 첫 방향광이 그림자를 끄고 뒤의 빛이 켤 때 행렬은 비고 플래그는 뒤의
         *          빛에 붙어, 그림자를 드리우는 빛에 그림자가 지지 않는다. 방향 · 색 · 앰비언트는 그대로 주광에서 옵니다.
         */
        class DirectionalLightComponent* findShadowCastingDirectionalLight() const;
        /** @brief 엔티티가 스폰된 프리팹 에셋 경로를 반환합니다(없으면 빈 문자열). */
        string_view getEntityPrefabPath( uint64 objectID ) const;

        /**
         * @brief 프리팹을 찾지 못해 오브젝트로 만들지 못한 엔티티의 수입니다(유니티의 "Missing Prefab").
         * @details 그 엔티티는 문서 그대로 들고 있다가 `serializeToDocument` 가 다시 써 넣는다 — 열고 저장하는 것만으로 데이터가
         *          사라지지 않게. 0 이 아니면 에디터 · 로그가 알려야 할 상태다.
         */
        size_t getUnresolvedEntityCount() const { return _listUnresolvedEntity.size(); }

    private:
        /** @brief 기본 머티리얼 참조를 해제합니다. */
        void releaseDefaultMaterial();
        /** @brief 새 오브젝트에 줄 파일 id 입니다 — 이 씬이 쓴 어느 파일 id(해석하지 못한 엔티티 포함)보다 큽니다. */
        uint64 computeNextFileID() const;
        /**
         * @brief 엔티티 목록을 한 배치로 지어 이 씬에 더합니다(`instantiate` · `instantiateCell` 의 본문).
         * @param bStreamingCell 스트리밍 셀을 짓는다. 지금 씬에 있는 파일 id 의 오브젝트를 배치에 먼저 기록해 엔티티가 그것을 파일 id 로 가리킬 수 있게 하고,
         *                       프리팹을 찾지 못한 엔티티는 보관하지 않습니다(셀은 다시 지어질 때마다 같은 엔티티를 낸다 — 쿠킹본은 저장하지 않는다).
         * @param pOutListObjectID 주면 지은 오브젝트 id 를 더합니다(엔티티마다 하나, 중첩 프리팹 부품 제외).
         */
        [[nodiscard]] bool instantiateNodes( const vector<SceneDocument::SceneObjectNode>& listNode, bool bStreamingCell, vector<uint64>* pOutListObjectID );

        /** @brief 컴포넌트 핸들로 카메라를 다시 찾습니다. */
        CameraComponent* resolveCamera( TComponentHandle<CameraComponent> handle ) const;
        /** @brief 카메라 핸들을 기록합니다. */
        void storeCameraHandle( CameraComponent* pCamera, TComponentHandle<CameraComponent>& outHandle );

        string                        _name;
        string                        _sourcePath;
        string                        _defaultMaterialPath;
        WorldSettings                 _worldSettings; ///< 씬 파일의 월드 설정(`instantiate` 가 읽고 `serializeToDocument` 가 쓴다)
        SceneEngineServices           _services;      ///< 만들 때 받은 엔진 서비스(빌림). 오브젝트 매니저도 같은 묶음으로 만든다
        unique_ptr<GameObjectManager> _objectManager;
        Material*                     _pMaterial;
        unordered_map<uint64, string> _mapPrefabSource;
        /**
         * @brief 런타임 오브젝트 id → 파일 id 입니다. 읽을 때 엔티티의 id 로 채우고 저장 때 없는 것을 새로 줍니다(`collectSavedIDMap`).
         * @details 저장이 const 라 mutable 입니다 — 새 오브젝트에 id 를 주는 것은 씬의 보이는 상태를 바꾸지 않습니다.
         */
        mutable ObjectSavedIDMap               _mapObjectIDToFileID;
        vector<SceneDocument::SceneObjectNode> _listUnresolvedEntity; ///< 프리팹을 찾지 못한 엔티티(문서 그대로, 저장 때 다시 써 넣는다)
        TComponentHandle<CameraComponent>      _activeGameCamera;     ///< 마지막 `ensureDefaultCameras` 가 고른 카메라(렌더 쪽이 O(1) 로 읽는다)
        TComponentHandle<CameraComponent>      _gameCameraOverride;   ///< `setActiveGameCamera` 로 직접 고른 카메라
        bool                                   _bAudioUpdateEnabled;  ///< `setAudioUpdateEnabled`
    };
} // namespace sw
