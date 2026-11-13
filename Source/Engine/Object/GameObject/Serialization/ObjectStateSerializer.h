/**
 * @file ObjectStateSerializer.h
 * @brief GameObject 상태를 저장하고 로드합니다. 저작용 기본 포맷은 XML 입니다.
 * @details JSON 은 도구 사이에서 주고받는 용도입니다. Shipping 쿠킹 결과는 Prefab 과 Scene 바이너리입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Serialization/Serializer/SerializePurpose.h"

namespace sw
{
    struct ObjectIdentity;
    struct PropertyInfo;

    class GameObject;
    class ObjectStateBatch;
    class TypeRegistry;

    /// @brief GameObject 와 Component 리플렉션 스키마 버전입니다(XMLSerializer 의 `_schemaVersion`).
    inline constexpr uint32 kObjectReflectedSchemaVersion = 0;

    /**
     * @brief 저장된 상태가 다른 오브젝트를 가리킬 때 쓴 id(`SceneComponent::_attachOwnerID`)가 어느 공간의 것인지 나타냅니다.
     * @details 같은 값이라도 공간이 다르면 다른 오브젝트다. 씬 파일의 id 는 그 파일 안에서만 의미가 있고, 이 실행의 런타임 id 와 우연히
     *          같을 수 있다. 그 둘을 섞으면 엉뚱한 오브젝트에 붙는다.
     */
    enum class ObjectIDSpace : uint8
    {
        /// 이 프로세스의 런타임 id 입니다. 되돌리기, 플레이 종료 복원, 같은 실행의 핫 리로드, 복제, 영속 이월에 씁니다. 배치에 없으면 매니저에서 찾습니다.
        Live,
        /// 파일이 정한 id 입니다(씬 파일 id, 다른 실행의 세이브). 같은 배치 안에서만 찾습니다.
        Saved,
    };

    /** @brief 런타임 오브젝트 id 에서 저장할 id 로 가는 테이블입니다. 씬이 저장할 때 넘겨 부모 참조를 파일 id 로 바꿔 적게 합니다. */
    using ObjectSavedIDMap = unordered_map<uint64, uint64>;

    /**
     * @struct ObjectSaveOptions
     * @brief 오브젝트 상태를 쓰는 목적(`SerializePurpose`, 필수)과, 다른 오브젝트를 가리키는 참조(부착의 부모, `GameObjectHandle` PROPERTY)를 어떻게 적을지 정합니다.
     * @details 목적은 만들 때만 정한다(기본 생성자가 없다). 저작(씬 · 프리팹 · 쿠킹 · 에디터의 복사 · 복제 · 되돌리기)은 `RuntimeState` 필드를 쓰지 않고,
     *          스냅샷(플레이 세션 복원 · 핫 리로드 · 씬 넘김)은 모두 쓰며, 세이브는 옵트인 타입의 `SaveGame` 필드만 쓴다(`SerializerUtil::shouldSerializeProperty`).
     */
    struct ObjectSaveOptions
    {

        /** @brief 있으면 다른 오브젝트의 런타임 id 를 이 테이블로 바꿔 적습니다(씬 파일 id). 없으면 런타임 id 를 그대로 적습니다(같은 실행의 스냅샷). */
        const ObjectSavedIDMap* _pSavedIDMap{ nullptr };
        /**
         * @brief 다른 오브젝트를 가리키는 참조를 적지 않습니다. 프리팹이 씁니다(프리팹 루트에는 부모가 없고, 핸들 PROPERTY 는 비워서 적습니다).
         * @details 이 값이 없으면 자식 인스턴스로 프리팹을 만들 때 원래 부모의 이름이 프리팹에 들어가, 그 프리팹을 스폰할 때마다 그 이름의 오브젝트에
         *          붙는다. 핸들도 같다. 프리팹의 런타임 id 는 스폰한 실행에서 엉뚱한 오브젝트를 가리킨다.
         */
        uint8 _bOmitExternalParent : 1;
        /**
         * @brief 텍스트 저장 전 검증(`ObjectValidation::reportGameObject`)을 건너뜁니다. 기본값 견본(`PrefabOverrides::omitDefaultValues` 가 새로 만든 오브젝트)이 씁니다.
         * @details 견본은 데이터가 아니라 형식의 초기값이라, 빈 필수 이름 칸 같은 검증 경고가 저장마다 거짓으로 남는다.
         */
        uint8 _bSkipValidation : 1;
        /**
         * @brief 있으면 이 저장이 가리킨 다른 오브젝트의 런타임 id(0 제외)를 모두 더합니다. 쿠커가 셀을 넘는 참조를 찾는 데 씁니다(`SceneCooker`).
         * @details 참조는 모두 `getSavedObjectID` 를 거치므로 여기서 모으면 부착 · 핸들 PROPERTY 를 타입마다 따로 훑지 않아도 된다.
         */
        vector<uint64>* _pOutListReferencedID{ nullptr };
        /**
         * @brief 0 이 아니면 이 런타임 id 의 오브젝트로의 부착을 적지 않습니다. 씬 인스턴스 앵커가 그 멤버 루트에 쓴다 — 앵커에 붙는 것은 인스턴스가 정하는 자리라 내부 씬의 상태에 없고, 덮어쓴 것으로 적으면 안 된다.
         * @details `_bOmitExternalParent` 와 달리 다른 부착과 핸들은 그대로 적는다.
         */
        uint64 _omitAttachToObjectID{ 0 };
        /** @brief 비어 있지 않으면 부착이 적는 부모 이름에서 이 앞머리를 뗍니다. 인스턴스 멤버의 이름은 앵커 이름이 붙은 것이라(`HouseLeft/Box`) 내부 씬의 상태(`Box`)와 비교하려면 떼어야 한다. */
        string_view _attachOwnerNamePrefix{};
        /** @brief 목적 @p purpose 로 저장합니다. 참조는 런타임 id 그대로 적습니다(아래 필드로 바꾼다). */
        explicit ObjectSaveOptions( SerializePurpose purpose )
            : _bOmitExternalParent{ SW_FALSE }, _bSkipValidation{ SW_FALSE }, _purpose{ purpose }
        {
        }

        SerializePurpose getPurpose() const { return _purpose; }

        /**
         * @brief 다른 오브젝트의 런타임 id 를 저장할 id 로 바꿉니다. 부착(`SceneComponent`)과 핸들 PROPERTY 가 모두 이 함수를 거칩니다.
         * @details 테이블이 있으면 그 오브젝트의 파일 id 를 반환하고, 테이블에 없는 오브젝트(이 파일에 없음)는 0 을 반환한다. 테이블이 없으면 그대로 반환한다.
         *          언리얼의 Instigator 처럼 런타임 참조는 파일 밖을 가리키면 저장되지 않는다.
         */
        uint64 getSavedObjectID( uint64 runtimeID ) const
        {
            if ( _pOutListReferencedID != nullptr && runtimeID != 0 )
                _pOutListReferencedID->push_back( runtimeID );
            if ( _pSavedIDMap == nullptr )
                return runtimeID;
            const auto mapIt = _pSavedIDMap->find( runtimeID );
            return ( mapIt != _pSavedIDMap->end() ) ? mapIt->second : 0;
        }

    private:
        SerializePurpose _purpose;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ObjectLoadContext
     * @brief 오브젝트 상태 하나를 읽을 때의 문맥입니다. 목적(`SerializePurpose`)은 필수이고, 나머지 기본값은 "상태 하나를 새 id 로 읽기"입니다.
     * @details 저작 목적으로 읽는 상태에 `RuntimeState` 필드가 있으면 로드 오류다(저작 파일에 런타임 상태가 섞인 것이다). 목적이 쓰지 않는 필드는 지금 값 그대로다.
     */
    struct ObjectLoadContext
    {

        /** @brief 같은 오브젝트를 복원할 때의 원래 id 입니다. 주면 다시 만드는 컴포넌트가 원래 componentID 를 받습니다. */
        const ObjectIdentity* _pIdentity{ nullptr };
        /** @brief 여러 오브젝트를 함께 읽을 때의 배치입니다. 주면 부착은 읽는 시점이 아니라 `ObjectStateBatch::finish` 가 한 번에 연결합니다. */
        ObjectStateBatch* _pBatch{ nullptr };
        /** @brief 이 상태가 저장될 때 이 오브젝트의 id 입니다. 같은 배치의 다른 상태가 이 값으로 이 오브젝트를 가리킵니다. 0 이면 `_pIdentity` 의 것을 씁니다. */
        uint64 _savedID{ 0 };
        /**
         * @brief 상태 안의 참조 id 를 배치의 id 로 옮길 때 XOR 하는 값입니다. 0 이면 그대로입니다. 씬 인스턴스의 글 상태가 씁니다(`SceneObjectNode::_idScope`).
         * @details 상태는 내부 씬의 파일 id 로 적혀 있고 배치는 인스턴스마다 겹치지 않는 평평한 id 를 쓴다. XOR 라서 범위 밖(바깥 씬)의 대상도 같은 식으로 왕복한다(유니티 스트립 오브젝트의 fileID 도 XOR 이다 — 기억에 의존).
         */
        uint64 _savedIDScope{ 0 };
        /** @brief false 이면 다른 오브젝트로의 부착을 읽지 않습니다. 프리팹은 루트에 부모가 없으므로 false 입니다. 오브젝트 안의 부착은 그대로 연결합니다. */
        bool _bExternalParentAllowed{ true }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        /**
         * @brief 읽기에 실패하면 오브젝트를 읽기 전 상태로 되돌립니다(기본). 되돌리기 로드만 끕니다.
         * @details 제자리 로드는 컴포넌트를 먼저 비우므로, 되돌리지 않으면 읽기가 실패할 때 **빈 오브젝트**가 남는다. 되돌리기, 복제, 프리팹
         *          되돌리기는 결과를 버리므로 그 빈 오브젝트가 그대로 저장된다.
         */
        uint8 _bRestorePreviousOnFailure : 1;
        /**
         * @brief 목적 @p purpose 로 읽습니다. 나머지는 선택입니다 — @p pIdentity 를 주면 원래 id 를 복원하고(같은 오브젝트를 같은 실행에서 복원할 때),
         *        @p pBatch 를 주면 그 배치에 기록하며, @p savedID 는 이 상태가 저장될 때의 id 입니다.
         */
        explicit ObjectLoadContext( SerializePurpose purpose, const ObjectIdentity* pIdentity = nullptr, ObjectStateBatch* pBatch = nullptr, uint64 savedID = 0 )
            : _pIdentity{ pIdentity }
            , _pBatch{ pBatch }
            , _savedID{ savedID }
            , _savedIDScope{ 0 }
            , _bRestorePreviousOnFailure{ SW_TRUE }
            , _purpose{ purpose }
        {
        }

        SerializePurpose getPurpose() const { return _purpose; }

    private:
        SerializePurpose _purpose;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ObjectIdentity
     * @brief 오브젝트 하나와 그 컴포넌트들의 런타임 ID 입니다. 같은 오브젝트를 복원할 때 핸들이 끊기지 않게 상태와 함께 적어 둡니다.
     * @details 상태(XML, JSON, 바이너리)에는 오브젝트 **자신의** ID 가 들어가지 않는다. 부착이 가리키는 부모의 id 는 들어간다(`ObjectStateBatch` 참고).
     *          씬이나 프리팹을 읽거나 오브젝트를 복제할 때는 늘 새 ID 를 받아야 하기 때문이다(같은 프리팹을 두 번 놓으면 ID 가 겹친다).
     *          에디터 되돌리기, 플레이 세션 복원, 핫 리로드처럼 **같은 프로세스에서 같은 오브젝트를 복원하는** 경우에만
     *          이것을 로드 함수에 넘겨 원래 ID 를 복원한다.
     *          objectID 는 오브젝트를 만들 때 `GameObjectManager::createGameObjectWithID` 가, componentID 는 로드가 복원한다.
     */
    struct ObjectIdentity
    {
        /**
         * @brief 컴포넌트 하나의 타입 FQN 과 ID 입니다. 오브젝트의 컴포넌트 목록 순서대로 적습니다.
         * @details **짧은 이름이 아니라 FQN 이다.** 복원(`takeRestoredComponentID`)이 이 값으로 원래 id 를 돌려준다. 짧은 이름이면 이름이 같은
         *          다른 네임스페이스의 타입이 그 id 를 가져가, 그 id 를 가리키던 타입 핸들(`TComponentHandle<T>`)이 다른 타입의 칸을 가리킨다.
         *          같은 프로세스에서만 읽으므로(프로세스 토큰) 이전 실행이 적은 짧은 이름과의 호환은 필요 없다.
         */
        struct ComponentEntry
        {
            hashed_string _typeFqn;
            uint64        _componentID{ 0 };
            string        _stableKey{}; ///< 오브젝트 안의 안정 키(`ComponentStableKey`, `이름#n`)다. 다른 실행의 세이브가 컴포넌트 상태를 다시 만든 컴포넌트에 잇는다
        };

        uint64                 _objectID{ 0 };
        vector<ComponentEntry> _listComponent;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ObjectStateBatch
     * @brief 여러 오브젝트의 상태를 **모두 읽은 뒤** 그 사이의 부착(부모, 소켓)을 한 번에 연결하는 배치입니다.
     * @details 상태를 읽는 모든 경로(씬 로드, 쿠커, 플레이 종료 복원, 핫 리로드, 세이브, 영속 이월, 복제, 되돌리기)가 이 배치를 거친다.
     *          상태 하나만 읽는 로드도 한 개짜리 배치다. 규칙은 다음과 같다.
     *          - 자기 오브젝트 안의 부착은 소유자 필드가 비어 있다.
     *          - 다른 오브젝트는 **id** 로 찾는다. 먼저 이 배치의 저장된 id 로, 그다음(`ObjectIDSpace::Live` 일 때만) 매니저의 런타임 id 로 찾는다.
     *          - id 가 빈 참조(찾지 못한 참조를 다른 id 공간으로 옮겨 적은 것)만 이름으로 찾되 **이 배치의 저장된 이름**에서만 찾는다. 매니저는 이름을 유일하게 바꾸므로(`X` → `X_2`)
     *            매니저에서 이름으로 찾으면 같은 이름의 다른 오브젝트(원본, 자동으로 만든 카메라, 들어오는 씬의 같은 이름)에 붙는다.
     *          - 찾지 못한 참조는 지우지 않고 그대로 둔다(`SceneComponent::keepUnresolvedAttach`). 다음 저장이 살아 있는 부모 포인터(null)에서
     *            필드를 다시 만들면 연결이 지워지기 때문이다. 쿠커는 자식이 부모보다 앞에 있는 씬의 연결을 모두 잃는다.
     *          언리얼은 레벨 안 오브젝트를 경로로, 유니티는 파일 안 fileID 로 가리킨다. 이 배치의 저장된 id 가 그에 해당한다.
     *
     *          컴포넌트의 `GameObjectHandle` PROPERTY 도 같은 규칙으로 바꾼다(`resolveObjectReference`). 어느 깊이에 있든(구조체 필드, 컨테이너 원소 · 키 · 값, 중첩) 바꾼다 —
     *          쓰기 처리기가 모든 깊이에서 저장 id 로 적으므로 읽기도 같은 깊이를 본다(`ReflectedValueVisitor`). 상태를 읽은 항목만, 그 읽기의 목적이 다룬 필드만 바꾼다.
     *          배치의 저장된 id 면 그 오브젝트로, `Live` 면 런타임 id 를 그대로(핫 리로드와 되돌리기에서는 id 가 다시 쓰이지 않는다), `Saved` 인데 배치에 없으면 없음으로 처리한다.
     *          컨테이너(시퀀스, set, 맵의 키와 값, 중첩)의 핸들도 옮긴다. 옮기지 못하는 모양(원소가 컨테이너인 set)이나 옮기다 실패한 것은 `Saved` 면
     *          **로드 오류**다(`finish` 가 false). 파일 id 를 그대로 두면 이 실행에서 같은 값을 받은 다른 오브젝트를 가리키고, 비우면 값이 조용히 사라진다.
     *          `Live` 면 런타임 id 가 맞으므로 그대로 둔다.
     *
     *          타입 핸들(`TComponentHandle<T>`) PROPERTY 도 옮긴다(값 하나, 또는 시퀀스 원소 — 파서가 그 밖의 모양을 막는다). 파일 형태는 `{대상 오브젝트의 저장 id, 컴포넌트 안정 키}` 이고
     *          오브젝트 자리가 0 이면 이 오브젝트 자신이다. 읽은 값의 컴포넌트 자리는 키의 해시이며, 대상 오브젝트의 컴포넌트 키 해시와 맞춰 찾는다.
     *          오브젝트를 찾지 못했거나 그 키의 컴포넌트가 없으면 핸들은 비운다(약한 참조라서 사라진 대상은 정상 값이다). 다만 대상 오브젝트에 모르는 타입의 자리
     *          (`MissingComponent`)가 있으면 그것이 대상일 수 있어 키 해시 그대로 남기고, 키 글은 그 자리가 맡아 다음 저장이 같은 글로 다시 쓴다(부착의 `keepUnresolvedAttach` 와 같은 규칙). **대상이 있는데 대상 타입이 아니면 로드 오류**다(`finish` 가 false).
     *          `Live` 면 런타임 id 를 그대로 두되 같은 타입 확인을 한다. 타입 없는 `ComponentHandle` 은 PROPERTY 에 둘 수 없다(리플렉션 파서 생성 오류).
     */
    class SW_API ObjectStateBatch
    {
    public:
        /** @brief 읽은 컴포넌트 핸들의 키 글(`타입#n`)을 그 해시(`componentID` 자리 값)로 찾는 표입니다. */
        using ComponentKeyTextMap = unordered_map<uint64, string>;

        explicit ObjectStateBatch( ObjectIDSpace idSpace );
        /** @brief 오브젝트를 추가했다면 `finish` 를 불렀어야 합니다(Debug 단언). */
        ~ObjectStateBatch();

        ObjectStateBatch( const ObjectStateBatch& )            = delete;
        ObjectStateBatch& operator=( const ObjectStateBatch& ) = delete;

        /**
         * @brief 이 배치가 상태를 읽지 않은 오브젝트를 가리킴 대상으로만 기록합니다(상태 없는 엔티티, 셀 · 레이어 묶음이 가리키는 이미 올라 있는 오브젝트).
         * @details `finish` 는 이름만 되찾고 부착 · 핸들은 다시 풀지 않는다 — 그 연결은 그 오브젝트를 읽은 배치가 이미 풀었다(다시 풀면 씬 인스턴스 멤버의 부모를 잃는다).
         * @param savedID 같은 배치의 다른 상태가 이 오브젝트를 가리킬 때 쓰는 id 입니다. 0 이면 id 로는 찾을 수 없습니다(id 가 없는 데이터).
         * @param savedName 상태에 적힌 이름입니다. 매니저가 유일하게 바꾸기 **전**의 이름입니다.
         */
        void addReferenceTarget( GameObject* pObject, uint64 savedID, hashed_string savedName );
        /**
         * @brief 상태를 읽은 오브젝트 하나를 기록합니다. `finish` 가 부착 · 핸들을 잇고 이 오브젝트의 컴포넌트에 `onPostLoad` 를 부릅니다. 로더가 부릅니다.
         * @details `onPostLoad` 는 부착과 핸들 PROPERTY 가 해석된 **뒤에** 온다. 읽는 시점에 부르면 다른 오브젝트를 가리키는 필드가 아직 저장된 id 다.
         */
        void addLoadedState( GameObject* pObject, uint64 savedID, hashed_string savedName, SerializePurpose purpose, bool bExternalParentAllowed = true, uint64 savedIDScope = 0 );
        /**
         * @brief 상태를 읽으며 모은 컴포넌트 핸들의 키 글을 더합니다. 로더가 `addLoadedState` 와 함께 부릅니다.
         * @details 핸들은 키의 해시만 든다. 대상이 모르는 타입이라 풀지 못한 참조를 남길 때 그 글을 `MissingComponent` 에 맡겨야 다음 글 저장이 참조를 그대로 쓴다.
         */
        void addComponentKeyTexts( ComponentKeyTextMap&& mapKeyText );

        /**
         * @brief 기록한 오브젝트들의 부착과 핸들 PROPERTY 를 연결하고, 상태를 읽은 오브젝트의 컴포넌트에 `onPostLoad` 를 부릅니다. 모두 읽은 뒤 한 번 부릅니다.
         * @details 그 앞에, 읽는 동안 이름이 겹쳐 번호를 받은 오브젝트는 그 이름이 비었으면 저장된 이름으로 되돌린다(플레이 중 이름을 바꾼
         *          오브젝트를 되돌릴 때 앞의 것이 뒤의 것 이름을 잠시 차지하고 있을 수 있다).
         * @return 모든 핸들 PROPERTY 를 옮겼으면 true 입니다. `Saved` 배치에서 옮기지 못한 핸들이 있으면 오류 로그와 함께 false 이고, 부르는 쪽은 로드 실패로 처리합니다.
         *         두 번째 호출은 첫 호출의 결과를 그대로 돌려줍니다.
         */
        [[nodiscard]] bool finish();
        /**
         * @brief 기록한 오브젝트를 연결하지 않고 버립니다. 부르는 쪽이 그 오브젝트들을 지울 때(중간에 멈춘 비동기 씬 로드 — `Scene::instantiateNodes`) 부릅니다.
         * @details 소멸자의 "finish 를 빠뜨렸다" 단언은 이것을 부르지 않고 놓친 길만 잡는다 — 멈춤은 의도한 길이라 단언이 아니다.
         */
        void abandon()
        {
            _bFinished           = SW_TRUE;
            _bReferencesResolved = SW_FALSE;
        }

        /** @brief 저장된 id 로 이 배치의 오브젝트를 찾습니다. 없으면 nullptr 입니다. */
        GameObject* findBySavedID( uint64 savedID ) const;
        /** @brief 저장된 이름으로 이 배치의 오브젝트를 찾습니다(id 가 없는 데이터용). 같은 이름이 여럿이면 먼저 기록된 것입니다. */
        GameObject* findBySavedName( hashed_string savedName ) const;

        ObjectIDSpace getIDSpace() const { return _idSpace; }

    private:
        struct Entry
        {
            GameObject*                _pObject{ nullptr };
            uint64                     _savedID{ 0 };
            hashed_string              _savedName{};
            bool                       _bExternalParentAllowed{ true }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            optional<SerializePurpose> _loadedPurpose{};                ///< 상태를 읽었으면 그 읽기의 목적이다. `finish` 가 `onPostLoad` 를 부르고 이 목적이 다룬 필드의 핸들을 옮긴다
            uint64                     _savedIDScope{ 0 };              ///< 상태 안의 참조 id 에 XOR 해 이 배치의 id 로 옮긴다(`ObjectLoadContext::_savedIDScope`)
        };

        /** @brief `add` 와 `addLoadedState` 의 본체입니다. */
        void addEntry( GameObject* pObject, uint64 savedID, hashed_string savedName, bool bExternalParentAllowed, optional<SerializePurpose> loadedPurpose, uint64 savedIDScope );

        /** @brief 항목 하나의 씬 컴포넌트마다 부모를 찾아 붙이고, 못 찾으면 참조를 남깁니다. */
        void resolveEntry( const Entry& entry ) const;
        /** @brief 부착 참조의 소유자 필드가 가리키는 오브젝트를 반환합니다. 자기 자신이면 항목의 오브젝트이고, 못 찾으면 nullptr 입니다. */
        GameObject* findAttachOwner( const Entry& entry, hashed_string ownerName, uint64 ownerID ) const;
        /**
         * @brief 상태를 읽은 항목 하나의 컴포넌트마다 `GameObjectHandle` 을 모든 깊이에서 이 실행의 오브젝트로 바꿉니다. 그 읽기의 목적이 다루지 않은 필드(`Transient`,
         *        저작 읽기의 `RuntimeState`)는 읽지 않았으므로 건드리지 않습니다. 상태 없이 기록한 항목(`add`)은 읽은 값이 없어 바꾸지 않습니다.
         * @return `Saved` 배치에서 옮기지 못한 자리(구조체 · 컨테이너를 원소로 가진 set 안, 구조체 키 안)가 있으면 필드 이름과 함께 오류 로그를 남기고 false 입니다.
         */
        [[nodiscard]] bool resolveObjectReferences( const Entry& entry ) const;
        /** @brief 저장된 핸들 하나가 이 실행에서 가리키는 오브젝트를 반환합니다(클래스 설명의 규칙). */
        GameObjectHandle resolveObjectReference( GameObjectHandle savedHandle, uint64 savedIDScope ) const;
        /**
         * @brief 저장된 컴포넌트 핸들 하나가 이 실행에서 가리키는 컴포넌트의 핸들을 반환합니다(클래스 설명의 규칙).
         * @param leafProperty 이 핸들이 든 필드다. 대상 타입(`_handleTargetTypeName`)이 여기 있다.
         * @param outInvalid 대상이 대상 타입이 아니거나 대상 타입을 알 수 없으면 true 로 바꿉니다(오류 로그와 함께). 부르는 쪽이 로드 오류로 처리합니다.
         */
        ComponentHandle resolveComponentReference( const Entry& entry, const TypeRegistry& registry, ComponentHandle savedHandle, const PropertyInfo& leafProperty,
                                                   bool& outInvalid ) const;

    private:
        vector<Entry>                        _listEntry;
        unordered_map<uint64, GameObject*>   _mapSavedIDToObject;
        unordered_map<hashed_string, uint32> _mapSavedNameToEntry;
        ComponentKeyTextMap                  _mapComponentKeyText; ///< 읽은 핸들의 키 글이다(`addComponentKeyTexts`)
        ObjectIDSpace                        _idSpace;
        uint8                                _bFinished           : 1;
        uint8                                _bReferencesResolved : 1; ///< `finish` 의 결과다. 두 번째 호출이 그대로 돌려준다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ObjectStateSerializer
     * @brief GameObject 상태를 XML, JSON, 바이너리로 저장하고 로드합니다.
     */
    class SW_API ObjectStateSerializer
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 문자열: PROPERTY 기반 GameObject XML 이며, 부모는 SceneComponent 의 `_pParent` 에서 구한다.
        // ------------------------------------------------------------------------------
        /**
         * @brief GameObject 상태를 XML 문자열로 직렬화합니다.
         * @details 루트는 TypeInfo 이름 `GameObject` 이다. 스칼라는 속성으로, `_listComponent` 는
         *          `<_listComponent>` 아래 런타임 타입 노드로 쓴다. 로컬 TRS 와 Attach 는 SceneComponent PROPERTY 다.
         *          GameObject 의 부모는 `getParent()` 가 SceneComponent 의 `_pParent` 에서 구하므로
         *          별도 `_parentGO` 필드나 속성을 쓰지 않는다.
         */
        static string saveToXMLString( const GameObject* pGameObject, const ObjectSaveOptions& options );
        /** @brief GameObject 상태를 JSON 으로 직렬화합니다. XMLSerializer 와 같은 PROPERTY 그래프를 씁니다. */
        [[maybe_unused]] static string saveToJSONString( const GameObject* pGameObject, const ObjectSaveOptions& options );

        /** @brief GameObject 상태를 바이너리 버퍼로 빠르게 직렬화합니다(핫 리로드와 프리팹용). */
        [[nodiscard]] static bool saveToBinaryBuffer( const GameObject* pGameObject, vector<uint8>& outBuffer, const ObjectSaveOptions& options );

        /**
         * @brief XML 문자열에서 GameObject 상태를 복원합니다(ObjectID 제외).
         * @param context 원래 id 를 복원할지(`_pIdentity`), 여러 오브젝트를 함께 읽는 배치인지(`_pBatch`) 정합니다. 기본값은 새 id 의 단독 로드입니다.
         * @details 적용하기 전에 기존 컴포넌트를 비운다. 배치 없이 읽으면 부착은 이 오브젝트 하나짜리 배치로 바로 연결한다(다른 오브젝트는
         *          매니저의 런타임 id 로 찾는다). 여러 오브젝트를 읽을 때는 배치를 주고 모두 읽은 뒤 `ObjectStateBatch::finish` 를 부른다.
         *          그래야 자식이 부모보다 먼저 읽혀도 부모를 찾는다.
         */
        [[nodiscard]] static bool loadFromXMLString( GameObject* pGameObject, string_view xmlString, const ObjectLoadContext& context );
        [[nodiscard]] static bool loadFromJSONString( GameObject* pGameObject, string_view jsonString, const ObjectLoadContext& context );

        /**
         * @brief 바이너리 버퍼에서 GameObject 상태를 복원하고 읽은 바이트 수를 반환합니다(실패하면 0).
         * @param context `loadFromXMLString` 과 같습니다.
         * @details 버퍼는 본문 크기(uint32)와 본문으로 이루어진다. 부모는 상태의 부착 필드가 id 로 가리킨다.
         */
        [[nodiscard]] static size_t loadFromBinaryBuffer( GameObject* pGameObject, const uint8* pData, size_t size, const ObjectLoadContext& context );

        // ------------------------------------------------------------------------------
        // 2) 런타임 ID: 같은 오브젝트를 복원할 때 핸들이 이어지게 한다(`ObjectIdentity`)
        // ------------------------------------------------------------------------------
        /** @brief 오브젝트와 컴포넌트의 현재 ID 를 기록합니다. 삭제 대기 컴포넌트는 제외합니다. */
        static ObjectIdentity captureIdentity( const GameObject* pGameObject );

        /** @brief ID 목록을 바이너리로 덧붙입니다(스냅샷 봉투에 싣는 용도). */
        static void writeIdentity( const ObjectIdentity& identity, vector<uint8>& outBuffer );

        /** @brief `writeIdentity` 로 쓴 것을 읽고, 읽은 바이트 수를 반환합니다. 실패하면 0 입니다. */
        static size_t readIdentity( const uint8* pData, size_t size, ObjectIdentity& outIdentity );

        /**
         * @brief 이 프로세스에서 찍은 스냅샷인지 가리는 값입니다. 프로세스마다 처음 부를 때 한 번 무작위로 정해지고, 0 은 아닙니다.
         * @details 핫 리로드 스냅샷과 세이브 파일은 형식이 같다. 다른 실행에서 만든 세이브 파일의 id 를 복원하면 이 실행에서
         *          이미 나간 id 와 겹칠 수 있으므로, 스냅샷에 이 값을 적어 두고 같을 때만 id 를 복원한다. Engine 에 있어서
         *          게임과 GameFramework DLL 을 다시 로드해도 바뀌지 않는다.
         */
        static uint64 getProcessToken();

    private:
        /**
         * @brief 리플렉션 문자열 포맷(XML, JSON) 하나로 저장합니다. 두 포맷은 직렬화기만 다르고 처리 순서가 같습니다.
         * @details 정의는 .cpp 에만 있다(그곳에서만 인스턴스화한다). 헤더가 직렬화기를 알 필요가 없다.
         */
        template <typename TSerializer>
        static string saveToText( const GameObject* pGameObject, const ObjectSaveOptions& options );
        /** @brief 리플렉션 문자열 포맷 하나에서 복원합니다. `loadFromXMLString` 과 `loadFromJSONString` 의 본체입니다. */
        template <typename TSerializer>
        [[nodiscard]] static bool loadFromXMLText( GameObject* pGameObject, string_view text, const ObjectLoadContext& context );
        /**
         * @brief 오브젝트의 상태를 제자리에서 다시 읽습니다. 로더 세 개(XML, JSON, 바이너리)는 포맷 읽기만 `deserializeState( version, context )` 로 넘깁니다.
         * @details 자식 연결을 기록하고 컴포넌트를 비운 뒤, 컴포넌트 ID 를 복원하는 범위 안에서 읽는다. 읽으면 배치에 기록하고(배치가 없으면 한 개짜리
         *          배치로 바로 연결한다), 읽기에 실패해도 기록해 둔 자식은 다시 붙인다.
         *          정의는 .cpp 에만 있다(ID 범위가 `GameObject` 의 비공개 타입이라 이 클래스의 멤버여야 한다).
         */
        template <typename DeserializeStateFunc>
        [[nodiscard]] static bool loadStateInPlace( GameObject* pGameObject, const ObjectLoadContext& context, DeserializeStateFunc&& deserializeState );
    };
} // namespace sw

/*
 * 전용 서버가 짓지 않는 소스(런타임 UI · `Client/`)의 리플렉션 형식 이름 표입니다 — 서버가 씬에서 그 컴포넌트를 만나면 `MissingComponent` 가 아니라 조용히 건너뜁니다.
 * 표는 생성 파일 `Config/Engine/ClientOnlyTypes.generated.json`(`Scripts/generate/client_only_types.py`, 커밋 훅이 낡음을 잡는다)이다. 서버는 그 형식을 모르므로
 *          이름 바꾸기 · 빠진 모듈(오류 — 경고를 남기고 원문을 지킨다)과 클라이언트 전용(정상 — 서버에는 할 일이 없다)을 이 표로 가른다.
 *          언리얼은 클래스를 서버도 지어 두고 HUD 를 띄우지 않지만, 여기는 클래스를 짓지 않으니 이름만 싣는다(기억). 클라이언트 구성은 표를 읽지 않는다.
 */
namespace sw
{
    /** @struct ClientOnlyTypeTable @brief 표 읽기입니다. 처음 물을 때 한 번 읽고(스레드 안전) 프로세스가 끝날 때까지 둡니다. */
    struct SW_API ClientOnlyTypeTable
    {
        static constexpr const utf8* kTableFile = "Config/Engine/ClientOnlyTypes.generated.json";

        /** @brief @p typeName 이 이 구성이 짓지 않는 클라이언트 전용 형식이면 true 입니다. 클라이언트 구성(`build::kWithClientCode`)은 언제나 false 입니다. */
        [[nodiscard]] static bool isClientOnlyTypeSkipped( string_view typeName );
    };
} // namespace sw
