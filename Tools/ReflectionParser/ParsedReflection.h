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
        vector<pair<string, string>>    _listCustomMeta;
        string                          _containerType;
        string                          _elementTypeName;
        string                          _keyTypeName;
        shared_ptr<ParsedContainerNode> _containerTree;
        float32                         _minRange;
        float32                         _maxRange;
        ContainerKind                   _containerKind;
        uint8                           _bIsBitField   : 1;
        uint8                           _bReadOnly     : 1;
        uint8                           _bXmlAttribute : 1;
        uint8                           _bAssetPath    : 1;
        uint8                           _bPolymorphic  : 1;
        uint8                           _bHasMinRange  : 1;
        uint8                           _bHasMaxRange  : 1;
        uint8                           _bIsContainer  : 1;
        uint8                           _bTransient    : 1;
        /** @brief 값이 비어 있으면 직렬화에서 생략합니다(PROPERTY(SkipIfEmpty)). */
        uint8 _bSkipIfEmpty     : 1;
        uint8 _bHideInInspector : 1;
        /**
         * @brief 값이 객체 밖에 있습니다 — `PROPERTY` 가 필드가 아니라 값 참조(`T&`)를 돌려주는 인자 없는 메서드에 붙었습니다.
         * @details 코드젠은 오프셋 대신 그 메서드를 부르는 `PropertyInfo::_pValueAccessor` 를 냅니다(`_memberName` 이 메서드 이름).
         */
        uint8                   _bIsAccessor : 1;
        [[maybe_unused]] uint8  _reserved    : 6;
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
            , _listCustomMeta{}
            , _containerType{}
            , _elementTypeName{}
            , _keyTypeName{}
            , _containerTree{ nullptr }
            , _minRange{ 0.0f }
            , _maxRange{ 1.0f }
            , _containerKind{ ContainerKind::None }
            , _bIsBitField{ SW_FALSE }
            , _bReadOnly{ SW_FALSE }
            , _bXmlAttribute{ SW_FALSE }
            , _bAssetPath{ SW_FALSE }
            , _bPolymorphic{ SW_FALSE }
            , _bHasMinRange{ SW_FALSE }
            , _bHasMaxRange{ SW_FALSE }
            , _bIsContainer{ SW_FALSE }
            , _bTransient{ SW_FALSE }
            , _bSkipIfEmpty{ SW_FALSE }
            , _bHideInInspector{ SW_FALSE }
            , _bIsAccessor{ SW_FALSE }
            , _reserved{ 0 }
            , _padding{ 0 }
        {
        }
    };

    /** @brief FUNCTION(...) 가 붙은 메서드(또는 자동 등록 생성자) */
    struct ParsedFunctionInfo
    {
        string                       _name;
        string                       _returnTypeName;
        vector<string>               _listParameterTypeName;
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
        [[maybe_unused]] uint8       _reserved      : 2;
        [[maybe_unused]] uint16      _padding;

        ParsedFunctionInfo() noexcept
            : _name{}
            , _returnTypeName{}
            , _listParameterTypeName{}
            , _category{ annotationConstants::kDefaultMethodCategory }
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
            , _reserved{ 0 }
            , _padding{ 0 }
        {
        }
    };

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
        vector<pair<string, string>> _listCustomMeta;
        vector<ParsedPropertyInfo>   _listProperty;
        vector<ParsedFunctionInfo>   _listMethod;
        uint8                        _bAbstract         : 1;
        uint8                        _bStatic           : 1;
        uint8                        _bReflectBody      : 1;
        uint8                        _bComponentFactory : 1;
        uint8                        _bHideInMenu       : 1;
        [[maybe_unused]] uint8       _reserved          : 3;

        ParsedTypeInfo() noexcept
            : _name{}
            , _fullyQualifiedName{}
            , _parentFQN{}
            , _category{}
            , _displayName{}
            , _tooltip{}
            , _listAlias{}
            , _listCustomMeta{}
            , _listProperty{}
            , _listMethod{}
            , _bAbstract{ SW_FALSE }
            , _bStatic{ SW_FALSE }
            , _bReflectBody{ SW_FALSE }
            , _bComponentFactory{ SW_FALSE }
            , _bHideInMenu{ SW_FALSE }
            , _reserved{ 0 }
        {
        }

        bool requiresTypeApi() const noexcept { return _bReflectBody == SW_TRUE; }
        bool requiresComponentFactory() const noexcept { return _bComponentFactory == SW_TRUE; }
    };

    /** @brief 열거형 안의 개별 enumerator */
    struct ParsedEnumeratorInfo
    {
        string _name;
        int64  _value{ 0 };
    };

    /** @brief ENUM(...) 가 붙은 열거형 */
    struct ParsedEnumInfo
    {
        string                       _name;
        string                       _fullyQualifiedName;
        vector<string>               _listAlias;
        vector<pair<string, string>> _listValueAlias;
        vector<pair<string, string>> _listCustomMeta;
        vector<ParsedEnumeratorInfo> _listEnumerator;
        string                       _invalidEnumerator;
        string                       _countEnumerator;
        /** @brief 기반 정수 타입의 정본 철자입니다(`uint8` 이 아니라 `unsigned char`). 전방 선언을 코드젠하는 데 씁니다. */
        string _underlyingType;
        /** @brief `ENUM( Flags )` 입니다. 등록부의 비트플래그 표시와 비트 연산자 트레이트 코드젠을 함께 켭니다. */
        uint8 _bIsBitFlag : 1;
        /** @brief 클래스 · 구조체 **안에** 선언된 열거형이면 1 입니다. 그러면 밖에서 전방 선언할 수 없습니다. */
        uint8                  _bNestedInType : 1;
        [[maybe_unused]] uint8 _reserved      : 6;

        ParsedEnumInfo() noexcept
            : _name{}
            , _fullyQualifiedName{}
            , _listAlias{}
            , _listValueAlias{}
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
