/**
 * @file TypeInfo.h
 * @brief 형식 하나의 리플렉션 정보입니다.
 */
#pragma once
#include "Engine/Reflection/EnumInfo.h"
#include "Engine/Reflection/FunctionInfo.h"
#include "Engine/Reflection/PropertyInfo.h"

namespace sw
{
    /**
     * @brief 상속 포함 프로퍼티 목록의 읽기 뷰입니다(`TypeInfo::getPropertiesWithBase`). 원소는 그 프로퍼티를 **선언한 타입**의 `_listProperty` 에 있습니다.
     * @details 예전에는 부모 목록을 값으로 복사해 들어, 무관한 타입이 하나 등록될 때마다 모든 타입이 사본을 비우고 다시 지었다 — 밖에서 든
     *          `const PropertyInfo*`(UI 바인딩 · 시퀀서 트랙)가 매달렸다. 지금은 선언한 타입의 원소를 가리키는 포인터 배열이라, 그 타입 자신이
     *          다시 등록되거나 내려가기 전까지 포인터가 산다(언리얼 `FProperty` 사슬이 선언 클래스의 것을 가리키는 것과 같은 자리).
     *          부모가 없는 타입은 자기 `_listProperty` 를 그대로 본다(포인터 배열을 따로 짓지 않는다).
     */
    class PropertyListView
    {
    public:
        class Iterator
        {
        public:
            Iterator( const PropertyInfo* pValue, const PropertyInfo* const* ppItem ) noexcept
                : _pValue{ pValue }
                , _ppItem{ ppItem }
            {
            }
            const PropertyInfo& operator*() const noexcept { return _ppItem != nullptr ? **_ppItem : *_pValue; }
            const PropertyInfo* operator->() const noexcept { return &**this; }
            Iterator&           operator++() noexcept
            {
                if ( _ppItem != nullptr )
                    ++_ppItem;
                else
                    ++_pValue;
                return *this;
            }
            bool operator==( const Iterator& other ) const noexcept { return _pValue == other._pValue && _ppItem == other._ppItem; }
            bool operator!=( const Iterator& other ) const noexcept { return ( *this == other ) == false; }

        private:
            const PropertyInfo*        _pValue; ///< 값 배열을 볼 때의 위치(`_ppItem` 이 nullptr 일 때)
            const PropertyInfo* const* _ppItem; ///< 포인터 배열을 볼 때의 위치
        };

        /** @brief 값 배열(부모 없는 타입의 `_listProperty`)을 봅니다. */
        PropertyListView( const PropertyInfo* pValues, size_t count ) noexcept
            : _pValues{ pValues }
            , _ppItems{ nullptr }
            , _count{ count }
        {
        }
        /** @brief 선언 목록(`TypeInfo::_listProperty`)을 그대로 봅니다 — 목록을 받는 도구(`SerializerUtil::matchProperty`)에 자기 선언만 넘길 때 쓴다. */
        PropertyListView( const vector<PropertyInfo>& listProperty ) noexcept // NOLINT(google-explicit-constructor): 선언 목록도 같은 뷰로 받는다
            : PropertyListView{ listProperty.data(), listProperty.size() }
        {
        }
        /** @brief 포인터 배열(상속 병합 목록)을 봅니다. */
        PropertyListView( const PropertyInfo* const* ppItems, size_t count ) noexcept
            : _pValues{ nullptr }
            , _ppItems{ ppItems }
            , _count{ count }
        {
        }

        size_t              size() const noexcept { return _count; }
        bool                empty() const noexcept { return _count == 0; }
        const PropertyInfo& operator[]( size_t index ) const noexcept { return _ppItems != nullptr ? *_ppItems[index] : _pValues[index]; }
        Iterator            begin() const noexcept { return _ppItems != nullptr ? Iterator{ nullptr, _ppItems } : Iterator{ _pValues, nullptr }; }
        Iterator            end() const noexcept { return _ppItems != nullptr ? Iterator{ nullptr, _ppItems + _count } : Iterator{ _pValues + _count, nullptr }; }

    private:
        const PropertyInfo*        _pValues;
        const PropertyInfo* const* _ppItems;
        size_t                     _count;
    };

