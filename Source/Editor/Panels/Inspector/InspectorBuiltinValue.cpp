#include "pch.h"

#include "Editor/Panels/Inspector/InspectorBuiltinValue.h"

#include "Core/Container/ComponentHandle.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/TagID.h"
#include "Core/String/formatString.h"

#include "Engine/Reflection/ReflectionTypes.h"

namespace sw::editor
{
    namespace
    {
        struct InspectorBuiltinValueInternal
        {
            template <typename T>
            static TaskValue makeDefault()
            {
                return TaskValue{ T{} };
            }

            template <typename T>
            static const void* findTaskValue( const TaskValue& value )
            {
                return value.getPtr<T>();
            }

            /** @brief 내장 타입 값 하나를 글로 씁니다. 타입이 늘었는데 여기 갈래가 없으면 컴파일되지 않습니다. */
            template <typename T>
            static void formatValue( const void* pValue, utf8* pOutBuf, uint32 capacity )
            {
                const T& value = *static_cast<const T*>( pValue );
                if constexpr ( std::is_same_v<T, bool> )
                    formatstring( pOutBuf, capacity, "%#", value ? "true" : "false" );
                else if constexpr ( std::is_same_v<T, atomic<bool>> )
                    formatstring( pOutBuf, capacity, "%#", value.load( std::memory_order_relaxed ) ? "true" : "false" );
                else if constexpr ( std::is_arithmetic_v<T> )
                    formatstring( pOutBuf, capacity, "%#", value );
                else if constexpr ( std::is_same_v<T, string> || std::is_same_v<T, hashed_string> )
                    formatstring( pOutBuf, capacity, "%#", value.c_str() );
                else if constexpr ( std::is_same_v<T, float2> )
                    formatstring( pOutBuf, capacity, "(%#, %#)", value._x, value._y );
                else if constexpr ( std::is_same_v<T, float3> )
                    formatstring( pOutBuf, capacity, "(%#, %#, %#)", value._x, value._y, value._z );
                else if constexpr ( std::is_same_v<T, float4> || std::is_same_v<T, quaternion> )
                    formatstring( pOutBuf, capacity, "(%#, %#, %#, %#)", value._x, value._y, value._z, value._w );
                else if constexpr ( std::is_same_v<T, float4x4> )
                {
                    formatstring( pOutBuf, capacity, "[(%#, %#, %#, %#), (%#, %#, %#, %#), (%#, %#, %#, %#), (%#, %#, %#, %#)]",
                                  value._11, value._12, value._13, value._14, value._21, value._22, value._23, value._24,
                                  value._31, value._32, value._33, value._34, value._41, value._42, value._43, value._44 );
                }
                else if constexpr ( std::is_same_v<T, SlotHandle> )
                    formatstring( pOutBuf, capacity, "index %# / generation %#", value.index(), value.generation() );
                else if constexpr ( std::is_same_v<T, ComponentHandle> )
                    formatstring( pOutBuf, capacity, "object %# / component %#", value.objectId(), value.componentId() );
                else if constexpr ( std::is_same_v<T, GameObjectHandle> )
                    formatstring( pOutBuf, capacity, "object %#", value.objectId() );
                else if constexpr ( std::is_same_v<T, TagID> )
                    formatstring( pOutBuf, capacity, "%#", value.isValid() ? value.getString() : "(none)" );
                else
                    static_assert( sizeof( T ) == 0, "InspectorBuiltinValue: no text format for this builtin type" );
            }

            template <typename T>
            static InspectorBuiltinValue makeRow( const utf8* pTypeName )
            {
                InspectorBuiltinValue row;
                row._pTypeName      = pTypeName;
                row._pMakeDefault   = &makeDefault<T>;
                row._pFormatValue   = &formatValue<T>;
                row._pFindTaskValue = &findTaskValue<T>;
                row._widget         = InspectorWidgetFor<T>::kWidget;
                row._index          = 0;
                return row;
            }

