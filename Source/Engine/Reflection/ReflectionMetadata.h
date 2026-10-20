/**
 * @file ReflectionMetadata.h
 * @brief `REFLECT()` · `PROPERTY()` · `FUNCTION()` 에 적은 메타데이터(속성 키 표)입니다. 카테고리, 기본값, 범위, 직렬화 · 복제 · 세이브 플래그를 담습니다.
 * @details 속성 키를 더할 때마다 바뀌는 구조라 `ReflectionTypes.h` 에서 갈랐습니다(계획 BuildSpeed 12). `ReflectionTypes.h` 는
 *          `ReflectMetadataBox` 로 이 구조를 값처럼 들 뿐 정의를 모르므로, `GameObject.h` · `Component.h` 를 거쳐 그 헤더를 읽는 TU 는
 *          이 파일이 바뀌어도 다시 컴파일되지 않습니다. 메타데이터 칸(`info._metadata->_category` 등)을 읽거나 쓰는 TU 만 이 파일을 include 합니다.
 *          생성 코드(.gen.cpp)는 `ReflectGenerated.h` 가 include 합니다.
 */
#pragma once
#include "Engine/Reflection/ReflectionTypes.h"

namespace sw
{
    /// @brief `REFLECT()` 를 붙인 클래스와 구조체의 메타데이터입니다.
    struct SW_API TypeMetadata
    {
#if !defined( SW_SHIPPING )
        string                               _category;
        string                               _displayName;
        string                               _tooltip;
        unordered_map<hashed_string, string> _mapCustomMeta;
        uint8                                _bHideInMenu   : 1; ///< "Add Component" 메뉴에서 숨긴다
        [[maybe_unused]] uint8               _reservedFlags : 7;
#else
        [[maybe_unused]] uint8 _reservedEmpty;
#endif

        TypeMetadata() noexcept;

