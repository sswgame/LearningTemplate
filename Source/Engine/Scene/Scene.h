#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"

namespace sw
{
    struct SceneDocument;

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

        /** @brief 씬 문서(SceneDocument)의 엔티티 · 프리팹을 스폰하고 계층 구조를 만듭니다. */
        bool instantiate( const SceneDocument& doc );
        /** @brief 현재 씬의 루트 오브젝트 상태를 씬 문서(SceneDocument)로 직렬화합니다. */
        bool serializeToDocument( SceneDocument& outDoc ) const;

        /**
         * @brief 활성 씬의 GameObject 를 병렬로 틱합니다.
         * @note 짝이 되는 `render()` 는 **없습니다.** 씬은 그리는 쪽을 모릅니다. 게임 스레드가 씬에서 스냅샷을 뽑아
         *       (`GpuSceneBuilder`) 패킷으로 넘기고, 렌더 스레드가 그것만 보고 그립니다. 예전에는 씬이
         *       `FrameRenderer` 포인터를 들고 `execute( this )` 를 부르는 길이 있었지만 아무도 부르지 않았습니다.
         */
        virtual void tick( float32 deltaTime );
        /**
         * @brief GameCamera GameObject 가 없으면 만듭니다.
         * @details CameraComponent(역할 Game)를 가집니다. 초기화할 때마다 불러도 안전합니다.
         */
        bool ensureDefaultCameras();
        /** @brief 씬에서 Game 역할 중 우선순위가 가장 높은 카메라를 다시 찾습니다. */
        void refreshCameraCache();

        /** @brief 씬 이름을 설정합니다. */
        void setName( string_view name ) { _name = name; }
        /** @brief 마지막 로드/저장 경로를 설정합니다. */
        void setSourcePath( string_view path ) { _sourcePath = path; }
        /** @brief 활성 게임 카메라를 설정합니다. */
        void setActiveGameCamera( CameraComponent* pCamera );
        /** @brief 엔티티가 스폰된 프리팹 에셋 경로를 설정합니다. 비우면 연결을 끊습니다. */
        void setEntityPrefabPath( uint64 objectId, string_view prefabPath );

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
        /** @brief 엔티티가 스폰된 프리팹 에셋 경로를 반환합니다(없으면 빈 문자열). */
        const string& getEntityPrefabPath( uint64 objectId ) const;

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
        sw::ComponentHandle           _activeGameCamera;
        bool                          _bCamerasEnsured;
    };
} // namespace sw
