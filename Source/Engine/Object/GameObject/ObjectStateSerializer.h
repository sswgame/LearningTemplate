/**
 * @file ObjectStateSerializer.h
 * @brief GameObject 상태를 저장하고 로드합니다. 저작 기본 포맷은 XML 입니다.
 * @details JSON 은 도구 사이 주고받기용입니다. Shipping 쿠킹 결과는 Prefab/Scene 바이너리입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    struct ObjectIdentity;

    class GameObject;
    class ObjectStateBatch;

    /// @brief GameObject/Component 리플렉션 스키마 버전입니다(XmlSerializer 의 `_schemaVersion`).
    inline constexpr uint32 kObjectReflectedSchemaVersion = 0;

    /**
     * @brief 저장된 상태가 다른 오브젝트를 가리킬 때 쓴 id(`SceneComponent::_attachOwnerId`)가 어느 공간의 것인지입니다.
     * @details 같은 값이라도 공간이 다르면 다른 오브젝트입니다. 씬 파일의 id 는 그 파일 안에서만 뜻이 있고, 이 실행의 런타임 id 와 우연히
     *          같을 수 있습니다 — 그 둘을 섞으면 엉뚱한 오브젝트에 붙습니다.
     */
    enum class ObjectIdSpace : uint8
    {
        /// 이 프로세스의 런타임 id 입니다(되돌리기 · 플레이 종료 복원 · 같은 실행의 핫 리로드 · 복제 · 영속 이월). 묶음에 없으면 매니저에서 찾습니다.
        Live,
        /// 파일이 정한 id 입니다(씬 파일 id · 다른 실행의 세이브). 같은 묶음 안에서만 풉니다.
        Saved,
    };

    /** @brief 런타임 오브젝트 id → 저장할 id 표입니다. 씬이 저장할 때 넘겨 부모 참조를 파일 id 로 옮겨 적게 합니다. */
    using ObjectSavedIdMap = unordered_map<uint64, uint64>;

    /**
     * @struct ObjectSaveOptions
     * @brief 오브젝트 상태를 쓸 때 다른 오브젝트를 가리키는 것(부착의 부모 · `GameObjectHandle` PROPERTY)을 어떻게 적을지입니다.
     */
    struct ObjectSaveOptions
    {
        /** @brief 있으면 다른 오브젝트의 런타임 id 를 이 표로 옮겨 적습니다(씬 파일 id). 없으면 런타임 id 그대로입니다(같은 실행의 스냅샷). */
        const ObjectSavedIdMap* _pSavedIdMap{ nullptr };
        /**
         * @brief 다른 오브젝트를 가리키는 것을 적지 않습니다 — 프리팹이 씁니다(프리팹 루트에는 부모가 없고, 핸들 PROPERTY 는 비워 적는다).
         * @details 예전에는 자식 인스턴스로 프리팹을 만들면 옛 부모의 이름이 프리팹에 들어가, 그 프리팹을 스폰할 때마다 그 이름의 오브젝트에 붙었습니다.
         *          핸들도 같다 — 프리팹의 런타임 id 는 스폰한 실행에서 엉뚱한 오브젝트를 가리킨다.
         */
        bool _bOmitExternalParent{ false };

        /**
         * @brief 다른 오브젝트의 런타임 id 를 저장할 id 로 옮깁니다 — 부착(`SceneComponent`)과 핸들 PROPERTY 가 이 하나를 지납니다.
         * @details 표가 있으면 그 오브젝트의 파일 id, 표에 없는 오브젝트(이 파일에 없다)는 0 입니다. 표가 없으면 그대로입니다. 언리얼의 Instigator 처럼
         *          런타임 참조는 파일 밖을 가리키면 저장되지 않는다.
         */
        uint64 getSavedObjectId( uint64 runtimeId ) const
        {
            if ( _pSavedIdMap == nullptr )
                return runtimeId;
            const auto mapIt = _pSavedIdMap->find( runtimeId );
            return ( mapIt != _pSavedIdMap->end() ) ? mapIt->second : 0;
        }
    };

    /**
     * @struct ObjectLoadContext
     * @brief 오브젝트 상태 하나를 읽을 때의 문맥입니다. 기본값은 "같은 실행의 상태 하나를 새 id 로" 입니다.
     */
    struct ObjectLoadContext
    {
        /** @brief 같은 오브젝트를 되살릴 때의 원래 id 입니다. 주면 다시 만드는 컴포넌트가 원래 componentId 를 받습니다. */
        const ObjectIdentity* _pIdentity{ nullptr };
        /** @brief 여러 오브젝트를 함께 읽을 때의 묶음입니다. 주면 부착은 읽는 자리가 아니라 `ObjectStateBatch::finish` 가 한 번에 잇습니다. */
        ObjectStateBatch* _pBatch{ nullptr };
        /** @brief 이 상태가 저장될 때 이 오브젝트의 id 입니다(같은 묶음의 다른 상태가 이 값으로 이 오브젝트를 가리킵니다). 0 이면 `_pIdentity` 의 것입니다. */
        uint64 _savedId{ 0 };
        /** @brief false 면 다른 오브젝트로의 부착을 읽지 않습니다 — 프리팹(루트에 부모가 없다). 오브젝트 안의 부착은 그대로 잇습니다. */
        bool _bExternalParentAllowed{ true };
        /**
         * @brief 읽기에 실패하면 오브젝트를 읽기 전 상태로 되돌립니다(기본). 되돌리는 로드 자신만 끕니다.
         * @details 제자리 로드는 컴포넌트를 먼저 비우므로, 예전에는 읽기가 실패하면 **빈 오브젝트**가 남았다 — 되돌리기 · 복제 · 프리팹 되돌리기가
         *          결과를 버려 그대로 저장되었다.
         */
        bool _bRestorePreviousOnFailure{ true };
    };

    /**
     * @struct ObjectIdentity
     * @brief 오브젝트 하나와 그 컴포넌트들의 런타임 ID 입니다. 같은 오브젝트를 되살릴 때 핸들이 끊기지 않게 상태와 함께 적어 둡니다.
     * @details 상태(XML · JSON · 바이너리)에는 오브젝트 **자신의** ID 가 들어가지 않습니다(부착이 가리키는 부모의 id 는 들어갑니다 —
     *          `ObjectStateBatch`). 씬 · 프리팹을 읽거나 오브젝트를 복제할 때는 늘 새 ID 를
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
     * @class ObjectStateBatch
     * @brief 여러 오브젝트의 상태를 **모두 읽은 뒤** 그 사이의 부착(부모 · 소켓)을 한 번에 잇는 묶음입니다.
     * @details 상태를 읽는 길(씬 로드 · 쿠커 · 플레이 종료 복원 · 핫 리로드 · 세이브 · 영속 이월 · 복제 · 되돌리기)이 모두 이것을 지납니다 —
     *          상태 하나만 읽는 로드도 한 개짜리 묶음입니다. 규칙은 하나입니다:
     *          - 자기 오브젝트 안의 부착은 소유자 칸이 비어 있습니다(옛 데이터는 자기 이름 — 읽기 전 이름으로 봅니다).
     *          - 다른 오브젝트는 **id** 로 찾습니다. 먼저 이 묶음의 저장된 id, 그다음(`ObjectIdSpace::Live` 일 때만) 매니저의 런타임 id 입니다.
     *          - id 가 없는 옛 데이터만 이름으로 찾되 **이 묶음의 저장된 이름**에서만 찾습니다. 매니저는 이름을 유일하게 바꾸므로(`X` → `X_2`)
     *            매니저에서 이름으로 찾으면 같은 이름의 다른 오브젝트(원본 · 자동으로 만든 카메라 · 들어오는 씬의 같은 이름)에 붙었습니다.
     *          - 찾지 못한 참조는 지우지 않고 그대로 둡니다(`SceneComponent::keepUnresolvedAttach`). 예전에는 다음 저장이 살아 있는 부모
     *            포인터(null)에서 필드를 다시 만들어 연결을 지웠습니다 — 쿠커는 자식이 부모보다 앞에 있는 씬의 연결을 모두 잃었습니다.
     *          언리얼은 레벨 안 오브젝트를 경로로, 유니티는 파일 안 fileID 로 가리킵니다 — 이 묶음의 저장된 id 가 그 자리입니다.
     *
     *          컴포넌트의 `GameObjectHandle` PROPERTY(단일 · 순서 컨테이너의 원소)도 같은 규칙으로 옮깁니다(`resolveObjectReference`): 묶음의 저장된
     *          id 면 그 오브젝트, `Live` 면 런타임 id 그대로(핫 리로드 · 되돌리기 — id 는 다시 쓰이지 않는다), `Saved` 인데 묶음에 없으면 없음입니다.
     *          옮기지 못하는 자리(`ComponentHandle`, 맵 · set 의 키 · 값, 중첩 컨테이너의 원소)는 `Saved` 면 비우고 경고합니다 — 파일 id 를 그대로 두면
     *          이 실행에서 같은 값을 받은 다른 오브젝트를 가리킵니다. `Live` 면 런타임 id 그대로 둡니다.
     */
    class SW_API ObjectStateBatch
    {
    public:
        explicit ObjectStateBatch( ObjectIdSpace idSpace );
        /** @brief 더했으면 `finish` 를 불렀어야 합니다(Debug 단언). */
        ~ObjectStateBatch();

        ObjectStateBatch( const ObjectStateBatch& )            = delete;
        ObjectStateBatch& operator=( const ObjectStateBatch& ) = delete;

        /**
         * @brief 상태를 읽은(또는 프리팹으로 지은) 오브젝트 하나를 적습니다. 로드가 부르고, 상태 없이 지은 오브젝트는 부른 쪽이 직접 적습니다.
         * @param savedId 같은 묶음의 다른 상태가 이 오브젝트를 가리킬 때 쓰는 id 입니다. 0 이면 id 로는 찾을 수 없습니다(옛 데이터).
         * @param savedName 상태에 적힌 이름입니다 — 매니저가 유일하게 바꾸기 **전**의 것입니다.
         */
        void add( GameObject* pObject, uint64 savedId, hashed_string savedName, bool bExternalParentAllowed = true );
        /**
         * @brief 상태를 읽은 오브젝트 하나를 적습니다(`add` 와 같고, `finish` 가 이 오브젝트의 컴포넌트에 `onPostLoad` 를 부릅니다). 로더가 부릅니다.
         * @details `onPostLoad` 는 부착 · 핸들 PROPERTY 가 풀린 **뒤에** 옵니다 — 읽는 자리에서 부르면 다른 오브젝트를 가리키는 칸이 아직 저장된 id 입니다.
         */
        void addLoadedState( GameObject* pObject, uint64 savedId, hashed_string savedName, bool bExternalParentAllowed = true );

        /**
         * @brief 적은 오브젝트들의 부착 · 핸들 PROPERTY 를 잇고, 상태를 읽은 오브젝트의 컴포넌트에 `onPostLoad` 를 부릅니다. 모두 읽은 뒤 한 번 부릅니다.
         * @details 그 앞에, 읽는 동안 이름이 겹쳐 번호를 받은 오브젝트는 그 이름이 비었으면 저장된 이름으로 되돌립니다(플레이 중 이름을 바꾼
         *          오브젝트를 되돌릴 때 앞의 것이 뒤의 것 이름을 잠시 쥐고 있었다).
         */
        void finish();

        /** @brief 저장된 id 로 이 묶음의 오브젝트를 찾습니다. 없으면 nullptr 입니다. */
        GameObject* findBySavedId( uint64 savedId ) const;
        /** @brief 저장된 이름으로 이 묶음의 오브젝트를 찾습니다(옛 데이터용). 같은 이름이 여럿이면 먼저 적힌 것입니다. */
        GameObject* findBySavedName( hashed_string savedName ) const;

        ObjectIdSpace getIdSpace() const { return _idSpace; }

    private:
        struct Entry
        {
            GameObject*   _pObject{ nullptr };
            uint64        _savedId{ 0 };
            hashed_string _savedName{};
            bool          _bExternalParentAllowed{ true };
            bool          _bLoadedState{ false }; ///< 상태를 읽었다 — `finish` 가 `onPostLoad` 를 부른다
        };

        /** @brief `add` · `addLoadedState` 의 몸통입니다. */
        void addEntry( GameObject* pObject, uint64 savedId, hashed_string savedName, bool bExternalParentAllowed, bool bLoadedState );

        /** @brief 항목 하나의 씬 컴포넌트마다 부모를 찾아 붙이고, 못 찾으면 참조를 남깁니다. */
        void resolveEntry( const Entry& entry ) const;
        /** @brief 부착 참조의 소유자 칸이 가리키는 오브젝트입니다. 자기면 항목의 오브젝트, 못 찾으면 nullptr 입니다. */
        GameObject* findAttachOwner( const Entry& entry, hashed_string ownerName, uint64 ownerId ) const;
        /** @brief 항목 하나의 컴포넌트마다 `GameObjectHandle` PROPERTY 를 이 실행의 오브젝트로 옮깁니다(`Transient` 는 읽지 않았으니 건드리지 않는다). */
        void resolveObjectReferences( const Entry& entry ) const;
        /** @brief 저장된 핸들 하나가 이 실행에서 가리키는 오브젝트입니다(클래스 설명의 규칙). */
        GameObjectHandle resolveObjectReference( GameObjectHandle savedHandle ) const;

        vector<Entry>                        _listEntry;
        unordered_map<uint64, GameObject*>   _mapSavedIdToObject;
        unordered_map<hashed_string, uint32> _mapSavedNameToEntry;
        ObjectIdSpace                        _idSpace;
        bool                                 _bFinished;
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
        static string saveToXmlString( const GameObject* pGameObject, const ObjectSaveOptions& options = {} );
        /** @brief GameObject 상태를 JSON 으로 직렬화합니다. XmlSerializer 와 같은 PROPERTY 그래프입니다. */
        [[maybe_unused]] static string saveToJsonString( const GameObject* pGameObject, const ObjectSaveOptions& options = {} );

        /** @brief GameObject 상태를 바이너리 버퍼로 빠르게 직렬화합니다(핫 리로드 · 프리팹용). */
        [[nodiscard]] static bool saveToBinaryBuffer( const GameObject* pGameObject, vector<uint8>& outBuffer, const ObjectSaveOptions& options = {} );

        /**
         * @brief XML 문자열에서 GameObject 상태를 복원합니다(ObjectId 제외).
         * @param context 원래 id 를 되살릴지(`_pIdentity`), 여러 오브젝트를 함께 읽는 묶음인지(`_pBatch`)입니다. 기본값은 새 id 의 단독 로드입니다.
         * @details 적용하기 전에 기존 컴포넌트를 비웁니다. 묶음 없이 읽으면 부착은 이 오브젝트 하나짜리 묶음으로 바로 잇습니다(다른 오브젝트는
         *          매니저의 런타임 id 로 찾습니다). 여러 오브젝트를 읽을 때는 묶음을 주고 모두 읽은 뒤 `ObjectStateBatch::finish` 를 부릅니다 —
         *          그래야 자식이 부모보다 먼저 읽혀도 부모를 찾습니다.
         */
        [[nodiscard]] static bool loadFromXmlString( GameObject* pGameObject, string_view xmlString, const ObjectLoadContext& context = {} );
        [[nodiscard]] static bool loadFromJsonString( GameObject* pGameObject, string_view jsonString, const ObjectLoadContext& context = {} );

        /**
         * @brief 바이너리 버퍼에서 GameObject 상태를 복원하고 읽은 바이트 수를 반환합니다(실패하면 0).
         * @param context `loadFromXmlString` 과 같습니다.
         * @details 버퍼는 본문 크기(uint32) + 본문입니다. 부모는 상태의 부착 필드가 id 로 듭니다.
         */
        [[nodiscard]] static size_t loadFromBinaryBuffer( GameObject* pGameObject, const uint8* pData, size_t size, const ObjectLoadContext& context = {} );

        // ------------------------------------------------------------------------------
        // 2) 런타임 ID: 같은 오브젝트를 되살릴 때 핸들이 이어지게 한다(`ObjectIdentity`)
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
        static string saveToText( const GameObject* pGameObject, const ObjectSaveOptions& options );
        /** @brief 리플렉션 문자열 포맷 하나에서 복원합니다. `loadFromXmlString` · `loadFromJsonString` 의 몸통입니다. */
        template <typename TSerializer>
        [[nodiscard]] static bool loadFromText( GameObject* pGameObject, string_view text, const ObjectLoadContext& context );
        /**
         * @brief 오브젝트의 상태를 제자리에서 다시 읽습니다. 세 로더(XML · JSON · 바이너리)는 포맷 읽기만 `deserializeState( version, ctx )` 로 넘깁니다.
         * @details 자식 연결을 적어 두고 컴포넌트를 비운 뒤, 컴포넌트 ID 를 되살리는 범위 안에서 읽습니다. 읽으면 묶음에 적고(묶음이 없으면 한 개짜리
         *          묶음으로 바로 잇습니다), 읽기에 실패해도 적어 둔 자식은 되붙입니다.
         *          정의는 .cpp 에만 있습니다(ID 범위가 `GameObject` 의 비공개 타입이라 이 클래스의 멤버여야 합니다).
         */
        template <typename DeserializeStateFunc>
        [[nodiscard]] static bool loadStateInPlace( GameObject* pGameObject, const ObjectLoadContext& context, DeserializeStateFunc&& deserializeState );
    };
} // namespace sw