    /// @brief 등록된 타입의 정보입니다. FQN, 프로퍼티, 메서드, 생성 가능 여부를 담습니다.
    struct SW_API TypeInfo
    {
        /**
         * @brief 루트부터 자기까지의 **이름(FQN 의 intern 인덱스)** 을 깊이 순서로 적은 조상 테이블과 자기 깊이입니다. **맨 앞 멤버다** — `castTo` 가
         *        이 타입을 전방 선언만 보고 그 주소에서 읽는다(`TypeCastHeader::of`, Reflection.cpp 의 `static_assert` 가 지킨다).
         * @details 캐스트의 핫패스는 이 테이블만 본다. `테이블[pTarget 의 깊이] == pTarget 의 이름` 이면 파생 관계다.
         *          포인터가 아니라 이름을 비교하므로, 레지스트리 밖 사본(테스트 목이 손으로 만든 `StaticType()`)도 이름이 같으면 같은 타입으로 본다.
         *          이 규칙은 부모를 따라가는 `isSameTypeName` 과 같다. `_typeID` 를 쓰지 않는 이유도 그 사본이다. 사본은 자기 id 를 따로 가진다.
         *          등록이나 해제로 부모 체인이 바뀔 수 있으면 `TypeRegistry` 가 깊이를 `kAncestorDepthUnknown` 으로 비우고,
         *          등록 배치가 끝날 때(`populateLookupCaches`)나 첫 상속 검사가 테이블을 다시 만든다.
         *          원자값으로 둔 이유는 `_pParentType` 과 같다. 첫 조회는 여러 스레드에서 올 수 있고, 모두 같은 값을 쓴다.
         *          등록과 캐스트가 동시에 실행되는 경우는 `_pParentType` 과 마찬가지로 가정하지 않는다(모듈 로드는 단일 스레드다).
         */
        TypeCastHeader _castHeader;
        size_t         _size;
        /** @brief `$ctor` 로 placement new 한 인스턴스를 소멸시킵니다. 없으면 nullptr 입니다. */
        void ( *_destroyInstance )( void* ) = nullptr;
        /**
         * @brief 이 타입의 컴포넌트를 @p pOwner(`GameObject*`)에 붙여 만들고 그 `Component*` 를 돌려주는 함수입니다(`GameObject::addComponentForReflection<T>`). 만들 수 있는 컴포넌트 타입이 아니면 nullptr 입니다.
         * @details 언리얼의 `UClass` 가 리플렉션 정보와 생성 함수(`ClassConstructor`)를 함께 가지는 것과 같다. 이름으로 컴포넌트를 만드는 경로
         *          (`GameObjectManager::addComponentByName` 이고, 씬 로드, 프리팹 로드, 에디터가 쓴다)는 이 함수 하나를 본다.
         *          코드젠이 추상이 아닌 컴포넌트 타입마다 채운다. 모듈 코드를 가리키므로 모듈을 해제할 때(`clearContent`) 비우고,
         *          같은 FQN 을 다시 등록하면 새 이미지의 주소로 덮어쓴다.
         */
        void* ( *_addComponent )( void* pOwner ); // 불투명하다 — 리플렉션(1 층)은 Object 층(5 층)의 형식을 모른다. 부르는 쪽(Object 층)이 형을 되돌린다
        /** @brief `REFLECT( Validate = fn )` 의 타입 검증 함수입니다. 없으면 nullptr 입니다. 모듈 코드를 가리키므로 `clearContent` 가 비웁니다. */
        ReflectValidateFunction _pValidate;
        /**
         * @brief 고정 배치형(B) 코덱 테이블입니다(`Engine/Serialization/Fixed/FixedLayout.h`). 코드젠이 모든 `REFLECT` 타입에 채우고, 적격이 아니면
         *        이유만 든 테이블입니다. 코드젠을 거치지 않은 타입(내장 스칼라)은 nullptr 입니다. 모듈 코드를 가리키므로 `clearContent` 가 비웁니다.
         */
        const FixedCodec*                _pFixedCodec;
        hashed_string                    _name;
        hashed_string                    _fullyQualifiedName;
        hashed_string                    _parentFQN;
        hashed_string                    _moduleName;
        vector<PropertyInfo>             _listProperty;
        vector<FunctionInfo>             _listMethod;
        vector<EventInfo>                _listEvent; ///< 이 타입이 선언한 이벤트다. 기반 타입의 이벤트는 `findEventInHierarchy` 가 따라 올라가며 찾는다
        ReflectMetadataBox<TypeMetadata> _metadata;  ///< `REFLECT()` 에 적은 메타데이터(`ReflectionMetadata.h`)
        /**
         * @brief 상속 병합 목록입니다 — 기반부터, 파생이 같은 이름을 제자리에서 덮는다. 원소는 선언한 타입의 `_listProperty` 를 가리킨다(`PropertyListView`).
         * @details `TypeRegistry` 가 등록 · 해제 때 **그 타입과 자손만** 다시 짓는다(`rebuildDerivedCaches`). 조회는 읽기만 한다 — `mutable` 지연 캐시가
         *          워커의 첫 조회와 등록 사이에서 경합하던 자리다.
         */
        vector<const PropertyInfo*>                       _listPropertyWithBase;
        unordered_map<hashed_string, const PropertyInfo*> _mapNameToPropertyWithBase; ///< 계층 병합 목록에서 이름과 별칭으로 항목을 찾는 맵이다. 목록과 함께 만든다
        unordered_map<hashed_string, const PropertyInfo*> _mapNameToProperty;
        unordered_map<hashed_string, const FunctionInfo*> _mapNameToMethod;
        /**
         * @brief `_parentFQN` 을 한 번 찾아 둔 부모 `TypeInfo` 입니다. 없거나 아직 찾지 못했으면 nullptr 입니다.
         * @details `isDerivedFrom` 이 조상마다 `findType(_parentFQN)`(shared_mutex 잠금과 해시맵 조회)을 부르지 않도록,
         *          등록 배치가 끝날 때(`TypeRegistry::populateLookupCaches`) 한 번 찾아 둔다. 그러면 조상을 따라가는 비용은 포인터 역참조 몇 번이다.
         *
         *          **타입을 해제하면 이 값을 비운다.** `TypeInfo` 의 주소는 고정이라 옮겨지지는 않지만,
         *          모듈 해제 뒤에는 묘비가 된 부모를 가리킬 수 있다. 그래서 해제할 때 남은 타입 전부의 이 값을 비우고,
         *          비어 있으면 `getParentType()` 이 이름으로 다시 찾는다. 원자값으로 둔 이유는, 배치 밖에서 등록한 타입(테스트)은
         *          첫 조회가 여러 스레드에서 동시에 올 수 있기 때문이다. 같은 값을 쓰는 경쟁이라 relaxed 로 충분하다.
         */
        mutable atomic<const TypeInfo*> _pParentType;
        /**
         * @brief `_parentFQN` 을 이름으로 찾아 봤지만 찾지 못했던 때의 타입 테이블 세대입니다. 0 이면 아직 찾아 보지 않았습니다.
         * @details 등록되지 않은 기반 타입은 이름만 적혀 있다. 부모를 물을 때마다(계층 프로퍼티 조회, 이름 따라가기, 기본값 체인 수집)
         *          레지스트리를 잠그고 다시 찾지 않도록, 세대가 같으면 결과도 같으므로 세대를 적어 두고 등록이나 해제로 세대가 바뀔 때만 다시 찾는다.
         */
        mutable atomic<uint32> _parentMissGeneration;
        uint32                 _typeID;
        /** @brief `REFLECT( Abstract )` 이거나 C++ 추상 클래스입니다. 생성할 수 없습니다(`UCLASS( Abstract )` 에 해당). */
        uint8 _bAbstract : 1;
        /** @brief `REFLECT( Static )` 타입(함수 라이브러리)입니다. `FunctionMetadata::_bStatic` 과는 다릅니다. */
        uint8 _bStatic : 1;
        /** @brief 내장 스칼라나 문자열 타입입니다(int32, bool, sw::string 등). REFLECT 코드젠이 없습니다. */
        uint8 _bPrimitive                 : 1;
        uint8 _bIsCacheBuilt              : 1; ///< 자기 이름 맵(`_mapNameToProperty` · `_mapNameToMethod`)을 지었다. 아니면 이름 조회는 선형으로 찾는다
        uint8 _bListPropertyWithBaseBuilt : 1; ///< 상속 병합 목록을 지었다(`rebuildDerivedCaches`). 부모가 있는데 아니면 `getPropertiesWithBase` 가 멈춘다
        /**
         * @brief 아래 셋은 판정(POD · 세이브 · 검증)이고 `computeDerivedFlags` 가 한 번에 구한다 — 조회는 읽기만 한다(`mutable` 지연 계산이 없다).
         * @details 등록은 배치 끝에 바뀐 타입의 상속 목록을 **모두** 다시 지은 뒤 판정을 구한다(`TypeRegistry::rebuildAffected`) — 검증 판정이 칸 타입의
         *          상속 목록을 재귀로 읽기 때문이다. 레지스트리 밖 사본(테스트)은 `rebuildDerivedCaches` → `computeDerivedFlags` 를 직접 부른다.
         *          구하지 않은 채 물으면 멈춘다(`_bDerivedFlagsBuilt`). 예전에는 첫 조회가 구해 로드 워커들이 같은 바이트의 비트를 함께 썼다.
         */
        uint8                  _bIsPODFastPath       : 1;
        uint8                  _bHasSaveGameProperty : 1;
        uint8                  _bHasValidator        : 1;
        uint8                  _bDerivedFlagsBuilt   : 1; ///< 위 셋을 구했다. 상속 목록을 비우면 함께 내려간다
        [[maybe_unused]] uint8 _reservedCacheFlags   : 7;
        [[maybe_unused]] uint8 _reservedPadding[2];

