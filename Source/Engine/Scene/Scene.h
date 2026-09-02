#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"

#include "Engine/Object/Component/ComponentHandle.h"

namespace sw
{
    class GameObjectManager;
    class GameObject;
    class IRHIDevice;
    class Material;
    class FrameRenderer;
    class CameraComponent;
    struct SceneDocument;

    /**
     * @class Scene
     * @brief 게임 월드의 기본 단위 (Level/World). 고유의 GameObjectManager를 소유합니다.
     */
    class SW_API Scene
    {
    public:
        /** @brief 이름으로 씬을 만듭니다. */
        explicit Scene( string_view name );
        /** @brief 씬을 해제합니다. */
        virtual ~Scene();

        /** @brief 씬 초기화 */
        virtual bool initialize( IRHIDevice* pRhiDevice );
        /** @brief GPU/머티리얼 보유를 해제합니다. 파기 전·비동기 로드 폐기 시 호출합니다. */
        virtual void shutdown();

        /** @brief 씬 문서(SceneDocument)로부터 엔티티/프리팹을 스폰하고 계층 구조를 인스턴스화합니다. */
        bool instantiate( const SceneDocument& doc );
        /** @brief 현재 씬의 루트 오브젝트 상태를 씬 문서(SceneDocument)로 직렬화 추출합니다. */
        bool serializeToDocument( SceneDocument& outDoc ) const;

        /** @brief 활성 씬의 GameObject를 병렬 tick합니다. */
        virtual void tick( float32 deltaTime );
        /** @brief FrameRenderer로 GameObject를 렌더링합니다. */
        virtual void render( IRHIDevice* pRhiDevice );
        /**
         * @brief 없으면 GameCamera GameObject를 생성합니다.
         * @details CameraComponent(역할 Game)를 가집니다. init마다 호출해도 안전합니다.
         */
        bool ensureDefaultCameras();
        /** @brief 씬에서 Game 역할 최고 우선순위 카메라를 다시 찾습니다. */
        void refreshCameraCache();

        /** @brief App이 소유한 FrameRenderer를 연결합니다 (비소유). */
        void setFrameRenderer( FrameRenderer* pFrameRenderer ) { _pFrameRenderer = pFrameRenderer; }
        /** @brief 씬 이름을 설정합니다. */
        void setName( string_view name ) { _name = name; }
        /** @brief 마지막 로드/저장 경로를 설정합니다. */
        void setSourcePath( string_view path ) { _sourcePath = path; }
        /** @brief 활성 게임 카메라를 설정합니다. */
        void setActiveGameCamera( CameraComponent* pCamera );
        /** @brief 엔티티가 스폰된 프리팹 애셋 경로를 설정합니다. 비우면 연결을 끊습니다. */
        void setEntityPrefabPath( uint64 objectId, string_view prefabPath );

        /** @brief 연결된 FrameRenderer를 반환합니다. */
        FrameRenderer* getFrameRenderer() const { return _pFrameRenderer; }
        /** @brief 씬 이름을 반환합니다. */
        const string& getName() const { return _name; }
        /** @brief 마지막 로드/저장 경로(리소스 상대 또는 절대)를 반환합니다. */
        const string& getSourcePath() const { return _sourcePath; }
        /** @brief 기본 머티리얼 에셋 경로(MaterialCache 키)를 반환합니다. */
        const string& getDefaultMaterialPath() const { return _defaultMaterialPath; }
        /** @brief 씬이 소유한 GameObjectManager 반환 */
        GameObjectManager* getObjectManager() const { return _objectManager.get(); }
        /** @brief MaterialCache에서 빌린 포인터(비소유)를 반환합니다. */
        Material* getMaterial() const { return _pMaterial; }
        /** @brief 활성 게임 카메라를 반환합니다. */
        CameraComponent* getActiveGameCamera() const;
        /** @brief 엔티티가 스폰된 프리팹 애셋 경로를 반환합니다 (없으면 빈 문자열). */
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
        FrameRenderer*                _pFrameRenderer;
        unordered_map<uint64, string> _mapPrefabSource;
        sw::ComponentHandle           _activeGameCamera;
        bool                          _bCamerasEnsured;
    };
} // namespace sw
