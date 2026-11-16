#pragma once
#include "Core/Concurrency/StopToken.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/GameObject/Service/SceneEngineServices.h"
#include "Engine/Scene/SceneDataLayerSubsystem.h"
#include "Engine/Scene/SceneDocument.h"

namespace sw
{
    struct PrefabPartOverride;

    class CameraComponent;
    class GameObject;
    class GameObjectManager;
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
        /** @brief 씬을 해제합니다. 붙들고 있던 GPU · 머티리얼 자원과 오브젝트 · 스트리밍 셀을 여기서 놓습니다(정리하는 길은 소멸 하나). */
        virtual ~Scene();

        /**
         * @brief 기본 머티리얼을 얻고 기본 카메라를 겁니다. 메시 기본값은 렌더 쪽이 겁니다(`ISceneRenderBinding::bindSceneDefaults`).
         * @details 매니저가 받는 씬은 받아들이는 길(`SceneManager::applyScene`)이 부릅니다 — 매니저 밖에서 부르는 것은 단위 테스트뿐입니다.
         */
        [[nodiscard]] bool initialize();

        /**
         * @brief 씬 문서(SceneDocument)의 엔티티 · 프리팹을 스폰하고 계층 구조를 만듭니다.
         * @details 모든 엔티티를 하나의 묶음(`ObjectStateBatch`, 파일 id 공간)으로 읽고 끝에서 한 번에 잇습니다 — 자식이 부모보다 앞에 적혀도 된다.
         *          엔티티의 파일 id 는 오브젝트의 런타임 id 와 짝지어 들고 있다가(`collectSavedIDMap`) 저장할 때 같은 값을 다시 씁니다.
         *          프리팹 엔티티는 프리팹의 원형 상태에 덮어쓴 것(`SceneObjectNode::_prefabOverrideXML`)을 얹어 짓습니다 — 프리팹을 고치면 퍼집니다.
         *          전체 상태가 실린 프리팹 엔티티는 그 상태가 기준입니다. 프리팹 엔티티를 만든 뒤 그 프리팹의 중첩 인스턴스를 부품으로 스폰합니다
         *          (`PrefabCache::spawnNestedInstances`) — 부품은 씬 파일에 없고 로드할 때마다 원본에서 다시 만듭니다.
         */
        [[nodiscard]] bool instantiate( const SceneDocument& doc, const StopToken& stopToken = {} );
        /**
         * @brief 스트리밍 셀 하나의 엔티티를 이 씬에 더해 짓습니다(`SceneStreamingSubsystem` 이 부릅니다). 지은 오브젝트 id 를 @p outListObjectID 에 더합니다.
         * @details 셀의 엔티티는 같은 셀이나 이미 지은 늘 올라 있는 엔티티를 파일 id 로 가리킨다 — 지금 씬에 있는 파일 id 의 오브젝트를 같은 배치에 기록해
         *          그 참조를 잇는다. 실패해도 그때까지 지은 오브젝트 id 는 @p outListObjectID 에 남는다(부르는 쪽이 `unloadCell` 로 내린다).
         */
        [[nodiscard]] bool instantiateCell( const vector<SceneDocument::SceneObjectNode>& listNode, vector<uint64>& outListObjectID );
        /** @brief `instantiateCell` 이 지은 오브젝트를 자식(중첩 프리팹 부품)까지 지우고 파일 id · 프리팹 기록을 뺍니다. */
        void unloadCell( const vector<uint64>& listObjectID );
        /** @brief 데이터 레이어 묶음의 엔티티를 이 씬에 더해 짓습니다(`SceneDataLayerSubsystem` 이 부릅니다). 셀과 같은 길이고, 프리팹을 찾지 못한 엔티티는 보관합니다(저장이 다시 쓴다). 내릴 때는 `unloadCell` 입니다. */
        [[nodiscard]] bool instantiateLayerNodes( const vector<SceneDocument::SceneObjectNode>& listNode, vector<uint64>& outListObjectID );
        /** @brief 다음 `instantiate` 가 처음 상태와 상관없이 모든 데이터 레이어를 켜고 짓습니다. 쿠커가 씁니다 — 모든 엔티티를 지어 봐야 상태 · 자리 · 참조를 안다. */
        void setBuildAllDataLayers() { _bBuildAllDataLayers = SW_TRUE; }
        /**
         * @brief 현재 씬의 오브젝트 상태를 씬 문서(SceneDocument)로 직렬화합니다. 부착은 부모의 파일 id 로 적습니다.
         * @details 프리팹 인스턴스는 프리팹 경로와 프리팹 원형과 다른 것만 적습니다(`PrefabOverrides` — 언리얼 · 유니티의 프리팹 인스턴스와 같다).
         *          프리팹을 읽지 못하면 전체 상태를 적습니다(다음 로드가 그 상태로 짓는다).
         *          중첩 프리팹 부품(`PrefabInstanceRegistry`)은 그것을 다시 스폰할 인스턴스 아래에 있으면 적지 않습니다. 인스턴스 밖으로 옮겼거나 인스턴스가
         *          프리팹 연결을 끊었으면 그 부품을 자기 프리팹의 인스턴스 엔티티로 적습니다(유니티의 Unpack 과 같다).
         *          주의: 씬에서 부품을 고친 것은 남지 않고(부품을 고치려면 바깥 프리팹에 적용한다), 부품에 붙인 다른 오브젝트는 다시 열면 루트로 떨어집니다.
         *          셀로 나눈 쿠킹본에서 지은 씬(셀을 스트리밍하는 씬)은 쓰지 않고 false 입니다 — 내려가 있는 셀의 엔티티가 빠진다.
         *          씬 인스턴스의 멤버는 엔티티로 적지 않고, 내부 씬과 다른 것을 맨 위 앵커의 `<Part path>` 로 적습니다(프리팹 부품과 같은 모델). 멤버를 지운 것은 남지 않습니다(프리팹 부품 · 유니티와 같다).
         *          짓지 않은 레이어 묶음의 엔티티는 문서 그대로 다시 적습니다.
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
         * @return 고른 카메라가 있으면 true 입니다(오브젝트 관리자가 없으면 false). 실패가 아니라 "골랐나" 이므로 버려도 됩니다 — 렌더 경로가 `getActiveGameCamera` 로 다시 봅니다.
         */
        bool pickActiveGameCamera();

