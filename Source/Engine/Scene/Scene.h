#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Object/GameObject/ObjectStateSerializer.h"
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
     * @brief 게임 월드의 기본 단위입니다(Level/World). 자기 GameObjectManager 를 소유합니다.
     */
    class SW_API Scene
    {
    public:
        /** @brief 이름으로 씬을 만듭니다. */
        explicit Scene( string_view name );
        /** @brief 씬을 해제합니다. */
        virtual ~Scene();

        /** @brief 씬을 초기화합니다. */
        virtual bool initialize( IRHIDevice* pRhiDevice );
        /** @brief 붙들고 있던 GPU · 머티리얼 자원을 놓습니다. 파괴하기 전이나 비동기 로드 결과를 버릴 때 부릅니다. */
        virtual void shutdown();

        /**
         * @brief 씬 문서(SceneDocument)의 엔티티 · 프리팹을 스폰하고 계층 구조를 만듭니다.
         * @details 모든 엔티티를 하나의 묶음(`ObjectStateBatch`, 파일 id 공간)으로 읽고 끝에서 한 번에 잇습니다 — 자식이 부모보다 앞에 적혀도 된다.
         *          엔티티의 파일 id 는 오브젝트의 런타임 id 와 짝지어 들고 있다가(`collectSavedIDMap`) 저장할 때 같은 값을 다시 씁니다.
         *          프리팹 엔티티는 프리팹의 원형 상태에 덮어쓴 것(`SceneObjectNode::_prefabOverrideXml`)을 얹어 짓습니다 — 프리팹을 고치면 퍼집니다.
         *          전체 상태가 실린 프리팹 엔티티는 그 상태가 기준입니다.
         */
        [[nodiscard]] bool instantiate( const SceneDocument& doc );
        /**
         * @brief 현재 씬의 오브젝트 상태를 씬 문서(SceneDocument)로 직렬화합니다. 부착은 부모의 파일 id 로 적습니다.
         * @details 프리팹 인스턴스는 프리팹 경로와 프리팹 원형과 다른 것만 적습니다(`PrefabOverrides` — 언리얼 · 유니티의 프리팹 인스턴스와 같다).
         *          프리팹을 읽지 못하면 전체 상태를 적습니다(다음 로드가 그 상태로 짓는다).
         */
        [[nodiscard]] bool serializeToDocument( SceneDocument& outDoc ) const;
        /**
         * @brief 살아 있는 오브젝트마다의 파일 id 표(런타임 id → 파일 id)를 채웁니다. id 가 없는 오브젝트(새로 만든 것)에는 여기서 줍니다.
         * @details 파일 id 는 한 번 정하면 그 오브젝트가 사라져도 다시 쓰지 않습니다 — 남은 참조가 새 오브젝트를 가리키지 않게. 쿠커 · 저장이 씁니다.
         */
        void collectSavedIDMap( ObjectSavedIDMap& outMap ) const;

        /**
         * @brief 활성 씬의 GameObject 를 병렬로 틱합니다.
         * @note 짝이 되는 `render()` 는 **없습니다.** 씬은 그리는 쪽을 모릅니다. 게임 스레드가 씬에서 스냅샷을 뽑아
         *       (`GpuSceneBuilder`) 패킷으로 넘기고, 렌더 스레드가 그것만 보고 그립니다.
         */
        virtual void tick( float32 deltaTime );
        /**
         * @brief 활성 게임 카메라를 (다시) 고르고, 쓸 만한 게임 카메라가 하나도 없으면 GameCamera 를 만듭니다.
         * @details 게임 스레드가 프레임마다 부릅니다(카메라 등록부의 몇 개를 도는 값입니다). 직접 고른 카메라(`setActiveGameCamera`)가
         *          살아 있고 켜져 있으면 그것이, 아니면 등록부의 규칙(`CameraRegistry::selectCamera`, 역할 Game · 우선순위)이 고릅니다.
         *          매 프레임 다시 고르므로 나중에 생긴 더 높은 우선순위의 카메라 · 꺼진 카메라 · 역할이 바뀐 카메라를 따라갑니다. 이미 있는
         *          "GameCamera" 의 위치 · 렌즈는 건드리지 않습니다(만들 때만 기본값을 씁니다).
         */
        bool ensureDefaultCameras();

        /** @brief 씬 이름을 설정합니다. */
        void setName( string_view name ) { _name = name; }
        /** @brief 마지막 로드/저장 경로를 설정합니다. */
        void setSourcePath( string_view path ) { _sourcePath = path; }
        /** @brief 활성 게임 카메라를 직접 고릅니다. 그 카메라가 살아 있고 켜져 있는 동안 등록부의 선택보다 먼저입니다(nullptr 이면 해제). */
        void setActiveGameCamera( CameraComponent* pCamera );
        /** @brief 엔티티가 스폰된 프리팹 에셋 경로를 설정합니다. 비우면 연결을 끊습니다. */
        void setEntityPrefabPath( uint64 objectID, string_view prefabPath );

        /** @brief 씬 이름을 반환합니다. */
        const string& getName() const { return _name; }
        /** @brief 마지막 로드/저장 경로(리소스 상대 또는 절대)를 반환합니다. */
        const string& getSourcePath() const { return _sourcePath; }
        /** @brief 씬이 소유한 GameObjectManager 를 반환합니다. */
        GameObjectManager* getObjectManager() const { return _objectManager.get(); }
        /** @brief 씬 기본 머티리얼입니다(MaterialCache 에서 빌린 포인터, 소유하지 않음). */
        Material* getMaterial() const { return _pMaterial; }
        /** @brief 활성 게임 카메라를 반환합니다. */
        CameraComponent* getActiveGameCamera() const;

        /**
         * @brief 씬의 주광입니다. 없으면 nullptr 이고, 렌더러가 기본값으로 폴백합니다.
         * @details 카메라와 달리 캐시하지 않고 그때그때 찾습니다(빛 등록부만 봅니다). 프레임당 한 번만 불리고
         *          (RenderFramePacket 을 채울 때) 라이트는 보통 한두 개입니다.
         */
        class DirectionalLightComponent* findActiveDirectionalLight() const;
        /**
         * @brief 그림자 맵을 가져가는 방향광입니다 — 켜져 있고 그림자를 드리우는 첫 방향광(등록 순서). 없으면 nullptr 입니다.
         * @details 그림자 행렬을 만드는 쪽(`EngineLoop` · `FrameRenderer`)과 그림자 플래그를 싣는 쪽(`collectSceneLights`)이
         *          **둘 다 이것을 부릅니다.** 주의: 둘이 따로 고르면 첫 방향광이 그림자를 끄고 뒤의 빛이 켤 때 행렬은 비고 플래그는 뒤의
         *          빛에 붙어, 그림자를 드리우는 빛에 그림자가 지지 않는다. 방향 · 색 · 앰비언트는 그대로 주광에서 옵니다.
         */
        class DirectionalLightComponent* findShadowCastingDirectionalLight() const;
        /** @brief 엔티티가 스폰된 프리팹 에셋 경로를 반환합니다(없으면 빈 문자열). */
        const string& getEntityPrefabPath( uint64 objectID ) const;

        /**
         * @brief 프리팹을 찾지 못해 오브젝트로 만들지 못한 엔티티의 수입니다(유니티의 "Missing Prefab").
         * @details 그 엔티티는 문서 그대로 들고 있다가 `serializeToDocument` 가 다시 써 넣는다 — 열고 저장하는 것만으로 데이터가
         *          사라지지 않게. 0 이 아니면 에디터 · 로그가 알려야 할 상태다.
         */
        size_t getUnresolvedEntityCount() const { return _listUnresolvedEntity.size(); }

    private:
        /** @brief 기본 머티리얼 참조를 해제합니다. */
        void releaseDefaultMaterial();

        /** @brief 컴포넌트 핸들로 카메라를 다시 찾습니다. */
        CameraComponent* resolveCamera( sw::ComponentHandle handle ) const;
        /** @brief 카메라 핸들을 기록합니다. */
        void storeCameraHandle( CameraComponent* pCamera, sw::ComponentHandle& handle );

        string                        _name;
        string                        _sourcePath;
        string                        _defaultMaterialPath;
        unique_ptr<GameObjectManager> _objectManager;
        Material*                     _pMaterial;
        unordered_map<uint64, string> _mapPrefabSource;
        /**
         * @brief 런타임 오브젝트 id → 파일 id 입니다. 읽을 때 엔티티의 id 로 채우고 저장 때 없는 것을 새로 줍니다(`collectSavedIDMap`).
         * @details 저장이 const 라 mutable 입니다 — 새 오브젝트에 id 를 주는 것은 씬의 보이는 상태를 바꾸지 않습니다.
         */
        mutable ObjectSavedIDMap               _mapObjectIDToFileID;
        vector<SceneDocument::SceneObjectNode> _listUnresolvedEntity; ///< 프리팹을 찾지 못한 엔티티(문서 그대로, 저장 때 다시 써 넣는다)
        sw::ComponentHandle                    _activeGameCamera;     ///< 마지막 `ensureDefaultCameras` 가 고른 카메라(렌더 쪽이 O(1) 로 읽는다)
        sw::ComponentHandle                    _gameCameraOverride;   ///< `setActiveGameCamera` 로 직접 고른 카메라
    };
} // namespace sw