        /** @brief 비어 있는 TypeInfo 를 만듭니다. */
        TypeInfo() noexcept;
        ~TypeInfo() = default;
        TypeInfo( const TypeInfo& other );
        TypeInfo( TypeInfo&& other ) noexcept;
        TypeInfo& operator=( const TypeInfo& other );
        TypeInfo& operator=( TypeInfo&& other ) noexcept;

        /** @brief 커스텀 메타데이터 태그를 조회합니다. */
        const string* findCustomMeta( const hashed_string& key ) const noexcept;

        /** @brief 생성 코드의 서술자 표(`PropertyDesc`) @p count 줄로 선언 프로퍼티 목록을 짓습니다. 목록 할당은 한 번입니다. */
        void setPropertyTable( const PropertyDesc* pDesc, size_t count );

        /** @brief 카테고리를 반환합니다. */
        string_view getCategory() const noexcept;
        /** @brief 표시 이름을 반환합니다. DisplayName 메타가 없으면 C++ 타입 이름을 반환합니다. */
        const utf8* getDisplayName() const noexcept;
        /** @brief 툴팁 문자열을 반환합니다. */
        string_view getTooltip() const noexcept;
        /** @brief "Add Component" 메뉴에서 숨겨야 하는지 여부를 반환합니다. */
        bool isHiddenInMenu() const noexcept;