        /** @brief 씬 이름을 설정합니다. */
        void setName( string_view name ) { _name = name; }
        /** @brief 마지막 로드/저장 경로를 설정합니다. */
        void setSourcePath( string_view path ) { _sourcePath = path; }
        /**
         * @brief 다음 `instantiate` 의 등록 · 로드 뒤 처리를 `finishHeldRegistration` 까지 쥡니다. 워커에서 짓는 비동기 로드가 짓기 전에 부릅니다.
         * @details 훅(`onRegister` · `onPostLoad`)은 게임 스레드에서만 돈다 — 씬을 받는 쪽(`SceneManager::applyScene`)이 끝낸다.
         */
        void holdRegistration();
        /** @brief 쥔 등록이 있으면 부릅니다(게임 스레드). 없으면 아무것도 하지 않습니다. */
        void finishHeldRegistration();
        /** @brief 쥔 등록을 @p maxCount 개까지 부릅니다(`GameObjectManager::processDeferredRegistration`, 0 은 한도 없음). 쥔 것이 없거나 다 끝나면 true 입니다(게임 스레드). */
        [[nodiscard]] bool processHeldRegistration( uint32 maxCount );
        /** @brief 활성 게임 카메라를 직접 고릅니다. 그 카메라가 살아 있고 켜져 있는 동안 레지스트리의 선택보다 먼저입니다(nullptr 이면 해제). */
        void setActiveGameCamera( CameraComponent* pCamera );
        /** @brief 엔티티가 스폰된 프리팹 에셋 경로를 설정합니다. 비우면 연결을 끊습니다. */
        void setEntityPrefabPath( uint64 objectID, string_view prefabPath );
        /** @brief 엔티티의 데이터 레이어를 바꿉니다(에디터). 다음 저장에 적힙니다. 인스턴스 멤버는 바꾸지 않고 false 입니다 — 레이어는 파일의 엔티티에 붙는다. */
        [[nodiscard]] bool setEntityDataLayers( uint64 objectID, vector<string> listLayer );

        /** @brief 월드 설정을 바꿉니다(에디터). 다음 저장에 적힙니다 — 이미 지은 씬의 스트리밍은 바뀌지 않습니다(셀은 쿠킹이 나눈다). */
        void setSceneSettings( const SceneSettings& settings ) { _sceneSettings = settings; }
        /** @brief 씬 파일에서 읽은 월드 설정입니다. */
        const SceneSettings& getSceneSettings() const { return _sceneSettings; }

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