        /** @brief 커스텀 메타데이터를 조회합니다. Shipping 빌드에서는 nullptr 를 반환합니다. */
        const string* findCustomMeta( const hashed_string& key ) const noexcept
        {
#if !defined( SW_SHIPPING )
            auto iter = _mapCustomMeta.find( key );
            return ( iter != _mapCustomMeta.end() ) ? &iter->second : nullptr;
#else
            (void)key;
            return nullptr;
#endif
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 한 객체에서 푼 프로퍼티의 허용 범위(`PROPERTY( Min · Max )`)입니다. 적은 쪽 경계만 막습니다(`PropertyMetadata::computeAllowedRange`).
     * @details 경계는 float64 다. 필드 경계는 float64 로 읽히고, 숫자 경계(float32 로 보관)는 넓혀 담는다.
     */
    struct PropertyAllowedRange
    {
        float64 _min{ 0.0 };
        float64 _max{ 0.0 };
        bool    _bHasMin{ false };
        bool    _bHasMax{ false };

        /** @brief @p value 가 적힌 경계 안이면 true 입니다. 경계가 없으면 늘 true 입니다. */
        bool contains( const float64 value ) const noexcept
        {
            const bool bBelow = _bHasMin && value < _min;
            const bool bAbove = _bHasMax && value > _max;
            return bBelow == false && bAbove == false;
        }
    };
} // namespace sw

namespace sw
{
    /// @brief `PROPERTY()` 에 적은 메타데이터입니다. 카테고리, 기본값, 범위, XML 속성을 담습니다.
    struct SW_API PropertyMetadata
    {
#if !defined( SW_SHIPPING )
        string                               _category;
        string                               _displayName;
        string                               _tooltip;
        unordered_map<hashed_string, string> _mapCustomMeta;
        /** @brief `EditCondition = "…"` 식입니다. 식이 거짓이면 인스펙터가 편집을 막습니다(`PropertyEditCondition`). */
        string _editCondition;
        /** @brief `FileFilter = "*.png;*.dds"` 로 적은, 경로 필드가 받는 파일 형식입니다. */
        string _fileFilter;
        /** @brief `UIMin` 과 `UIMax` 로 정한 인스펙터 슬라이더와 드래그 범위입니다. 허용 범위(`_minRange`, `_maxRange`)와는 별개입니다. */
        float32                _uiMinRange;
        float32                _uiMaxRange;
        uint8                  _bHasUIMinRange      : 1;
        uint8                  _bHasUIMaxRange      : 1;
        uint8                  _bEditConditionHides : 1; ///< 조건이 거짓이면 막지 않고 숨긴다
        uint8                  _bColorHdr           : 1; ///< 색을 HDR 로 편집한다. 1 을 넘는 값을 허용한다
        uint8                  _bMultiline          : 1; ///< 여러 줄 텍스트 필드
        [[maybe_unused]] uint8 _reservedDisplay     : 3;
#endif
        /**
         * @brief 에셋이 이 프로퍼티를 생략할 때 쓰는 기본값입니다(`PROPERTY( Default = "..." )`).
         * @details XML, JSON, 바이너리 역직렬화가 이 값을 적용한다. C++ 멤버 초기화자와는 별개이므로 둘을 맞춰 둔다.
         */
        string _defaultValue;
        /** @brief 소프트 에셋의 타입 힌트입니다(`PROPERTY( AssetPath )`, `AssetType = "Texture"`). */
        string _assetType;
        /** @brief `RepNotify = fn` 에 적은 메서드 이름입니다. 받은 값으로 바꾼 뒤 이 메서드를 부르며, 호출은 `PropertyInfo::_pRepNotify` 가 맡습니다. */
        hashed_string _repNotify;
        /** @brief `Validate = fn` 에 적은 검증 함수 이름입니다. 결과 메시지와 문서에 씁니다. 호출은 `PropertyInfo::_pValidate` 가 맡습니다. */
        hashed_string _validate;
        /**
         * @brief 필드 이름 꼴 허용 범위(`PROPERTY( Min = "-_halfSize", Max = "_halfSize" )`)의 아래 · 위 경계를 인스턴스에서 읽는 함수입니다. 없으면 nullptr 입니다.
         * @details 경계가 실행 시간에 정해지므로 숫자(`_minRange`) 하나로는 담지 못한다. 생성기가 **같은 타입의 필드 하나**를 가리키는 경계에만 만든다.
         *          이름을 실행 시간에 찾지 않고 컴파일된 멤버 읽기라, 이름이 틀리면 생성된 코드가 컴파일되지 않는다. 숫자 경계(`_bHasMinRange`)와 함께 있지 않다.
         *          읽기 공통 끝도 이 경계를 지키므로 Shipping 에도 있다(`computeAllowedRange`).
         *          바깥 타입의 필드를 가리키는 경계와 성분 목록(`"-_a, 0, -_a"`)은 고정 배치형 코덱만 쓴다(인스펙터는 바깥 인스턴스를 모르고 벡터 성분에 범위를 두지 않는다).
         */
        PropertyRangeReadFunction _pReadMinRange;
        PropertyRangeReadFunction _pReadMaxRange;
        float32                   _minRange;
        float32                   _maxRange;
        /**
         * @brief 문자열 · 컨테이너 최대 길이입니다(`PROPERTY( MaxCount = 32 )`, `MaxCount = kMaxSlotCount`). 적지 않았으면 0 입니다.
         * @details 고정 배치형 코덱이 쓰기와 읽기에서 지키는 상한과 같은 값입니다. 상수 이름으로 적으면 생성기가 그 상수의 값을 넣으므로
         *          상한을 C++ 상수와 애노테이션 두 곳에 숫자로 적지 않습니다.
         */
        uint32 _maxCount;
        /**
         * @brief 아래 경계와 위 경계를 각각 적었는지 나타냅니다(`PROPERTY( Min = … )`, `Max = …`). 적은 쪽만 막습니다.
         * @details 경계 표시를 하나로 합치면 `Min = 0` 만 적은 프로퍼티의 위 경계가 기본값 1 로 남아, 인스펙터가 그 값을 1 에서 막는다.
         *          슬라이더는 두 경계가 모두 있을 때만 그린다(`hasFullRange`).
         */
        uint8 _bHasMinRange : 1;
        uint8 _bHasMaxRange : 1;
        uint8 _bReadOnly    : 1;
        /** @brief 부모 엘리먼트의 XML attribute 로 직렬화합니다(`PROPERTY( XMLAttribute )`). */
        uint8 _bXMLAttribute : 1;
        uint8 _bAssetPath    : 1;
        /** @brief 값이 `ReflectAny` 이거나, 타입과 blob 으로 이루어진 다형 페이로드입니다. */
        uint8 _bPolymorphic : 1;
        /** @brief 직렬화(JSON, XML, 바이너리, Diff)의 저장과 로드 대상에서 뺍니다(`Transient`, `NonSerialized`). */
        uint8 _bTransient : 1;
        /**
         * @brief 값이 비어 있으면 **쓸 때 생략**합니다(`PROPERTY( SkipIfEmpty )`).
         * @details 리플렉션 직렬화는 기본적으로 모든 PROPERTY 를 쓴다. 그래야 "파일에 없음"과 "명시적으로 비어 있음"을 구분할 수 있기 때문이다.
         *          그런데 선택적 필드(쓰지 않는 셰이더 스테이지의 진입점 같은 것)까지 모두 쓰면 파일을 읽기 어려워진다.
         *          이 플래그를 쓰면 스키마가 생략 여부를 직접 선언한다. 생략해도 된다고 선언한 필드만 생략하므로 모호함이 생기지 않는다.
         * @note 읽기에는 영향이 없다. 파일에 없으면 멤버 초기값이 그대로 남는다.
         */
        uint8 _bSkipIfEmpty : 1;
#if !defined( SW_SHIPPING )
        /** @brief 에디터 인스펙터에서 숨깁니다(`HideInInspector`). */
        uint8 _bHideInInspector : 1;
#else
        [[maybe_unused]] uint8 _reservedFlags : 1;
#endif
        /** @brief 네트워크 복제 대상입니다(`Replicated`, `RepNotify`). 네트워크 계층이 `PropertyRoleUtil::collectReplicatedProperties` 로 모읍니다. */
        uint8 _bReplicated : 1;
        /**
         * @brief 데이터 파일이 반드시 적어야 하는 칸입니다(`PROPERTY( Required )`). 빠지면 엄격한 읽기(`GameDataXML::readFields`)가 이름과 함께 오류를 냅니다.
         * @details 초기값은 형식일 뿐 기본값이 아니라는 표시다. 표시가 없는 칸은 초기값이 스키마 기본값이다.
         */
        uint8 _bRequired : 1;
        /**
         * @brief 세이브 대상입니다(`SaveGame`).
         * @details 타입(기반 포함)에 `SaveGame` 프로퍼티가 하나라도 있으면 세이브 직렬화(`SerializePurpose::SaveGame`)는 그 프로퍼티만 쓴다.
         *          하나도 없는 타입은 모든 프로퍼티를 쓴다(`TypeInfo::hasSaveGameProperty`).
         */
        uint8 _bSaveGame : 1;
        /** @brief 시퀀서 값 트랙이 섞을 수 있는 프로퍼티입니다(`Interp`). 숫자, float2, float3, float4, quaternion 만 허용하며 파서가 나머지를 막습니다. */
        uint8 _bInterp : 1;
        /**
         * @brief 플레이 중에만 의미가 있는 상태입니다(`RuntimeState`). 저작 저장(`SerializePurpose::Authoring`)은 쓰지 않고, 스냅샷과 세이브는 씁니다.
         * @details 판정 표는 `SerializerUtil::shouldSerializeProperty` 한 곳이다. 저작 파일을 읽을 때 이 칸이 있으면 로드 오류다(옛 데이터를 다시 쓴다).
         *          `Transient` · `Required` 와 함께 붙이면 파서가 멈춘다.
         */
        uint8                  _bRuntimeState : 1;
        [[maybe_unused]] uint8 _reservedRoles : 3;

