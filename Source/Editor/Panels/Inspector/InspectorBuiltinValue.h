/**
 * @file InspectorBuiltinValue.h
 * @brief 내장 값 타입(`ReflectBuiltins.xxx`)마다 인스펙터 위젯 종류와 CallInEditor 인자, 반환 처리를 정하는 테이블입니다.
 * @details ImGui 에 의존하지 않으므로 테스트를 붙일 수 있다. 테이블의 항목은 `ReflectBuiltins.xxx` 를 include 해서 만들므로, 내장 타입이
 *          하나 늘어나면 `InspectorWidgetFor` 특수화가 없는 한 컴파일되지 않는다. 그리는 쪽은 `InspectorPropertyManager.cpp` 이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/Reflection/ReflectValue.h"

namespace sw
{
    struct float2;
    struct float3;
    struct float4;
    struct float4x4;
    struct FunctionInfo;
    struct quaternion;
    struct TagID;

    class ComponentHandle;
    class GameObjectHandle;
    class SlotHandle;
} // namespace sw

namespace sw::editor
{
    /** @brief 인스펙터가 내장 값 타입 하나를 그리는 위젯 종류입니다. 프로퍼티와 CallInEditor 인자가 같은 종류를 씁니다. */
    enum class InspectorValueWidget : uint8
    {
        None,     ///< 위젯이 없다. 내장 타입에는 없어야 한다
        Number,   ///< 정수와 실수 드래그. 범위, 슬라이더, 단위 메타를 지원한다
        Checkbox, ///< bool 과 atomic<bool>
        Text,     ///< string. 애셋 경로 메타가 있으면 드롭 대상이 된다
        Name,     ///< hashed_string. Enter 로 확정하며 키 입력마다 인턴하지 않는다
        Vector2,  ///< float2
        Vector3,  ///< float3. 색 메타가 있으면 색 선택기를 쓴다
        Vector4,  ///< float4. 색 메타가 있으면 색 선택기를 쓴다
        Matrix,   ///< float4x4. 행 네 개로 그린다
        Rotation, ///< quaternion. 오일러 각(도)으로 보여 준다
        Handle,   ///< SlotHandle, ComponentHandle, GameObjectHandle. id 정수로 보여 준다
        Tag,      ///< TagID. Enter 로 확정한다
    };

    /**
     * @brief 내장 값 타입 하나의 위젯 종류입니다.
     * @details 기본 템플릿은 정의하지 않는다. `ReflectBuiltins.xxx` 에 타입이 늘어나면 여기에 특수화가 없는 한 테이블(.cpp)과
     *          위젯 등록(`InspectorPropertyManager::registerDefaults`)이 컴파일되지 않는다.
     */
    template <typename T>
    struct InspectorWidgetFor;

    template <InspectorValueWidget Widget>
    struct InspectorWidgetKind
    {
        static constexpr InspectorValueWidget kWidget = Widget;
    };
} // namespace sw::editor

namespace sw::editor
{
    // clang-format off
    template <> struct InspectorWidgetFor<int8>             : InspectorWidgetKind<InspectorValueWidget::Number> {};
    template <> struct InspectorWidgetFor<int16>            : InspectorWidgetKind<InspectorValueWidget::Number> {};
    template <> struct InspectorWidgetFor<int32>            : InspectorWidgetKind<InspectorValueWidget::Number> {};
    template <> struct InspectorWidgetFor<int64>            : InspectorWidgetKind<InspectorValueWidget::Number> {};
    template <> struct InspectorWidgetFor<uint8>            : InspectorWidgetKind<InspectorValueWidget::Number> {};
    template <> struct InspectorWidgetFor<uint16>           : InspectorWidgetKind<InspectorValueWidget::Number> {};
    template <> struct InspectorWidgetFor<uint32>           : InspectorWidgetKind<InspectorValueWidget::Number> {};
    template <> struct InspectorWidgetFor<uint64>           : InspectorWidgetKind<InspectorValueWidget::Number> {};
    template <> struct InspectorWidgetFor<float32>          : InspectorWidgetKind<InspectorValueWidget::Number> {};
    template <> struct InspectorWidgetFor<float64>          : InspectorWidgetKind<InspectorValueWidget::Number> {};
    template <> struct InspectorWidgetFor<bool>             : InspectorWidgetKind<InspectorValueWidget::Checkbox> {};
    template <> struct InspectorWidgetFor<atomic<bool>>     : InspectorWidgetKind<InspectorValueWidget::Checkbox> {};
    template <> struct InspectorWidgetFor<string>           : InspectorWidgetKind<InspectorValueWidget::Text> {};
    template <> struct InspectorWidgetFor<hashed_string>    : InspectorWidgetKind<InspectorValueWidget::Name> {};
    template <> struct InspectorWidgetFor<float2>           : InspectorWidgetKind<InspectorValueWidget::Vector2> {};
    template <> struct InspectorWidgetFor<float3>           : InspectorWidgetKind<InspectorValueWidget::Vector3> {};
    template <> struct InspectorWidgetFor<float4>           : InspectorWidgetKind<InspectorValueWidget::Vector4> {};
    template <> struct InspectorWidgetFor<float4x4>         : InspectorWidgetKind<InspectorValueWidget::Matrix> {};
    template <> struct InspectorWidgetFor<quaternion>       : InspectorWidgetKind<InspectorValueWidget::Rotation> {};
    template <> struct InspectorWidgetFor<SlotHandle>       : InspectorWidgetKind<InspectorValueWidget::Handle> {};
    template <> struct InspectorWidgetFor<ComponentHandle>  : InspectorWidgetKind<InspectorValueWidget::Handle> {};
    template <> struct InspectorWidgetFor<GameObjectHandle> : InspectorWidgetKind<InspectorValueWidget::Handle> {};
    template <> struct InspectorWidgetFor<TagID>            : InspectorWidgetKind<InspectorValueWidget::Tag> {};
    // clang-format on
} // namespace sw::editor