        /** @brief 엔티티가 스폰된 프리팹 에셋 경로를 반환합니다(없으면 빈 문자열). */
        string_view getEntityPrefabPath( uint64 objectID ) const;
        /** @brief 씬 인스턴스 앵커면 넣은 씬의 리소스 id 입니다(없으면 빈 문자열). */
        string_view getSceneInstancePath( uint64 objectID ) const;
        /** @brief 씬 인스턴스 멤버면 true 입니다. */
        bool isSceneInstanceMember( uint64 objectID ) const { return _mapInstanceMember.find( objectID ) != _mapInstanceMember.end(); }
        /** @brief 씬 인스턴스 멤버면 자기를 펼친 앵커의 오브젝트 id 입니다. 멤버가 아니거나 앵커가 없으면 0 입니다. */
        [[nodiscard]] uint64 findInstanceAnchorObjectID( uint64 objectID ) const;
        /** @brief 엔티티가 붙은 데이터 레이어입니다(인스턴스 멤버는 앵커의 것을 더한 것). 없으면 nullptr 입니다. */
        const vector<string>* findEntityDataLayers( uint64 objectID ) const;
        /** @brief 데이터 레이어 @p name 에 붙은, 지금 지어진 오브젝트를 모읍니다(비우고 채웁니다). */
        void collectObjectsOnDataLayer( string_view name, vector<uint64>& outListObjectID ) const;
        /** @brief 씬 상속의 부모입니다(루트의 `_base`). 없으면 빈 문자열입니다. */
        const string& getBaseScenePath() const { return _baseScenePath; }

        /** @brief 씬 인스턴스 멤버 하나의 기록입니다. */
        struct InstanceMember
        {
            uint64 _anchorFileID{ 0 }; ///< 자기를 펼친(가장 가까운) 앵커의 평평한 파일 id
            uint64 _idScope{ 0 };      ///< 내부 파일 id 와의 XOR 값(`SceneObjectNode::_idScope`)
            string _partPath;          ///< 문서 뿌리에서의 경로(`SceneObjectNode::_instancePartPath`)
        };

        /** @brief 오브젝트 상태에 없는 조립 기록을 값으로 찍은 것입니다. 플레이 종료 복원이 씬을 다시 만들어도 인스턴스 · 레이어 · 상속이 저장에서 풀리지 않게 들고 있습니다. */
        struct AssemblySnapshot
        {
            SceneSettings                          _sceneSettings{};
            string                                 _baseScenePath;
            unordered_map<uint64, string>          _mapSceneInstancePath;
            unordered_map<uint64, InstanceMember>  _mapInstanceMember;
            unordered_map<uint64, vector<string>>  _mapObjectDataLayer;
            ObjectSavedIDMap                       _mapObjectIDToFileID;
            vector<SceneDocument::SceneObjectNode> _listUnresolvedEntity;
            SceneDataLayerSubsystem::State         _dataLayers;
        };
        /** @brief 지금 조립 기록을 찍습니다. */
        [[nodiscard]] AssemblySnapshot captureAssembly() const;
        /** @brief @p snapshot 으로 되돌립니다. 오브젝트는 부르는 쪽이 같은 id 로 복원합니다. */
        void restoreAssembly( AssemblySnapshot snapshot );

        /**
         * @brief 프리팹을 찾지 못해 오브젝트로 만들지 못한 엔티티의 수입니다(유니티의 "Missing Prefab").
         * @details 그 엔티티는 문서 그대로 들고 있다가 `serializeToDocument` 가 다시 써 넣는다 — 열고 저장하는 것만으로 데이터가
         *          사라지지 않게. 0 이 아니면 에디터 · 로그가 알려야 할 상태다.
         */
        size_t getUnresolvedEntityCount() const { return _listUnresolvedEntity.size(); }