        /** @brief 범위와 플래그를 끈 기본값으로 만듭니다. */
        PropertyMetadata() noexcept;

        /** @brief 아래 경계와 위 경계가 모두 있으면(슬라이더로 그릴 수 있으면) true 입니다. */
        bool hasFullRange() const noexcept { return _bHasMinRange != SW_FALSE && _bHasMaxRange != SW_FALSE; }

        /** @brief 허용 범위의 경계를 하나라도 적었는지(숫자 경계든 필드 경계든)입니다. 인스턴스 없이 묻습니다. */
        bool hasAllowedRange() const noexcept
        {
            return _bHasMinRange != SW_FALSE || _bHasMaxRange != SW_FALSE || _pReadMinRange != nullptr || _pReadMaxRange != nullptr;
        }

        /**
         * @brief 이 프로퍼티를 가진 객체 @p pOwner 에서 허용 범위를 풉니다. 숫자 경계는 그 값, 필드 경계는 그 필드의 지금 값입니다.
         * @details 인스펙터의 막기와 읽기 공통 끝(`SerializerUtil::isWithinDeclaredRanges`)이 이 함수 하나로 범위를 얻는다 — 둘이 따로 풀면 필드 경계를 한쪽만 지킨다.
         *          필드 경계가 있으면 @p pOwner 가 있어야 한다.
         */
        PropertyAllowedRange computeAllowedRange( const void* pOwner ) const noexcept;