namespace sw::editor
{
    /** @brief `ReflectBuiltins.xxx` 의 C++ 타입 항목을 값이 실제로 쓰는 타입으로 바꿉니다(`std::string` → `sw::string`). 엔진의 테이블과 같습니다. */
    template <typename T>
    using InspectorBuiltinCppTypeT = ReflectBuiltinCppTypeT<T>;

    /** @brief 내장 값 타입 하나의 테이블 항목입니다. 항목 순서는 `ReflectBuiltins.xxx` 의 순서입니다. */
    struct InspectorBuiltinValue
    {
        const utf8* _pTypeName; ///< 정규 이름(`ReflectBuiltins.xxx` 의 첫 항목). 리플렉션이 기록하는 인자와 반환 타입 이름과 같다
        /** @brief 그 C++ 타입의 기본값입니다. CallInEditor 인자 입력란의 첫 값이며, 생성된 호출기가 `args.get<T>` 로 그대로 꺼냅니다. */
        TaskValue ( *_pMakeDefault )();
        /** @brief 그 C++ 타입 값(@p pValue)을 문자열로 만듭니다. 반환값 표시와 읽기 전용 프로퍼티가 함께 씁니다. */
        void ( *_pFormatValue )( const void* pValue, utf8* pOutBuf, uint32 capacity );
        /** @brief TaskValue 에 들어 있는 그 C++ 타입 값의 주소를 반환합니다. 비어 있으면 nullptr 입니다. */
        const void* ( *_pFindTaskValue )( const TaskValue& value );
        InspectorValueWidget _widget; ///< 프로퍼티와 인자를 그리는 위젯 종류
        uint8                _index;  ///< 테이블 안의 인덱스
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief CallInEditor 인자 입력란 하나입니다. 값은 항상 `_builtinIndex` 항목의 C++ 타입입니다. */
    struct InspectorMethodArgSlot
    {
        TaskValue _value;
        uint8     _builtinIndex{ 0 };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 내장 값 타입 테이블을 읽어 CallInEditor 의 인자 판정, 인자 묶기, 반환 형식화를 한 테이블로 처리합니다. */
    struct InspectorBuiltinValueUtil
    {
        /** @brief 인스펙터가 CallInEditor 인자 입력란을 그리는 최대 개수입니다. 넘는 메서드는 호출할 수 없습니다. */
        static constexpr uint32 kMaxMethodArgCount = 8;
        /** @brief 테이블의 항목 수입니다. `ReflectBuiltins.xxx` 의 내장 타입 수와 같습니다. 테이블을 따라 만드는 다른 배열이 static_assert 로 맞춥니다. */
        static constexpr uint32 kBuiltinCount = 0
#define SW_REFLECT_BUILTIN_TYPE( ... ) +1
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"
#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER
            ;

        /** @brief 테이블의 항목 수를 반환합니다. `kBuiltinCount` 와 같습니다. */
        static uint32 getBuiltinCount();
        /** @brief @p index 번째 항목을 반환합니다. */
        static const InspectorBuiltinValue& getBuiltin( uint32 index );
        /** @brief 정규 이름으로 항목을 찾습니다. 내장 타입이 아니면 nullptr 입니다. */
        static const InspectorBuiltinValue* findBuiltin( string_view typeName );

        /** @brief CallInEditor 인자나 반환으로 쓸 수 있는 타입이면 true 입니다. 인자와 반환이 같은 판정을 받습니다. */
        static bool supportsMethodValue( string_view typeName );
        /**
         * @brief @p method 의 인자 입력란을 맞춥니다. 입력란 수를 인자 수에 맞추고, 타입이 바뀐 입력란은 기본값으로 되돌립니다.
         * @return 모든 인자를 인스펙터가 채울 수 있으면 true 입니다. 인자가 `kMaxMethodArgCount` 를 넘거나 내장 타입이 아닌 인자가 있으면 false 입니다.
         */
        static bool prepareMethodArgs( const FunctionInfo& method, vector<InspectorMethodArgSlot>& inoutListSlot );
        /** @brief 인자 입력란들을 호출 인자로 묶습니다. 각 값은 인자의 C++ 타입 그대로 들어갑니다. */
        static TaskArgs makeMethodArgs( const vector<InspectorMethodArgSlot>& listSlot );
        /**
         * @brief 메서드 반환값을 문자열로 만듭니다.
         * @return 형식화했으면 true 입니다. 내장 타입이 아닌 반환은 "(unsupported return: …)" 를 쓰고 false 를 반환합니다.
         */
        static bool formatMethodResult( const TaskValue& value, string_view returnType, utf8* pOutBuf, uint32 capacity );
    };
} // namespace sw::editor