    private:
        /** @brief 붙들고 있던 GPU · 머티리얼 자원과 오브젝트를 놓습니다. 소멸자만 부릅니다(내리는 길을 소멸 하나로 둔다). */
        void shutdown();
        /** @brief 기본 머티리얼 참조를 해제합니다. */
        void releaseDefaultMaterial();
        /** @brief 새 오브젝트에 줄 파일 id 입니다 — 이 씬이 쓴 어느 파일 id(해석하지 못한 엔티티 · 짓지 않은 레이어 엔티티 포함)보다 큽니다. 인스턴스 멤버의 평평한 id 는 넓은 값이라 세지 않습니다. */
        uint64 computeNextFileID() const;
        /**
         * @brief 맨 위 앵커의 파일 id 마다 멤버에 덮어쓴 것(`<Part>` 항목)을 모읍니다(비우고 채웁니다).
         * @details 살아 있는 멤버는 내부 씬을 임시로 지어 같은 직렬화기로 쓴 상태와 비교합니다(프리팹 원형 `makeBaseState` 와 같은 까닭 — 손으로 쓴 글과 비교하면 값이 같아도 다르다).
         *          올리지 않은 멤버(끈 레이어 · 프리팹을 못 찾음)는 읽은 값을 그대로 다시 적습니다.
         * @param mapNestedPart 멤버마다 프리팹 부품에 덮어쓴 것입니다(오브젝트 id). 멤버의 항목 안에 함께 적습니다.
         */
        void collectSceneInstanceParts( const unordered_map<uint64, vector<PrefabPartOverride>>& mapNestedPart, unordered_map<uint64, vector<PrefabPartOverride>>& outMapAnchorPart ) const;
        /** @brief `instantiateNodes` 가 짓는 조각의 종류입니다. */
        enum class NodeBuild : uint8
        {
            Document,  ///< 문서의 늘 올라 있는 엔티티 — 처음 짓는다
            Cell,      ///< 스트리밍 셀 — 지금 씬의 오브젝트를 배치에 기록해 파일 id 로 가리킬 수 있게 하고, 프리팹을 찾지 못한 엔티티는 보관하지 않는다(셀은 다시 지어질 때마다 같은 엔티티를 낸다 — 쿠킹본은 저장하지 않는다)
            DataLayer, ///< 데이터 레이어 묶음 — 셀처럼 배치에 기록하되, 프리팹을 찾지 못한 엔티티는 보관한다(소스에서 연 씬은 저장한다)
        };
        /**
         * @brief 엔티티 목록을 한 배치로 지어 이 씬에 더합니다(`instantiate` · `instantiateCell` · `instantiateLayerNodes` 의 본문). 문서 · 셀 · 레이어 묶음 · 펼친 인스턴스가 모두 이 한 길입니다.
         * @param pOutListObjectID 주면 지은 오브젝트 id 를 더합니다(엔티티마다 하나, 중첩 프리팹 부품 제외).
         */
        [[nodiscard]] bool instantiateNodes( const vector<SceneDocument::SceneObjectNode>& listNode, NodeBuild build, vector<uint64>* pOutListObjectID, const StopToken& stopToken = {} );

        /** @brief 컴포넌트 핸들로 카메라를 다시 찾습니다. */
        CameraComponent* resolveCamera( TComponentHandle<CameraComponent> handle ) const;
        /** @brief 카메라 핸들을 기록합니다. */
        void storeCameraHandle( CameraComponent* pCamera, TComponentHandle<CameraComponent>& outHandle );

    private:
        string                        _name;
        string                        _sourcePath;
        string                        _defaultMaterialPath;
        SceneSettings                 _sceneSettings; ///< 씬 파일의 월드 설정(`instantiate` 가 읽고 `serializeToDocument` 가 쓴다)
        SceneEngineServices           _services;      ///< 만들 때 받은 엔진 서비스(빌림). 오브젝트 매니저도 같은 묶음으로 만든다
        unique_ptr<GameObjectManager> _objectManager;
        Material*                     _pMaterial;
        unordered_map<uint64, string> _mapPrefabSource;
        string                        _baseScenePath;        ///< 씬 상속의 부모(문서에서 읽고 저장이 차이로 쓴다)
        unordered_map<uint64, string> _mapSceneInstancePath; ///< 앵커 오브젝트 id → 넣은 씬
        /** @brief 멤버 오브젝트 id → 기록입니다. 프리팹 경로표처럼 오브젝트 id 로 들고 있어 되돌리기 · 플레이 복원이 같은 id 로 돌아오면 그대로 맞습니다. */
        unordered_map<uint64, InstanceMember> _mapInstanceMember;
        unordered_map<uint64, vector<string>> _mapObjectDataLayer; ///< 레이어가 붙은 오브젝트 id → 레이어(`SceneObjectNode::_listDataLayer`)
        /**
         * @brief 런타임 오브젝트 id → 파일 id 입니다. 읽을 때 엔티티의 id 로 채우고 저장 때 없는 것을 새로 줍니다(`collectSavedIDMap`).
         * @details 저장이 const 라 mutable 입니다 — 새 오브젝트에 id 를 주는 것은 씬의 보이는 상태를 바꾸지 않습니다.
         */
        mutable ObjectSavedIDMap               _mapObjectIDToFileID;
        vector<SceneDocument::SceneObjectNode> _listUnresolvedEntity;    ///< 프리팹을 찾지 못한 엔티티(문서 그대로, 저장 때 다시 써 넣는다)
        uint8                                  _bRegistrationHeld   : 1; ///< `holdRegistration` 이 쥔 미루기가 남았다(`finishHeldRegistration` 이 놓는다)
        uint8                                  _bBuildAllDataLayers : 1; ///< `setBuildAllDataLayers` — 쿠커의 씬
        TComponentHandle<CameraComponent>      _activeGameCamera;        ///< 마지막 `pickActiveGameCamera` 가 고른 카메라(렌더 쪽이 O(1) 로 읽는다)
        TComponentHandle<CameraComponent>      _gameCameraOverride;      ///< `setActiveGameCamera` 로 직접 고른 카메라
    };
} // namespace sw