            /** @brief `ReflectBuiltins.xxx` 의 내장 타입마다 한 줄입니다. */
            static vector<InspectorBuiltinValue> makeTable()
            {
                vector<InspectorBuiltinValue> listRow{
#define SW_REFLECT_BUILTIN_TYPE( Canon, CppType, TextConv, Ns, ... ) makeRow<InspectorBuiltinCppTypeT<CppType>>( #Canon ),
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"
#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER
                };
                for ( size_t index = 0; index < listRow.size(); ++index )
                    listRow[index]._index = static_cast<uint8>( index );
                return listRow;
            }

            static const vector<InspectorBuiltinValue>& getTable()
            {
                static const vector<InspectorBuiltinValue> s_listRow = makeTable();
                return s_listRow;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    uint32 InspectorBuiltinValueUtil::getBuiltinCount()
    {
        return static_cast<uint32>( InspectorBuiltinValueInternal::getTable().size() );
    }

    const InspectorBuiltinValue& InspectorBuiltinValueUtil::getBuiltin( uint32 index )
    {
        const vector<InspectorBuiltinValue>& listRow = InspectorBuiltinValueInternal::getTable();
        SW_ASSERT( index < listRow.size() );
        return listRow[index];
    }

    const InspectorBuiltinValue* InspectorBuiltinValueUtil::findBuiltin( string_view typeName )
    {
        for ( const InspectorBuiltinValue& row : InspectorBuiltinValueInternal::getTable() )
        {
            if ( typeName == row._pTypeName )
                return &row;
        }
        return nullptr;
    }

    bool InspectorBuiltinValueUtil::supportsMethodValue( string_view typeName )
    {
        return findBuiltin( typeName ) != nullptr;
    }

    bool InspectorBuiltinValueUtil::prepareMethodArgs( const FunctionInfo& method, vector<InspectorMethodArgSlot>& inoutListSlot )
    {
        const size_t paramCount = method._listParameter.size();
        bool         bAllFilled = paramCount <= kMaxMethodArgCount;
        inoutListSlot.resize( paramCount < kMaxMethodArgCount ? paramCount : kMaxMethodArgCount );
        for ( size_t paramIndex = 0; paramIndex < inoutListSlot.size(); ++paramIndex )
        {
            InspectorMethodArgSlot&      slot = inoutListSlot[paramIndex];
            const InspectorBuiltinValue* pRow = findBuiltin( method._listParameter[paramIndex]._typeName );
            if ( pRow == nullptr )
            {
                slot._value.reset();
                bAllFilled = false;
                continue;
            }
            const bool bTypeMatches = slot._value.hasValue() && slot._builtinIndex == pRow->_index;
            if ( bTypeMatches == false )
            {
                slot._value        = pRow->_pMakeDefault();
                slot._builtinIndex = pRow->_index;
            }
        }
        return bAllFilled;
    }

    TaskArgs InspectorBuiltinValueUtil::makeMethodArgs( const vector<InspectorMethodArgSlot>& listSlot )
    {
        TaskArgs args;
        for ( const InspectorMethodArgSlot& slot : listSlot )
            args.add( slot._value );
        return args;
    }

    bool InspectorBuiltinValueUtil::formatMethodResult( const TaskValue& value, string_view returnType, utf8* pOutBuf, uint32 capacity )
    {
        if ( pOutBuf == nullptr || capacity == 0 )
            return false;
        if ( returnType.empty() || returnType == "void" || value.hasValue() == false )
        {
            formatstring( pOutBuf, capacity, "(void / empty)" );
            return true;
        }

        const InspectorBuiltinValue* pRow = findBuiltin( returnType );
        if ( pRow == nullptr )
        {
            formatstring( pOutBuf, capacity, "(unsupported return: %#)", string( returnType ).c_str() );
            return false;
        }
        pRow->_pFormatValue( pRow->_pFindTaskValue( value ), pOutBuf, capacity );
        return true;
    }
} // namespace sw::editor