        /** @brief 팩토리나 `$ctor` 로 인스턴스를 만들 수 있으면 true 입니다. */
        bool canConstruct() const noexcept { return _bAbstract == SW_FALSE && _bStatic == SW_FALSE && _bPrimitive == SW_FALSE; }

        /** @brief REFLECT 없이 등록된 내장 타입(int32, float32, …)이면 true 입니다. */
        bool isPrimitive() const noexcept { return _bPrimitive != SW_FALSE; }

        /** @brief 직렬화나 복사에서 memcpy POD 경로를 쓸 수 있으면 true 입니다(`computeDerivedFlags` 가 구한 값). */
        bool usesPODCopyFastPath() const;
        /**
         * @brief 상속한 기반까지 포함해 `SaveGame` 프로퍼티가 하나라도 있으면 true 입니다. 세이브 직렬화가 그 프로퍼티만 쓰는 타입(옵트인)인지 나타냅니다.
         * @details 상속 목록 캐시와 함께 비우고 다시 구한다(`clearInheritedProperties` → `computeDerivedFlags`).
         */
        bool hasSaveGameProperty() const;
        /** @brief 검증 함수 유무입니다(`ReflectionValidation::hasValidator` 가 읽는다). */
        bool hasValidator() const;
        /**
         * @brief 판정(POD · 세이브 · 검증)을 지금의 상속 목록으로 구합니다. `rebuildDerivedCaches` 뒤에 부르고, 칸 타입의 목록도 지어져 있어야 합니다.
         * @details 레지스트리가 배치 끝에 바뀐 타입마다 부른다. 레지스트리 밖 사본은 부르는 쪽이 직접 부른다.
         */
        void computeDerivedFlags();
        /** @brief 이 타입이 targetFqn 이거나 그 파생이면 true 입니다. 부모가 등록되지 않았어도 `_parentFQN` 이 같으면 true 입니다. */
        bool isDerivedFrom( const hashed_string& targetFqn ) const;
        /**
         * @brief 이 타입이 pTarget 이거나 그 파생이면 true 입니다. 인라인 함수이며, 조상 테이블이 있으면 로드 두 번과 비교 한 번으로 끝납니다.
         * @details 캐스트의 핫패스라 DLL 경계를 넘지 않는다. 양쪽 모두 테이블이 있으면 `테이블[pTarget 의 깊이] == pTarget 의 이름` 으로 끝난다.
         *          테이블이 아직 없거나 만들 수 없는 쪽은 `isDerivedFromSlow` 가 테이블을 만들거나 부모 포인터를 따라간다.
         *          레지스트리 밖 사본(테스트 목의 `StaticType()`)도 이름이 같으면 같은 타입으로 본다.
         *          pTarget 이 nullptr 이면 false 다.
         */
        bool isDerivedFrom( const TypeInfo* pTarget ) const
        {
            if ( pTarget == nullptr )
                return false;
            if ( pTarget == this )
                return true;
            bool bDerived = false;
            return _castHeader.tryCompare( pTarget->_castHeader, bDerived ) ? bDerived : isDerivedFromSlow( pTarget );
        }
        /** @brief 조상 테이블이 없을 때의 경로입니다. 만들 수 있으면 만들어서 답하고, 아니면 부모 포인터를 따라갑니다. */
        bool isDerivedFromSlow( const TypeInfo* pTarget ) const;
        /** @brief 부모 TypeInfo 입니다. 찾아 둔 것이 없으면 이름으로 찾아 기록합니다. 부모가 없거나 등록되지 않았으면 nullptr 입니다. */
        const TypeInfo* getParentType() const;
        /** @brief 부모 포인터를 이름으로 다시 찾습니다. 등록 배치가 끝날 때 `TypeRegistry` 가 부릅니다. */
        void resolveParentType() const;
        /** @brief 부모 포인터와 "찾지 못했다"는 기록을 비웁니다. 해제 뒤에는 부모가 사라졌을 수 있으므로 `TypeRegistry` 가 부릅니다. */
        void clearParentType() const
        {
            _pParentType.store( nullptr, std::memory_order_relaxed );
            _parentMissGeneration.store( 0, std::memory_order_relaxed );
        }