        /** @brief 슬라이더 범위(`UIMin`, `UIMax`)가 모두 있으면 true 입니다. Shipping 빌드에는 없습니다. */
        bool hasFullUIRange() const noexcept
        {
#if !defined( SW_SHIPPING )
            return _bHasUIMinRange != SW_FALSE && _bHasUIMaxRange != SW_FALSE;
#else
            return false;
#endif
        }

        /** @brief 커스텀 메타데이터를 조회합니다. Shipping 빌드에서는 nullptr 를 반환합니다. */
        const string* findCustomMeta( const hashed_string& key ) const noexcept
        {
#if !defined( SW_SHIPPING )
            auto iter = _mapCustomMeta.find( key );
            return ( iter != _mapCustomMeta.end() ) ? &iter->second : nullptr;
#else
            (void)key;
            return nullptr;
#endif
        }
    };
} // namespace sw

namespace sw
{
    /// @brief `FUNCTION()` 에 적은 메타데이터입니다. 카테고리, NetRole, static, const 를 담습니다.
    struct SW_API FunctionMetadata
    {
#if !defined( SW_SHIPPING )
        string                               _category;
        string                               _displayName;
        string                               _tooltip;
        string                               _editorPreview; ///< `FUNCTION( EditorPreview = "..." )` 의 값이다. 에디터 뷰포트 미리보기가 이 값으로 메서드를 찾는다
        unordered_map<hashed_string, string> _mapCustomMeta;
#endif
        FunctionNetRole _netRole;
        uint8           _bReliable    : 1;
        uint8           _bValidate    : 1;
        uint8           _bConstructor : 1; ///< REFLECT 타입의 생성자 호출기다. objPtr 에 placement new 한다
        uint8           _bStatic      : 1; ///< C++ static 멤버 함수
        uint8           _bConst       : 1; ///< const 멤버 함수
#if !defined( SW_SHIPPING )
        uint8                  _bCallInEditor : 1; ///< 에디터 인스펙터에 실행 버튼을 보여 준다
        [[maybe_unused]] uint8 _reserved      : 2;
#else
        [[maybe_unused]] uint8 _reserved : 3;
#endif

        /** @brief 플래그를 끈 기본값으로 만듭니다. */
        FunctionMetadata() noexcept;

        /** @brief 커스텀 메타데이터를 조회합니다. Shipping 빌드에서는 nullptr 를 반환합니다. */
        const string* findCustomMeta( const hashed_string& key ) const noexcept
        {
#if !defined( SW_SHIPPING )
            auto iter = _mapCustomMeta.find( key );
            return ( iter != _mapCustomMeta.end() ) ? &iter->second : nullptr;
#else
            (void)key;
            return nullptr;
#endif
        }
    };
} // namespace sw
