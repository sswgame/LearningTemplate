/**
 * @file InspectorBuiltinValue.h
 * @brief 내장 값 타입(`ReflectBuiltins.xxx`)마다 인스펙터 위젯 갈래와 CallInEditor 인자 · 반환 처리를 정하는 표입니다.
 * @details ImGui 에 의존하지 않아 시험을 붙일 수 있습니다. 표의 줄은 `ReflectBuiltins.xxx` 를 include 해서 만들므로, 내장 타입이
 *          하나 늘면 `InspectorWidgetFor` 특수화가 없는 한 컴파일되지 않습니다. 그리는 쪽은 `InspectorPropertyManager.cpp` 입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"
#include "Core/Task/TaskTypes.h"

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
    /** @brief 인스펙터가 내장 값 타입 하나를 그리는 위젯 갈래입니다. 프로퍼티와 CallInEditor 인자가 같은 갈래를 씁니다. */
    enum class InspectorValueWidget : uint8
    {
        None,     ///< 위젯 없음 — 내장 타입에는 없어야 한다
        Number,   ///< 정수 · 실수 드래그(범위 · 슬라이더 · 단위 메타)
        Checkbox, ///< bool · atomic<bool>
        Text,     ///< string(애셋 경로 메타면 드롭 대상)
        Name,     ///< hashed_string — Enter 로 확정(키 입력마다 인턴하지 않는다)
        Vector2,  ///< float2
        Vector3,  ///< float3(색 메타면 색 선택기)
        Vector4,  ///< float4(색 메타면 색 선택기)
        Matrix,   ///< float4x4 — 행 넷
        Rotation, ///< quaternion — 오일러 각(도)
        Handle,   ///< SlotHandle · ComponentHandle · GameObjectHandle — id 정수
        Tag,      ///< TagID — Enter 로 확정
    };

    /**
     * @brief 내장 값 타입 하나의 위젯 갈래입니다.
     * @details 기본 템플릿은 정의하지 않습니다. `ReflectBuiltins.xxx` 에 타입이 늘면 여기 특수화가 없는 한 표(.cpp)와
     *          위젯 등록(`InspectorPropertyManager::registerDefaults`)이 컴파일되지 않습니다.
     */
    template <typename T>
    struct InspectorWidgetFor;

    template <InspectorValueWidget Widget>
    struct InspectorWidgetKind
    {
        static constexpr InspectorValueWidget kWidget = Widget;
    };

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

    /**
     * @brief `ReflectBuiltins.xxx` 의 C++ 타입 칸을 값이 실제로 쓰는 타입으로 바꿉니다.
     * @details 문자열 줄의 칸은 `std::string` 이지만 프로퍼티와 생성 호출기(`args.get<string>`)는 `sw::string` 입니다
     *          (STL 컨테이너 빌드가 아니면 둘은 다른 타입이고 크기도 다릅니다).
     */
    template <typename T>
    struct InspectorBuiltinCppType
    {
        using Type = T;
    };

    template <>
    struct InspectorBuiltinCppType<std::string>
    {
        using Type = string;
    };

    template <typename T>
    using InspectorBuiltinCppTypeT = typename InspectorBuiltinCppType<T>::Type;

    /** @brief 내장 값 타입 하나의 표 한 줄입니다. 줄 순서는 `ReflectBuiltins.xxx` 의 순서입니다. */
    struct InspectorBuiltinValue
    {
        const utf8* _pTypeName; ///< 정규 이름(`ReflectBuiltins.xxx` 첫 칸). 리플렉션이 적는 인자 · 반환 타입 이름과 같다
        /** @brief 그 C++ 타입의 기본값입니다. CallInEditor 인자 칸의 첫 값이며, 생성 호출기가 `args.get<T>` 로 그대로 꺼냅니다. */
        TaskValue ( *_pMakeDefault )();
        /** @brief 그 C++ 타입 값(@p pValue)을 글로 씁니다. 반환값 표시와 읽기 전용 프로퍼티가 같이 씁니다. */
        void ( *_pFormatValue )( const void* pValue, utf8* pOutBuf, uint32 capacity );
        /** @brief TaskValue 에 든 그 C++ 타입 값의 자리입니다. 비었으면 nullptr 입니다. */
        const void* ( *_pFindTaskValue )( const TaskValue& value );
        InspectorValueWidget _widget; ///< 프로퍼티 · 인자를 그리는 위젯 갈래
        uint8                _index;  ///< 표 안의 자리
    };

    /** @brief CallInEditor 인자 칸 하나입니다. 값은 늘 `_builtinIndex` 줄의 C++ 타입입니다. */
    struct InspectorMethodArgSlot
    {
        TaskValue _value;
        uint8     _builtinIndex{ 0 };
    };

    /** @brief 내장 값 타입 표를 읽고, CallInEditor 의 인자 판정 · 인자 묶기 · 반환 형식화를 한 표로 합니다. */
    struct InspectorBuiltinValueUtil
    {
        /** @brief 인스펙터가 CallInEditor 인자 칸을 그리는 최대 개수입니다. 넘는 메서드는 부를 수 없습니다. */
        static constexpr uint32 kMaxMethodArgCount = 8;
        /** @brief 표의 줄 수(= `ReflectBuiltins.xxx` 의 내장 타입 수)입니다. 표를 따라 만드는 다른 배열이 static_assert 로 맞춥니다. */
        static constexpr uint32 kBuiltinCount = 0
#define SW_REFLECT_BUILTIN_TYPE( ... ) +1
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"
#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER
            ;

        /** @brief 표의 줄 수입니다(`kBuiltinCount` 와 같습니다). */
        static uint32 getBuiltinCount();
        /** @brief @p index 줄입니다. */
        static const InspectorBuiltinValue& getBuiltin( uint32 index );
        /** @brief 정규 이름으로 줄을 찾습니다. 내장 타입이 아니면 nullptr 입니다. */
        static const InspectorBuiltinValue* findBuiltin( string_view typeName );

        /** @brief CallInEditor 인자 · 반환으로 쓸 수 있는 타입인지 — 인자와 반환이 같은 판정을 받습니다. */
        static bool supportsMethodValue( string_view typeName );
        /**
         * @brief @p method 의 인자 칸을 맞춥니다. 칸 수를 인자 수로 맞추고, 타입이 바뀐 칸은 기본값으로 되돌립니다.
         * @return 모든 인자를 인스펙터가 채울 수 있으면 true(인자가 `kMaxMethodArgCount` 를 넘거나 내장 타입이 아닌 인자가 있으면 false).
         */
        static bool prepareMethodArgs( const FunctionInfo& method, vector<InspectorMethodArgSlot>& inoutListSlot );
        /** @brief 인자 칸들을 호출 인자로 묶습니다. 각 값은 인자의 C++ 타입 그대로 들어갑니다. */
        static TaskArgs makeMethodArgs( const vector<InspectorMethodArgSlot>& listSlot );
        /**
         * @brief 메서드 반환값을 글로 씁니다.
         * @return 형식화했으면 true. 내장 타입이 아닌 반환은 "(unsupported return: …)" 를 쓰고 false 입니다.
         */
        static bool formatMethodResult( const TaskValue& value, string_view returnType, utf8* pOutBuf, uint32 capacity );
    };
} // namespace sw::editor