        /** @brief 조상 테이블이 만들어져 있으면 true 입니다. 이때 상속 검사는 부모를 따라가지 않고 O(1) 로 답합니다. */
        bool hasAncestorDisplay() const
        {
            return _castHeader._ancestorDepth.load( std::memory_order_acquire ) < constant::reflection::kAncestorDisplayDepth;
        }
        /**
         * @brief 부모 포인터를 따라 조상 테이블을 만듭니다. 체인을 모두 찾았고 테이블 깊이 안에 들어오면 true 입니다.
         * @details 이름이 없거나, 순환이거나, 테이블보다 깊으면 `kAncestorDepthNone` 을 적고 false 를 반환한다. 그 타입은 다음
         *          등록이나 해제까지 부모 포인터를 따라가서 답한다. 찾을 수 없는 부모(REFLECT 가 아닌 기반이나 아직 로드되지 않은
         *          모듈)는 체인의 끝으로 본다. 그 부모가 등록되는 순간 `registerClass` 가 테이블을 비운다.
         *          등록 배치가 끝날 때 `TypeRegistry` 가 부르고, 배치 밖의 타입은 첫 상속 검사가 부른다.
         */
        bool computeAncestorDisplay() const;
        /** @brief 조상 테이블을 비웁니다. 체인이 바뀔 수 있는 등록과 해제 뒤에 `TypeRegistry` 가 부릅니다. */
        void clearAncestorDisplay() const
        {
            _castHeader._ancestorDepth.store( constant::reflection::kAncestorDepthUnknown, std::memory_order_relaxed );
        }
        /**
         * @brief 부모에게서 **물려받은** 캐시(상속 포함 목록, 그 이름 맵, POD · 세이브 · 검증 판정)를 비웁니다. 다시 지을 때까지 상속 목록을 물으면 멈춥니다.
         * @details 기반이 다시 등록되거나 내려갈 때 `TypeRegistry` 가 **그 자손에게만** 부르고, 잠금을 푼 뒤 `rebuildDerivedCaches` 로 다시 짓는다.
         *          예전에는 등록 한 번마다 모든 타입을 비워 등록 비용이 O(타입²) 였다.
         */
        void clearInheritedProperties();
        /**
         * @brief 선언에서 **파생된** 캐시(이름 맵, 상속 포함 목록, 부모 포인터, 조상 테이블, POD 판정)를 모두 비웁니다.
         * @details 복사 대입과 이동 대입이 함께 쓴다. 캐시를 추가하면 여기서도 비워야 한다. 빠뜨리면 대입된 타입이 **이전 타입의 캐시**로 답한다
         *          (이름으로 조회하면 다른 오프셋을 준다).
         */
        void invalidateDerivedCaches();
        /**
         * @brief 자기 이름 맵과 상속 병합 목록 · 그 이름 맵을 지금의 부모 사슬로 다시 짓습니다. `TypeRegistry` 가 등록 · 해제 뒤 잠금 밖, 단일 스레드에서 부릅니다.
         * @details 사슬의 단계마다 **자기 선언 목록**만 접어 넣으므로(부모의 병합 목록을 읽지 않는다) 같은 배치에서 부모가 아직 다시 지어지지 않았어도 답이 같다.
         *          사슬이 순환하면(`_parentFQN` 이 자손을 가리키는 핫 리로드) 오류를 남기고 자기 목록만 둔다. 판정(POD · 세이브 · 검증)은 부르는 쪽이 이어서 구한다.
         */
        void rebuildDerivedCaches();
        /** @brief 레지스트리에 살아 있으면 true 입니다. 모듈을 해제하면 내려가고 다시 등록하면 올라갑니다. 사본은 항상 true 입니다. */
        bool isAlive() const { return _castHeader.isAlive(); }
        /**
         * @brief 묘비로 남길 때 모듈이 가지고 있던 내용을 비웁니다. 이름, id, 모듈만 남습니다.
         * @details 프로퍼티와 메서드 목록은 그 모듈의 코드(델리게이트, `$ctor`, 소멸 함수)를 가리킨다. 모듈이 언로드된 뒤에
         *          이 목록을 파괴하면(재등록 대입이나 레지스트리 소멸) 사라진 코드를 부른다. 그래서 해제 시점에,
         *          **모듈이 아직 살아 있을 때** 여기서 비운다.
         */
        void clearContent();

