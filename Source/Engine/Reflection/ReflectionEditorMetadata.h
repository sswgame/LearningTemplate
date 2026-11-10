/**
 * @file ReflectionEditorMetadata.h
 * @brief 에디터 · 문서만 읽는 리플렉션 메타데이터(카테고리 · 표시 이름 · 툴팁 · 커스텀 메타 · 편집 조건 · 슬라이더 범위)와 그 옆 표입니다.
 * @details 런타임 메타데이터(`ReflectionMetadata.h`)와 떼어 둔다 — 그 구조체는 이 데이터를 포인터 하나(`_pEditor`)로만 가리켜 **배치가 구성마다 같다**.
 *          값은 생성기가 헤더마다 내는 개발 전용 단위(`<이름>.gen.meta.cpp`)가 정적 등록으로 올리고(`TypeEditorMetadataSource`), 타입을 등록할 때
 *          `TypeRegistry` 가 FQN 으로 찾아 붙인다(`TypeEditorMetadataSource::find`). 배포 구성은 그 단위를 짓지 않아 포인터가 비고, 읽는 쪽은 기본값을 본다.
 *          언리얼 `WITH_METADATA`(UMetaData 를 패키지 옆 표에 두고 배포에서 뺀다)와 같은 자리다 — 다만 `#if` 로 칸을 가르지 않고 빌드 단위로 가른다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"
#include "Core/Registry/RegistrationList.h"

namespace sw
{
    /** @brief `REFLECT()` 에 적은 에디터 메타데이터입니다. 멤버 이름은 생성기 DTO(`ParsedTypeInfo`)와 같다 — 생성 코드가 이름으로 넣는다. */
    struct SW_API TypeEditorMetadata
    {
        string                               _category;
        string                               _displayName;
        string                               _tooltip;
        unordered_map<hashed_string, string> _mapCustomMeta;
        uint8                                _bHideInMenu   : 1; ///< "Add Component" 메뉴에서 숨긴다
        [[maybe_unused]] uint8               _reservedFlags : 7;

        TypeEditorMetadata() noexcept;

        /** @brief 커스텀 메타데이터를 조회합니다. 없으면 nullptr 입니다. */
        const string* findCustomMeta( const hashed_string& key ) const noexcept;
        /** @brief 아무것도 적지 않은 기본값입니다(포인터가 빈 타입 — 배포 구성 · 메타데이터 없는 타입 — 이 읽는다). */
        static const TypeEditorMetadata& getDefault() noexcept;
    };

    /** @brief `PROPERTY()` 에 적은 에디터 메타데이터입니다. 이벤트(멀티캐스트 델리게이트 PROPERTY)도 같은 형식의 표시 칸을 쓴다. */
    struct SW_API PropertyEditorMetadata
    {
        string                               _category;
        string                               _displayName;
        string                               _tooltip;
        unordered_map<hashed_string, string> _mapCustomMeta;
        /** @brief `EditCondition = "…"` 에 적은 글입니다(문서 · 덤프). 판정은 생성된 `PropertyMetadata::_pEditCondition` 이 합니다. */
        string _editCondition;
        /** @brief `FileFilter = "*.png;*.dds"` 로 적은, 경로 필드가 받는 파일 형식입니다. */
        string _fileFilter;
        /** @brief `UIMin` 과 `UIMax` 로 정한 인스펙터 슬라이더와 드래그 범위입니다. 허용 범위(`PropertyMetadata::_minRange`)와는 별개입니다. */
        float32                _uiMinRange;
        float32                _uiMaxRange;
        uint8                  _bHasUIMinRange      : 1;
        uint8                  _bHasUIMaxRange      : 1;
        uint8                  _bEditConditionHides : 1; ///< 조건이 거짓이면 막지 않고 숨긴다
        uint8                  _bColorHDR           : 1; ///< 색을 HDR 로 편집한다. 1 을 넘는 값을 허용한다
        uint8                  _bMultiline          : 1; ///< 여러 줄 텍스트 필드
        uint8                  _bHideInInspector    : 1; ///< 에디터 인스펙터에서 숨긴다(`HideInInspector`)
        [[maybe_unused]] uint8 _reservedDisplay     : 2;

        PropertyEditorMetadata() noexcept;

        /** @brief 슬라이더 범위(`UIMin`, `UIMax`)가 모두 있으면 true 입니다. */
        bool hasFullUIRange() const noexcept { return _bHasUIMinRange != SW_FALSE && _bHasUIMaxRange != SW_FALSE; }
        /** @brief 커스텀 메타데이터를 조회합니다. 없으면 nullptr 입니다. */
        const string* findCustomMeta( const hashed_string& key ) const noexcept;
        /** @brief 아무것도 적지 않은 기본값입니다. */
        static const PropertyEditorMetadata& getDefault() noexcept;
    };

    /** @brief `FUNCTION()` 에 적은 에디터 메타데이터입니다. */
    struct SW_API FunctionEditorMetadata
    {
        string                               _category;
        string                               _displayName;
        string                               _tooltip;
        string                               _editorPreview; ///< `FUNCTION( EditorPreview = "..." )` 의 값이다. 에디터 뷰포트 미리보기가 이 값으로 메서드를 찾는다
        unordered_map<hashed_string, string> _mapCustomMeta;
        uint8                                _bCallInEditor : 1; ///< 에디터 인스펙터에 실행 버튼을 보여 준다
        [[maybe_unused]] uint8               _reserved      : 7;

        FunctionEditorMetadata() noexcept;

        /** @brief 커스텀 메타데이터를 조회합니다. 없으면 nullptr 입니다. */
        const string* findCustomMeta( const hashed_string& key ) const noexcept;
        /** @brief 아무것도 적지 않은 기본값입니다. */
        static const FunctionEditorMetadata& getDefault() noexcept;
    };

    /** @brief `ENUM()` 에 적은 에디터 메타데이터입니다(커스텀 메타뿐). */
    struct SW_API EnumEditorMetadata
    {
        unordered_map<hashed_string, string> _mapCustomMeta;

        /** @brief 커스텀 메타데이터를 조회합니다. 없으면 nullptr 입니다. */
        const string* findCustomMeta( const hashed_string& key ) const noexcept;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct TypeEditorMetadataSet
     * @brief 타입 하나의 에디터 메타데이터 묶음입니다 — 타입 줄, 프로퍼티 · 이벤트(이름), 메서드(찾는 이름 `FunctionInfo::_hashName`).
     * @details 개발 전용 생성 단위의 채우기 함수가 채우고 `TypeRegistry` 가 든다. 맵은 노드 기반이라 다 채운 뒤에는 원소 주소가 바뀌지 않는다 — 리플렉션 정보가 그 주소를 가리킨다.
     */
    struct SW_API TypeEditorMetadataSet
    {
        TypeEditorMetadata                                   _type;
        unordered_map<hashed_string, PropertyEditorMetadata> _mapProperty;
        unordered_map<hashed_string, PropertyEditorMetadata> _mapEvent;
        unordered_map<hashed_string, FunctionEditorMetadata> _mapFunction;

        PropertyEditorMetadata& addProperty( const hashed_string& name ) { return _mapProperty[name]; }
        PropertyEditorMetadata& addEvent( const hashed_string& name ) { return _mapEvent[name]; }
        FunctionEditorMetadata& addFunction( const hashed_string& lookupName ) { return _mapFunction[lookupName]; }
    };

    /**
     * @struct TypeEditorMetadataSource
     * @brief 개발 전용 생성 단위 하나가 올리는 타입 줄입니다(그 단위의 정적 저장소에 있다). 줄 목록(정적 등록)은 이 형식의 static 멤버가 다룬다.
     * @details 타입을 등록하는 `TypeRegistry` 가 FQN 으로 찾아 묶음을 만든다. 배포 구성은 생성 단위를 짓지 않아 목록이 비어 있다.
     */
    struct SW_API TypeEditorMetadataSource
    {
        const utf8* _pFqn;                                 ///< 타입 FQN(`TypeInfo::_fullyQualifiedName`)
        void ( *_pFill )( TypeEditorMetadataSet& outSet ); ///< 묶음을 채운다

        /** @brief 줄을 올립니다(정적 등록자). 같은 FQN 이 있으면 공용 거절 로그를 남기고 false 입니다(핫 리로드의 교체 이미지는 대체한다). */
        [[nodiscard]] static bool add( const TypeEditorMetadataSource* pSource );
        /** @brief 줄을 뺍니다(올린 모듈이 내려갈 때). */
        static void remove( const TypeEditorMetadataSource* pSource );
        /** @brief FQN @p fqn 의 줄입니다. 없으면(배포 · 메타데이터 없는 타입) nullptr 입니다. */
        static const TypeEditorMetadataSource* find( string_view fqn );
    };

    /** @struct EnumEditorMetadataSource @brief 개발 전용 생성 단위 하나가 올리는 열거형 줄입니다(`TypeEditorMetadataSource` 의 짝). */
    struct SW_API EnumEditorMetadataSource
    {
        const utf8* _pFqn;
        void ( *_pFill )( EnumEditorMetadata& outMetadata );

        [[nodiscard]] static bool              add( const EnumEditorMetadataSource* pSource );
        static void                            remove( const EnumEditorMetadataSource* pSource );
        static const EnumEditorMetadataSource* find( string_view fqn );
    };

    /** @brief 생성 단위가 두는 타입 줄 정적 등록자입니다(모듈이 내려가면 뺀다). */
    SW_DEFINE_STATIC_REGISTRAR( TypeEditorMetadataRegistrar, const TypeEditorMetadataSource*, &TypeEditorMetadataSource::add, &TypeEditorMetadataSource::remove );
    /** @brief 생성 단위가 두는 열거형 줄 정적 등록자입니다. */
    SW_DEFINE_STATIC_REGISTRAR( EnumEditorMetadataRegistrar, const EnumEditorMetadataSource*, &EnumEditorMetadataSource::add, &EnumEditorMetadataSource::remove );
} // namespace sw
