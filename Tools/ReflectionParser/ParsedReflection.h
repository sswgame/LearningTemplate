/**
 * @file ParsedReflection.h
 * @brief AstVisitor 가 수집하고 CodeGenerator 가 출력하는 파싱 결과 DTO 입니다.
 * @details "헤더에서 무엇을 뽑았는지" 만 보려면 이 파일부터 보면 됩니다.
 *          AST 순회 로직은 AstVisitor, 코드 출력은 CodeGenerator 에 있습니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionTypes.h"

#include "ReflectionParser/ParserDefines.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) parse — 컨테이너 / 프로퍼티 / 함수 / 타입 / 열거형
    // ------------------------------------------------------------------------------
    /** @brief Sequence/Map 등 중첩 컨테이너 트리 노드 */
    struct ParsedContainerNode
    {
        string                          _containerType; ///< VectorWrapper stem
        string                          _typeName;      ///< TypeInfo 이름 (vector, unordered_map, …)
        string                          _elementTypeName;
        string                          _keyTypeName;
        shared_ptr<ParsedContainerNode> _elementNested;
        ContainerKind                   _containerKind;
        uint8                           _bIsContainer : 1;
        [[maybe_unused]] uint8          _reserved     : 7;
        [[maybe_unused]] uint16         _padding;

        ParsedContainerNode() noexcept
            : _containerType{}
            , _typeName{}
            , _elementTypeName{}
            , _keyTypeName{}
            , _elementNested{ nullptr }
            , _containerKind{ ContainerKind::None }
            , _bIsContainer{ SW_FALSE }
            , _reserved{ 0 }
            , _padding{ 0 }
        {
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 열거형 안의 개별 enumerator */
    struct ParsedEnumeratorInfo
    {
        string _name;
        int64  _value{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief `Min` · `Max` 에 적은 글 그대로입니다. 숫자 하나이거나, 쉼표로 나눈 숫자 · 필드 이름(`-_halfSize`) 목록입니다.
     * @details 숫자 하나면 인스펙터 범위(`_minRange` 등)로도 쓰입니다. 목록 · 이름 꼴은 고정 배치형 코덱만 씁니다(필드 이름은 실행 시간에 값이 정해집니다).
     */
    struct ParsedRangeExpr
    {
        string _text;
    };
} // namespace sw

namespace sw
{
    /** @brief 고정 배치형(B) 코덱에서 값 하나를 어떤 꼴로 인코딩하는지 나타냅니다(`docs/plans/ReflectionFormats.md` 3.4 표). */
    enum class FixedValueKind : uint8
    {
        Ineligible, ///< 인코딩 표 밖입니다. 그 필드를 가진 타입은 코덱이 없습니다(`ParsedFixedType::_reason`)
        Bool,
        Integer,
        Float32,
        Float64,
        Enum,
        String,
        Math,       ///< `float3` 같은 수학 타입입니다. 원소마다 실수 규칙을 씁니다
        Vector,     ///< 가변 길이 시퀀스입니다. `MaxCount` 가 필요합니다
        FixedArray, ///< `T[N]` · `std::array<T, N>` 입니다. 개수 없이 원소 N 개를 씁니다
        Reflect,    ///< 다른 `REFLECT` 타입입니다. 그 타입의 필드를 씁니다
    };
} // namespace sw

namespace sw
{
    /** @brief 필드가 가리키는 열거형 하나입니다. 다른 헤더의 열거형도 선언에서 읽습니다. */
    struct ParsedFixedEnum
    {
        string                       _fullyQualifiedName;
        string                       _underlyingTypeName; ///< 정규 이름(`uint8`)
        vector<ParsedEnumeratorInfo> _listEnumerator;     ///< 선언 순서, `Count` 포함
        string                       _countEnumerator;    ///< `ENUM( Count = … )` 의 이름입니다. 없으면 비어 있습니다
        uint8                        _bIsBitFlag : 1;
        [[maybe_unused]] uint8       _reserved   : 7;

        ParsedFixedEnum() noexcept
            : _fullyQualifiedName{}
            , _underlyingTypeName{}
            , _listEnumerator{}
            , _countEnumerator{}
            , _bIsBitFlag{ SW_FALSE }
            , _reserved{ 0 }
        {
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 필드 타입을 고정 배치형 인코딩 표에 맞춰 분류한 결과입니다. 컨테이너는 원소를 재귀로 담습니다.
     * @details 선언(clang 타입)에서 읽습니다. 사용자는 B 형식을 따로 선언하지 않습니다.
     */
    struct ParsedFixedType
    {
        string                      _typeName;           ///< 정규 타입 이름입니다. 수학 타입 표를 찾고 진단에 씁니다
        string                      _reason;             ///< Ineligible 이면 이유입니다(영문, 로그와 생성 주석에 그대로 나갑니다)
        string                      _fullyQualifiedName; ///< Reflect 의 FQN
        shared_ptr<ParsedFixedType> _element;            ///< Vector · FixedArray 의 원소
        shared_ptr<ParsedFixedEnum> _enum;               ///< Enum 의 열거자
        FixedValueKind              _kind;

        ParsedFixedType() noexcept
            : _typeName{}
            , _reason{}
            , _fullyQualifiedName{}
            , _element{ nullptr }
            , _enum{ nullptr }
            , _kind{ FixedValueKind::Ineligible }
        {
        }
    };
} // namespace sw

namespace sw
{
    /** @brief PROPERTY(...) 가 붙은 멤버 필드, 또는 값 참조를 돌려주는 메서드(값이 객체 밖에 있는 프로퍼티) */
    struct ParsedPropertyInfo
    {
        /** @brief 리플렉션 이름(직렬화 키)입니다. 기본은 멤버 이름이고 `PROPERTY( Name = "..." )` 로 바꿉니다. */
        string _name;
        /** @brief C++ 식별자입니다. 필드 이름, 또는 접근자 프로퍼티면 값 참조를 돌려주는 메서드 이름입니다. 코드젠이 `offsetof` · `decltype` · 호출에 씁니다. */
        string                          _memberName;
        string                          _typeName;
        vector<string>                  _listAlias;
        string                          _category;
        string                          _displayName;
        string                          _tooltip;
        string                          _defaultValue;
        string                          _assetType;
        string                          _repNotify;     ///< `RepNotify = fn` — 같은 타입의 메서드 이름(모양은 `validateMemberFunctions` 가 본다)
        string                          _validate;      ///< `Validate = fn` — 같은 타입의 `void fn( ValidationContext& )`
        string                          _editCondition; ///< `EditCondition = "…"` — 식은 런타임(`PropertyEditCondition`)이 푼다
        string                          _fileFilter;    ///< `FileFilter = "*.png;*.dds"`
        string                          _units;         ///< `Units = m` — 단위 표(`ReflectUnits.h`)에 있어야 한다. 커스텀 메타 `Units` 로 실린다
        vector<pair<string, string>>    _listCustomMeta;
        string                          _containerType;
        string                          _elementTypeName;
        string                          _keyTypeName;
        shared_ptr<ParsedContainerNode> _containerTree;
        /** @brief 고정 배치형 코덱에서의 분류입니다. 필드면 늘 있고, 접근자 프로퍼티는 Ineligible 입니다. */
        shared_ptr<ParsedFixedType> _fixedType;
        ParsedRangeExpr             _minRangeExpr; ///< `Min` 의 글입니다. 숫자 하나면 `_minRange` 에도 들어갑니다
        ParsedRangeExpr             _maxRangeExpr;
        float32                     _minRange;
        float32                     _maxRange;
        float32                     _uiMinRange; ///< `UIMin` — 슬라이더 범위(허용 범위와 따로)
        float32                     _uiMaxRange;
        float32                     _step;     ///< `Step` 양자화 간격입니다(`_bHasStep`)
        uint32                      _maxCount; ///< `MaxCount` 문자열 · 컨테이너 최대 길이입니다(`_bHasMaxCount`)
        uint32                      _line;     ///< 선언 줄입니다. 생성 오류 메시지에 씁니다
        ContainerKind               _containerKind;
        uint8                       _bIsBitField   : 1;
        uint8                       _bReadOnly     : 1;
        uint8                       _bXMLAttribute : 1;
        uint8                       _bAssetPath    : 1;
        uint8                       _bPolymorphic  : 1;
        uint8                       _bHasMinRange  : 1;
        uint8                       _bHasMaxRange  : 1;
        uint8                       _bIsContainer  : 1;
        uint8                       _bTransient    : 1;
        /** @brief 값이 비어 있으면 직렬화에서 생략합니다(PROPERTY(SkipIfEmpty)). */
        uint8 _bSkipIfEmpty : 1;
        /** @brief 데이터 파일이 반드시 적어야 하는 칸입니다(PROPERTY(Required)). */
        uint8 _bRequired        : 1;
        uint8 _bHideInInspector : 1;
        /**
         * @brief 값이 객체 밖에 있습니다 — `PROPERTY` 가 필드가 아니라 값 참조(`T&`)를 돌려주는 인자 없는 메서드에 붙었습니다.
         * @details 코드젠은 오프셋 대신 그 메서드를 부르는 `PropertyInfo::_pValueAccessor` 를 냅니다(`_memberName` 이 메서드 이름).
         */
        uint8 _bIsAccessor : 1;
        uint8 _bReplicated : 1;
        uint8 _bSaveGame   : 1;
        uint8 _bInterp     : 1;
        /** @brief `RepNotify` 함수가 이전 값을 받는다(`void fn( const T& )`). 선언에서 온 사실이다(애노테이션 줄이 아니다). */
        uint8 _bRepNotifyTakesOldValue : 1;
        uint8 _bHasUIMinRange          : 1;
        uint8 _bHasUIMaxRange          : 1;
        uint8 _bEditConditionHides     : 1;
        uint8 _bColorHdr               : 1;
        uint8 _bMultiline              : 1;
        uint8 _bHasStep                : 1;
        uint8 _bHasMaxCount            : 1;
        /** @brief 공개 멤버입니다. 다른 타입의 코덱이 이 필드를 직접 읽고 쓸 수 있는지(중첩 타입 펼치기) 정합니다. */
        uint8                   _bPublic  : 1;
        [[maybe_unused]] uint8  _reserved : 1;
        [[maybe_unused]] uint16 _padding;

        ParsedPropertyInfo() noexcept
            : _name{}
            , _memberName{}
            , _typeName{}
            , _listAlias{}
            , _category{}
            , _displayName{}
            , _tooltip{}
            , _defaultValue{}
            , _assetType{}
            , _repNotify{}
            , _validate{}
            , _editCondition{}
            , _fileFilter{}
            , _units{}
            , _listCustomMeta{}
            , _containerType{}
            , _elementTypeName{}
            , _keyTypeName{}
            , _containerTree{ nullptr }
            , _fixedType{ nullptr }
            , _minRangeExpr{}
            , _maxRangeExpr{}
            , _minRange{ 0.0f }
            , _maxRange{ 1.0f }
            , _uiMinRange{ 0.0f }
            , _uiMaxRange{ 1.0f }
            , _step{ 0.0f }
            , _maxCount{ 0 }
            , _line{ 0 }
            , _containerKind{ ContainerKind::None }
            , _bIsBitField{ SW_FALSE }
            , _bReadOnly{ SW_FALSE }
            , _bXMLAttribute{ SW_FALSE }
            , _bAssetPath{ SW_FALSE }
            , _bPolymorphic{ SW_FALSE }
            , _bHasMinRange{ SW_FALSE }
            , _bHasMaxRange{ SW_FALSE }
            , _bIsContainer{ SW_FALSE }
            , _bTransient{ SW_FALSE }
            , _bSkipIfEmpty{ SW_FALSE }
            , _bRequired{ SW_FALSE }
            , _bHideInInspector{ SW_FALSE }
            , _bIsAccessor{ SW_FALSE }
            , _bReplicated{ SW_FALSE }
            , _bSaveGame{ SW_FALSE }
            , _bInterp{ SW_FALSE }
            , _bRepNotifyTakesOldValue{ SW_FALSE }
            , _bHasUIMinRange{ SW_FALSE }
            , _bHasUIMaxRange{ SW_FALSE }
            , _bEditConditionHides{ SW_FALSE }
            , _bColorHdr{ SW_FALSE }
            , _bMultiline{ SW_FALSE }
            , _bHasStep{ SW_FALSE }
            , _bHasMaxCount{ SW_FALSE }
            , _bPublic{ SW_FALSE }
            , _reserved{ 0 }
            , _padding{ 0 }
        {
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief REFLECT 타입이 선언한 메서드 하나의 모양입니다(FUNCTION 이 없어도). 애노테이션이 메서드 이름을 가리킬 때(`RepNotify` 등) 대조합니다.
     */
    struct ParsedMethodSignature
    {
        string         _name;
        vector<string> _listParameterTypeName; ///< 정규 타입 이름 — const · 참조는 벗겼다
    };
} // namespace sw

namespace sw
{
    /** @brief 함수 · 이벤트의 인자 하나입니다. */
    struct ParsedParameterInfo
    {
        string _name;     ///< 선언에 적힌 인자 이름. 이름 없이 적었으면 비어 있다
        string _typeName; ///< 정규 타입 이름(`int32` · `string` · `DamageEvent`) — const · 참조는 벗긴다
        /** @brief 기본 인자의 C++ 식 그대로(`1.0f` · `"idle"` · `Mode::Fast`). 없으면 비어 있다. 런타임이 글로 읽어 인자 타입으로 바꾼다. */
        string _defaultValue;
    };
} // namespace sw

namespace sw
{
    /** @brief FUNCTION(...) 가 붙은 메서드(또는 자동 등록 생성자) */
    struct ParsedFunctionInfo
    {
        string                       _name;
        string                       _returnTypeName;
        vector<ParsedParameterInfo>  _listParameter;
        string                       _category;
        string                       _displayName;
        string                       _tooltip;
        string                       _editorPreview; ///< `EditorPreview = "..."` — 에디터 뷰포트 미리보기가 이 메서드를 찾는 종류
        vector<pair<string, string>> _listCustomMeta;
        FunctionNetRole              _netRole;
        uint8                        _bReliable     : 1;
        uint8                        _bValidate     : 1;
        uint8                        _bConstructor  : 1;
        uint8                        _bStatic       : 1;
        uint8                        _bConst        : 1;
        uint8                        _bCallInEditor : 1;
        uint8                        _bSynthesized  : 1; ///< 소스에 선언이 없어 파서가 더한 기본 생성자(집합체 · 암시 생성자 타입)
        [[maybe_unused]] uint8       _reserved      : 1;
        [[maybe_unused]] uint16      _padding;

        ParsedFunctionInfo() noexcept
            : _name{}
            , _returnTypeName{}
            , _listParameter{}
            , _category{ annotation::kDefaultMethodCategory }
            , _displayName{}
            , _tooltip{}
            , _editorPreview{}
            , _listCustomMeta{}
            , _netRole{ FunctionNetRole::Local }
            , _bReliable{ SW_FALSE }
            , _bValidate{ SW_FALSE }
            , _bConstructor{ SW_FALSE }
            , _bStatic{ SW_FALSE }
            , _bConst{ SW_FALSE }
            , _bCallInEditor{ SW_FALSE }
            , _bSynthesized{ SW_FALSE }
            , _reserved{ 0 }
            , _padding{ 0 }
        {
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief `PROPERTY()` 가 붙은 멀티캐스트 델리게이트 필드 — 이름으로 찾아 묶을 수 있는 이벤트입니다.
     * @details 값이 아니라 구독 목록이라 직렬화하지 않습니다. 애노테이션은 프로퍼티 표 그대로 읽고(`_annotation`), 표시 메타
     *          (`Category` · `DisplayName` · `Tooltip` · `Meta` · `HideInInspector`)만 받습니다 — 나머지는 오류입니다.
     */
    struct ParsedEventInfo
    {
        string                      _memberName; ///< C++ 필드 이름. 리플렉션 이름은 `_annotation._name`(기본은 이 이름)
        vector<ParsedParameterInfo> _listParameter;
        ParsedPropertyInfo          _annotation; ///< 프로퍼티 표로 읽은 애노테이션(표시 메타만 쓴다)
    };
} // namespace sw

namespace sw
{
    /** @brief REFLECT 가 붙은 클래스·구조체 */
    struct ParsedTypeInfo
    {
        string                       _name;
        string                       _fullyQualifiedName;
        string                       _parentFQN;
        string                       _category;
        string                       _displayName;
        string                       _tooltip;
        vector<string>               _listAlias;
        string                       _validate; ///< `REFLECT( Validate = fn )` — 같은 타입의 `void fn( ValidationContext& )`
        vector<pair<string, string>> _listCustomMeta;
        vector<ParsedPropertyInfo>   _listProperty;
        vector<ParsedFunctionInfo>   _listMethod;
        vector<ParsedEventInfo>      _listEvent;
        uint32                       _baseCount; ///< 기반 클래스 수입니다. 둘 이상이면 고정 배치형 코덱이 없습니다
        uint8                        _bAbstract         : 1;
        uint8                        _bStatic           : 1;
        uint8                        _bReflectBody      : 1;
        uint8                        _bComponentFactory : 1;
        uint8                        _bHideInMenu       : 1;
        /** @brief 첫 기반 클래스에 `REFLECT()` 가 있습니다. 없으면 기반의 필드는 프로퍼티가 아니므로 고정 배치형 코덱도 기반을 건너뜁니다. */
        uint8                  _bParentReflected : 1;
        [[maybe_unused]] uint8 _reserved         : 2;

        ParsedTypeInfo() noexcept
            : _name{}
            , _fullyQualifiedName{}
            , _parentFQN{}
            , _category{}
            , _displayName{}
            , _tooltip{}
            , _listAlias{}
            , _validate{}
            , _listCustomMeta{}
            , _listProperty{}
            , _listMethod{}
            , _listEvent{}
            , _baseCount{ 0 }
            , _bAbstract{ SW_FALSE }
            , _bStatic{ SW_FALSE }
            , _bReflectBody{ SW_FALSE }
            , _bComponentFactory{ SW_FALSE }
            , _bHideInMenu{ SW_FALSE }
            , _bParentReflected{ SW_FALSE }
            , _reserved{ 0 }
        {
        }

        bool requiresTypeAPI() const noexcept { return _bReflectBody == SW_TRUE; }
        bool requiresComponentFactory() const noexcept { return _bComponentFactory == SW_TRUE; }
    };
} // namespace sw

namespace sw
{
    /** @brief ENUM(...) 가 붙은 열거형 */
    struct ParsedEnumInfo
    {
        string                       _name;
        string                       _fullyQualifiedName;
        vector<string>               _listAlias;
        vector<pair<string, string>> _listValueAlias;        ///< `ValueAlias = "Old:Current"` — 이름을 바꾼 열거자의 (옛 이름, 지금 열거자)
        vector<pair<string, string>> _listRemovedEnumerator; ///< `Removed = "Old:Replacement"` — 지운 열거자의 (옛 이름, 대신 읽을 열거자)
        vector<pair<string, string>> _listCustomMeta;
        vector<ParsedEnumeratorInfo> _listEnumerator;
        string                       _invalidEnumerator;
        string                       _countEnumerator;
        /** @brief 기반 정수 타입의 원본 철자입니다(`uint8` 이 아니라 `unsigned char`). 전방 선언을 코드젠하는 데 씁니다. */
        string _underlyingType;
        /** @brief `ENUM( Flags )` 입니다. 레지스트리의 비트플래그 표시와 비트 연산자 트레이트 코드젠을 함께 켭니다. */
        uint8 _bIsBitFlag : 1;
        /** @brief 클래스 · 구조체 **안에** 선언된 열거형이면 1 입니다. 그러면 밖에서 전방 선언할 수 없습니다. */
        uint8                  _bNestedInType : 1;
        [[maybe_unused]] uint8 _reserved      : 6;

        ParsedEnumInfo() noexcept
            : _name{}
            , _fullyQualifiedName{}
            , _listAlias{}
            , _listValueAlias{}
            , _listRemovedEnumerator{}
            , _listCustomMeta{}
            , _listEnumerator{}
            , _invalidEnumerator{}
            , _countEnumerator{}
            , _underlyingType{}
            , _bIsBitFlag{ SW_FALSE }
            , _bNestedInType{ SW_FALSE }
            , _reserved{ 0 }
        {
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 입력 헤더 하나에서 모은 것입니다. `.gen.cpp` / `.gen.h` 한 벌이 이것 하나에서 나옵니다.
     * @details 번역 단위 하나가 헤더 여럿을 담을 수 있으므로(한 TU 로 묶어 파싱) 결과는 헤더 단위로 나눠 둡니다.
     */
    struct ParsedHeader
    {
        vector<ParsedTypeInfo> _listType;
        vector<ParsedEnumInfo> _listEnum;
        /** @brief 이 헤더에서 리플렉션 오류가 났습니다. 산출물을 쓰지 않습니다(같은 TU 의 다른 헤더는 계속 모읍니다). */
        uint8                  _bHasError : 1;
        [[maybe_unused]] uint8 _reserved  : 7;

        ParsedHeader() noexcept
            : _listType{}
            , _listEnum{}
            , _bHasError{ SW_FALSE }
            , _reserved{ 0 }
        {
        }
    };
} // namespace sw