        /**
         * @brief 자신과 상속한 기반의 프로퍼티입니다(단일 부모 체인). 원소는 선언한 타입의 것입니다(`PropertyListView`).
         * @details 자식의 이름이 부모를 제자리에서 덮어쓴다. 단일 상속에서만 안전하다. 부모가 있는 타입은 `rebuildDerivedCaches` 가 지은 뒤에만 부를 수 있다 —
         *          레지스트리에 든 타입은 등록이 짓는다. 짓지 않은 사본(레지스트리 밖에서 손으로 만든 파생 타입)에 부르면 조용히 자기 목록만 주지 않고 멈춘다.
         */
        PropertyListView getPropertiesWithBase() const;

        /** @brief 자기 이름 맵이 지어져 있으면 true 입니다(레지스트리에 든 타입은 등록 뒤 늘 true 입니다). */
        bool isLookupCacheBuilt() const { return _bIsCacheBuilt != SW_FALSE; }

        /** @brief 이름이나 별칭으로 프로퍼티를 찾습니다. 맵을 짓지 않은 사본은 선형으로 찾습니다(같은 답, 읽기만 한다). */
        const PropertyInfo* findProperty( const hashed_string& propertyNameOrAlias ) const
        {
            if ( _bIsCacheBuilt == SW_FALSE || _listProperty.size() <= constant::reflection::kLinearSearchThreshold )
            {
                return VectorUtil::findIf( _listProperty, [&]( const PropertyInfo& propertyInfo )
                { return propertyInfo.matchesName( propertyNameOrAlias ); } );
            }

            auto it = _mapNameToProperty.find( propertyNameOrAlias );
            return it != _mapNameToProperty.end() ? it->second : nullptr;
        }

