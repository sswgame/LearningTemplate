/**
 * @file ObjectStateSerializer.h
 * @brief GameObject 상태를 저장하고 로드합니다. 저작 기본 포맷은 XML 입니다.
 * @details JSON 은 도구 사이 주고받기용입니다. Shipping 쿠킹 결과는 Prefab/Scene 바이너리입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class GameObject;

    /// @brief GameObject/Component 리플렉션 스키마 버전입니다(XmlSerializer 의 `_schemaVersion`).
    inline constexpr uint32 kObjectReflectedSchemaVersion = 0;

    /**
     * @struct ObjectIdentity
     * @brief 오브젝트 하나와 그 컴포넌트들의 런타임 ID 입니다. 같은 오브젝트를 되살릴 때 핸들이 끊기지 않게 상태와 함께 적어 둡니다.
     * @details 상태(XML · JSON · 바이너리)에는 ID 가 들어가지 않습니다. 씬 · 프리팹을 읽거나 오브젝트를 복제할 때는 늘 새 ID 를
     *          받아야 하기 때문입니다(같은 프리팹을 두 번 놓으면 ID 가 겹칩니다). 에디터 되돌리기 · 플레이 세션 복원 · 핫 리로드처럼
     *          **같은 프로세스에서 같은 오브젝트를 되살리는** 경우에만 이것을 로드 함수에 넘겨 원래 ID 를 되살립니다.
     *          objectId 는 오브젝트를 만들 때 `GameObjectManager::createGameObjectWithId` 가, componentId 는 로드가 되살립니다.
     */
    struct ObjectIdentity
    {
        /** @brief 컴포넌트 하나의 타입 이름과 ID 입니다. 오브젝트의 컴포넌트 목록 순서로 적습니다. */
        struct ComponentEntry
        {
            hashed_string _typeName;
            uint64        _componentId{ 0 };
        };

        uint64                 _objectId{ 0 };
        vector<ComponentEntry> _listComponent;
    };

    /**
     * @class ObjectStateSerializer
     * @brief GameObject 상태를 XML · JSON · 바이너리로 저장하고 로드합니다.
     */
    class SW_API ObjectStateSerializer
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 문자열: PROPERTY 기반 GameObject XML. 부모는 SceneComponent `_pParent`.
        // ------------------------------------------------------------------------------
        /**
         * @brief GameObject 상태를 XML 문자열로 직렬화합니다.
         * @details 루트는 TypeInfo 이름 `GameObject` 입니다. 스칼라는 속성, `_listComponent` 는
         *          `<_listComponent>` 아래 런타임 타입 노드입니다. 로컬 TRS 와 Attach 는 SceneComponent PROPERTY 입니다.
         *          GameObject 부모는 `getParent()` 가 SceneComponent `_pParent` 에서 끌어내므로
         *          별도 `_parentGO` 필드/속성을 쓰지 않습니다.
         */
        static string saveToXmlString( const GameObject* pGameObject );
        /** @brief GameObject 상태를 JSON 으로 직렬화합니다. XmlSerializer 와 같은 PROPERTY 그래프입니다. */
        [[maybe_unused]] static string saveToJsonString( const GameObject* pGameObject );

        /** @brief GameObject 상태를 바이너리 버퍼로 빠르게 직렬화합니다(핫 리로드 · 프리팹용). */
        static bool saveToBinaryBuffer( const GameObject* pGameObject, vector<uint8>& outBuffer );

        /**
         * @brief XML 문자열에서 GameObject 상태를 복원합니다(ObjectId 제외).
         * @param pIdentity 같은 오브젝트를 되살릴 때의 원래 ID 입니다. 주면 다시 만드는 컴포넌트가 원래 componentId 를 받습니다.
         *                  nullptr 이면 새 ID 입니다(씬 · 프리팹 로드와 복제).
         * @details 적용하기 전에 기존 컴포넌트를 비웁니다. 부모 GameObject 가 아직 없으면
         *          SceneComponent Attach 는 실패할 수 있으므로 나중에 rebindSceneHierarchy 로 확정합니다.
         */
        static bool                  loadFromXmlString( GameObject* pGameObject, string_view xmlString, const ObjectIdentity* pIdentity = nullptr );
        [[maybe_unused]] static bool loadFromJsonString( GameObject* pGameObject, string_view jsonString, const ObjectIdentity* pIdentity = nullptr );

        /**
         * @brief 바이너리 버퍼에서 GameObject 상태를 복원하고 읽은 바이트 수를 반환합니다(실패하면 0).
         * @param pIdentity `loadFromXmlString` 과 같습니다. nullptr 이면 컴포넌트가 새 ID 를 받습니다.
         */
        static size_t loadFromBinaryBuffer( GameObject* pGameObject, const uint8* pData, size_t size, string& outParentName,
                                            const ObjectIdentity* pIdentity = nullptr );

        /**
         * @brief SceneComponent Attach 필드로 계층을 다시 해석합니다.
         * @details 여러 GameObject 를 복원한 뒤 부릅니다(모든 GameObject 가 있다는 전제).
         */
        static bool                  rebindSceneHierarchy( GameObject* pGameObject, string_view xmlString );
        [[maybe_unused]] static bool rebindSceneHierarchyFromJson( GameObject* pGameObject, string_view jsonString );

        // ------------------------------------------------------------------------------
        // 2) 파일
        // ------------------------------------------------------------------------------
        /** @brief GameObject 상태를 XML 파일로 저장합니다. */
        static bool saveToXmlFile( const GameObject* pGameObject, string_view filePath );

        /** @brief XML 파일에서 GameObject 상태를 로드합니다. */
        static bool loadFromXmlFile( GameObject* pGameObject, string_view filePath );

        // ------------------------------------------------------------------------------
        // 3) 런타임 ID: 같은 오브젝트를 되살릴 때 핸들이 이어지게 한다(`ObjectIdentity`)
        // ------------------------------------------------------------------------------
        /** @brief 오브젝트와 컴포넌트들의 지금 ID 를 적습니다. 삭제 대기 컴포넌트는 뺍니다. */
        static ObjectIdentity captureIdentity( const GameObject* pGameObject );

        /** @brief ID 목록을 바이너리로 덧붙입니다(스냅샷 봉투에 싣는 용도). */
        static void writeIdentity( const ObjectIdentity& identity, vector<uint8>& outBuffer );

        /** @brief `writeIdentity` 로 쓴 것을 읽고, 읽은 바이트 수를 반환합니다. 실패하면 0 입니다. */
        static size_t readIdentity( const uint8* pData, size_t size, ObjectIdentity& outIdentity );

        /**
         * @brief 이 프로세스에서 찍은 스냅샷인지 가리는 값입니다. 프로세스마다 처음 부를 때 한 번 무작위로 정해지고, 0 은 아닙니다.
         * @details 핫 리로드 스냅샷과 세이브 파일은 형식이 같습니다. 다른 실행에서 만든 세이브 파일의 id 를 되살리면 이 실행에서
         *          이미 나간 id 와 겹칠 수 있으므로, 스냅샷에 이 값을 적어 두고 같을 때만 id 를 되살립니다. Engine 에 있어서
         *          게임 · 게임프레임워크 DLL 을 다시 올려도 바뀌지 않습니다.
         */
        static uint64 getProcessToken();

    private:
        /**
         * @brief 리플렉션 문자열 포맷(XML · JSON) 하나로 저장합니다. 두 포맷은 직렬화기만 다르고 걸음이 같습니다.
         * @details 정의는 .cpp 에만 있습니다(그곳에서만 실체화합니다). 헤더가 직렬화기를 알 필요가 없습니다.
         */
        template <typename TSerializer>
        static string saveToText( const GameObject* pGameObject );
        /** @brief 리플렉션 문자열 포맷 하나에서 복원합니다. `loadFromXmlString` · `loadFromJsonString` 의 몸통입니다. */
        template <typename TSerializer>
        static bool loadFromText( GameObject* pGameObject, string_view text, const ObjectIdentity* pIdentity );
    };
} // namespace sw