        /** @brief 이름으로 메서드를 찾습니다. 맵을 짓지 않은 사본은 선형으로 찾습니다. */
        const FunctionInfo* findMethod( const hashed_string& methodName ) const
        {
            if ( _bIsCacheBuilt == SW_FALSE || _listMethod.size() <= constant::reflection::kLinearSearchThreshold )
                return VectorUtil::findBy<&FunctionInfo::_hashName>( _listMethod, methodName );

            auto it = _mapNameToMethod.find( methodName );
            return it != _mapNameToMethod.end() ? it->second : nullptr;
        }

        /**
         * @brief 현재 클래스와 부모 상속 체인에서 프로퍼티를 찾습니다. 계층 병합 목록과 함께 만든 맵 하나로 찾습니다.
         * @details `getPropertiesWithBase()` 가 만들어 둔 맵을 한 번 조회한다. 파생 타입이 기반과 같은 이름을 다시 적으면 파생 타입이 이긴다(병합 규칙과 같다).
         */
        const PropertyInfo* findPropertyInHierarchy( const hashed_string& propertyNameOrAlias ) const;

        /**
         * @brief 프로퍼티를 순회합니다. bIncludeBase 가 true 이면 상속 체인을 포함합니다.
         * @details **직렬화는 true 를 써야 합니다.** 기본값 false 로 부르면 **상속된 PROPERTY 를 저장도 로드도 하지
         *          않는다.** 컴포넌트에서는 그것이 곧 `SceneComponent` 의 트랜스폼이라, 씬, 프리팹, Undo 스냅샷에서
         *          메시, 스프라이트, 카메라의 위치가 사라진다.
         *          `getPropertiesWithBase()` 는 기반 타입 먼저, 파생 타입이 같은 이름을 **덮어쓰는** 순서로
         *          평탄화하고 캐시하므로 중복으로 방출하지 않는다.
         * @note 상속 체인을 **부르는 쪽이 직접 순회하는** 코드(`ComponentDefaults` 가 레벨마다 자기 XML
         *       노드를 찾아 적용한다)는 false 가 맞다. true 를 주면 같은 값을 여러 노드에서
         *       중복 적용한다.
         */
        template <typename Func>
        void forEachProperty( Func&& func, bool bIncludeBase = false ) const
        {
            if ( bIncludeBase == false )
            {
                for ( const PropertyInfo& propertyInfo : _listProperty )
                {
                    func( propertyInfo );
                }
            }
            else
            {
                for ( const PropertyInfo& propertyInfo : getPropertiesWithBase() )
                {
                    func( propertyInfo );
                }
            }
        }

        /** @brief 메서드를 순회합니다. */
        template <typename Func>
        void forEachMethod( Func&& func ) const
        {
            for ( const FunctionInfo& method : _listMethod )
            {
                func( method );
            }
        }

        /** @brief 이 타입이 선언한 이벤트를 이름으로 찾습니다. */
        const EventInfo* findEvent( const hashed_string& eventName ) const
        {
            return VectorUtil::findBy<&EventInfo::_name>( _listEvent, eventName );
        }

        /** @brief 자기부터 기반으로 올라가며 이벤트를 이름으로 찾습니다. 파생 타입이 우선합니다. */
        const EventInfo* findEventInHierarchy( const hashed_string& eventName ) const;
        /** @brief 자기부터 기반으로 올라가며 메서드를 이름으로 찾습니다. 파생 타입이 우선합니다. */
        const FunctionInfo* findMethodInHierarchy( const hashed_string& methodName ) const;

        /** @brief 기반부터 파생 순서로 이벤트를 순회합니다. */
        template <typename Func>
        void forEachEventWithBase( Func&& func ) const
        {
            const TypeInfo* arrChain[constant::reflection::kMaxParentChainDepth];
            uint32          depth = collectTypeChain( arrChain );
            while ( depth > 0 )
            {
                --depth;
                for ( const EventInfo& event : arrChain[depth]->_listEvent )
                {
                    func( event );
                }
            }
        }

        /** @brief 자기부터 기반까지의 체인을 @p outArrType 에 담고 길이를 반환합니다. 찾을 수 없는 부모에서 끝나고, 순환이면 상한에서 멈춥니다. */
        uint32 collectTypeChain( const TypeInfo* ( &outArrType )[constant::reflection::kMaxParentChainDepth] ) const;
    };

} // namespace sw
