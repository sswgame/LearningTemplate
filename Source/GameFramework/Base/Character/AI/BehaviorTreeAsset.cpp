#include "pch.h"

#include "GameFramework/Base/Character/AI/BehaviorTreeAsset.h"

#include "Core/Container/StringUtil.h"
#include "Core/String/EnumNameTable.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

#include "GameFramework/Base/Character/AI/BehaviorTaskRegistry.h"

#include <optional>

namespace sw
{
    SW_LOG_CALLER( "BehaviorTreeAsset" );

    namespace
    {
        struct BehaviorTreeAssetInternal
        {
            static constexpr const utf8* kArrNodeTypeName[] = { "Selector", "Sequence", "Inverter", "ForceSuccess", "Repeat", "Cooldown",
                                                                "TimeLimit", "BlackboardCondition", "Action", "Condition", "Wait", "MoveTo" };
            static constexpr const utf8* kArrCompareName[]  = { "IsSet", "IsNotSet", "Equal", "NotEqual", "Less", "LessOrEqual", "Greater", "GreaterOrEqual" };
            static constexpr const utf8* kArrAbortName[]    = { "None", "Self", "LowerPriority", "Both" };

            static constexpr const utf8* kArrSelectorAttr[]            = { "name", "reactive" };
            static constexpr const utf8* kArrNameAttr[]                = { "name" };
            static constexpr const utf8* kArrCountAttr[]               = { "count" };
            static constexpr const utf8* kArrSecondsAttr[]             = { "seconds" };
            static constexpr const utf8* kArrBlackboardConditionAttr[] = { "key", "compare", "value", "abort" };
            static constexpr const utf8* kArrTaskAttr[]                = { "task", "name" };
            static constexpr const utf8* kArrMoveToAttr[]              = { "key", "repathDistance" };

            static constexpr EnumNameTable<BehaviorNodeType>  kNodeTypeTable{ kArrNodeTypeName };
            static constexpr EnumNameTable<BlackboardCompare> kCompareTable{ kArrCompareName };
            static constexpr EnumNameTable<BehaviorAbortMode> kAbortTable{ kArrAbortName };
            static_assert( kNodeTypeTable.getCount() == static_cast<size_t>( BehaviorNodeType::MoveTo ) + 1, "node type names must cover every BehaviorNodeType" );
            static_assert( kCompareTable.getCount() == static_cast<size_t>( BlackboardCompare::GreaterOrEqual ) + 1, "compare names must cover every BlackboardCompare" );
            static_assert( kAbortTable.getCount() == static_cast<size_t>( BehaviorAbortMode::Both ) + 1, "abort names must cover every BehaviorAbortMode" );

            /** @brief 잎 노드(자식을 가질 수 없는 종류)인지 판정합니다. */
            static bool isLeaf( BehaviorNodeType type )
            {
                return type == BehaviorNodeType::Action || type == BehaviorNodeType::Condition || type == BehaviorNodeType::Wait || type == BehaviorNodeType::MoveTo;
            }
        };

        /** @brief 노드 원소 하나와 그 아래를 읽어 트리에 더하는 읽기 상태입니다. */
        class BehaviorTreeReader
        {
        public:
            BehaviorTreeReader( BehaviorTree& tree, string_view sourceName )
                : _tree{ tree }
                , _sourceName{ sourceName }
            {
            }

            /** @brief @p node 와 자손을 읽어 @p parent 아래에 더합니다. 하나라도 틀리면 false 이고, 읽을 수 있는 나머지는 계속 읽어 오류를 모두 알립니다. */
            [[nodiscard]] bool readNode( const XMLNode& node, int32 parent, uint32 depth );

        private:
            /** @brief 노드 종류의 속성 표(`BehaviorTreeNames::getAttributeNames`)에 없는 속성마다 오류를 남깁니다. */
            [[nodiscard]] bool checkAttributes( const XMLNode& node, BehaviorNodeType type ) const
            {
                const BehaviorTreeNames::AttributeList list = BehaviorTreeNames::getAttributeNames( type );
                return XMLNameCheck::reportUnknownAttributes( node, list._ppName, list._count, _sourceName, LogLevel::Error );
            }
            /** @brief 선택 속성 `name` 입니다(없으면 노드 종류 이름). */
            [[nodiscard]] bool readName( const XMLNode& node, string& outName ) const
            {
                std::optional<string> name;
                const bool            bRead = node.readOptionalAttribute<string>( "name", name );
                outName                     = name.has_value() ? *name : string( node.getName() );
                return bRead;
            }
            /** @brief 필수 실수 속성입니다. 없거나 깨지거나 음수면 false 입니다(오류를 남긴다). */
            [[nodiscard]] bool readSeconds( const XMLNode& node, const utf8* pAttribute, float32& outValue ) const
            {
                const std::optional<float32> value = node.readRequiredAttribute<float32>( pAttribute );
                if ( value.has_value() == false )
                    return false;
                if ( *value < 0.0f )
                {
                    SW_LOG_ERROR( "%#: <%#> attribute '%#' must not be negative (%#)", _sourceName, node.getName(), pAttribute, *value );
                    return false;
                }
                outValue = *value;
                return true;
            }
            /** @brief 열거 속성 하나를 이름으로 읽습니다. 없으면 false 를 돌려 주지 않고 @p inoutValue 를 그대로 둡니다(기본값). 모르는 이름이면 오류이고 false 입니다. */
            template <typename E>
            [[nodiscard]] bool readEnumName( const XMLNode& node, const utf8* pAttribute, const EnumNameTable<E>& table, E& inoutValue ) const
            {
                std::optional<string> text;
                if ( node.readOptionalAttribute<string>( pAttribute, text ) == false )
                    return false;
                if ( text.has_value() == false )
                    return true;
                if ( table.parse( *text, inoutValue ) )
                    return true;
                SW_LOG_ERROR( "%#: <%#> has an unknown %# '%#' (%#)", _sourceName, node.getName(), pAttribute, text->c_str(), table.joinNames().c_str() );
                return false;
            }
            /** @brief 속성 `task` 의 이름을 레지스트리에서 찾습니다. 없거나 모르는 이름이면 오류이고 nullptr 입니다. */
            BehaviorTaskFunction readTask( const XMLNode& node ) const
            {
                const std::optional<string> task = node.readRequiredAttribute<string>( "task" );
                if ( task.has_value() == false )
                    return nullptr;
                const BehaviorTaskFunction function = BehaviorTaskRegistry::get().findTask( *task );
                if ( function == nullptr )
                    SW_LOG_ERROR( "%#: <%#> names the unknown task '%#' (registered: %#)", _sourceName, node.getName(), task->c_str(), BehaviorTaskRegistry::get().joinNames().c_str() );
                return function;
            }
            /** @brief Repeat 한 개를 읽어 더합니다. `count` 가 없거나 음수면 @p inoutbValid 를 false 로 둡니다. */
            int32 addRepeat( const XMLNode& node, int32 parent, bool& inoutbValid )
            {
                const std::optional<int32> countValue = node.readRequiredAttribute<int32>( "count" );
                inoutbValid                           = countValue.has_value() && inoutbValid;
                if ( countValue.has_value() && *countValue < 0 )
                {
                    SW_LOG_ERROR( "%#: <Repeat> attribute 'count' must not be negative (%#)", _sourceName, *countValue );
                    inoutbValid = false;
                }
                return _tree.addRepeat( parent, countValue.value_or( 0 ) );
            }
            /** @brief `seconds` 하나로 만드는 노드(Cooldown · TimeLimit · Wait)를 읽어 더합니다. */
            int32 addTimed( const XMLNode& node, BehaviorNodeType type, int32 parent, bool& inoutbValid )
            {
                float32 seconds{ 0.0f };
                inoutbValid = readSeconds( node, "seconds", seconds ) && inoutbValid;
                switch ( type )
                {
                    case BehaviorNodeType::Cooldown:
                        return _tree.addCooldown( parent, seconds );
                    case BehaviorNodeType::TimeLimit:
                        return _tree.addTimeLimit( parent, seconds );
                    default:
                        return _tree.addWait( parent, seconds );
                }
            }
            /** @brief BlackboardCondition 한 개를 읽어 더합니다. */
            int32 addBlackboardCondition( const XMLNode& node, int32 parent, bool& inoutbValid )
            {
                using Internal                        = BehaviorTreeAssetInternal;
                const std::optional<string> key       = node.readRequiredAttribute<string>( "key" );
                BlackboardCompare           compare   = BlackboardCompare::IsSet;
                BehaviorAbortMode           abortMode = BehaviorAbortMode::None;
                std::optional<float32>      compareValue;
                inoutbValid = key.has_value() && inoutbValid;
                inoutbValid = readEnumName( node, "compare", Internal::kCompareTable, compare ) && inoutbValid;
                inoutbValid = readEnumName( node, "abort", Internal::kAbortTable, abortMode ) && inoutbValid;
                inoutbValid = node.readOptionalAttribute<float32>( "value", compareValue ) && inoutbValid;
                return _tree.addBlackboardCondition( parent, hashed_string( key.value_or( string{} ) ), compare, compareValue.value_or( 0.0f ), abortMode );
            }
            /** @brief Action · Condition 한 개를 읽어 더합니다. 함수가 없으면 노드를 더하지 않고 -1 입니다(함수 없는 잎이 트리에 남지 않게 한다). */
            int32 addTask( const XMLNode& node, BehaviorNodeType type, int32 parent, bool& inoutbValid )
            {
                string                     name;
                const BehaviorTaskFunction function = readTask( node );
                inoutbValid                         = function != nullptr && inoutbValid;
                inoutbValid                         = readName( node, name ) && inoutbValid;
                if ( function == nullptr )
                    return -1;
                return type == BehaviorNodeType::Action ? _tree.addAction( parent, name.c_str(), function ) : _tree.addCondition( parent, name.c_str(), function );
            }
            /** @brief MoveTo 한 개를 읽어 더합니다. */
            int32 addMoveTo( const XMLNode& node, int32 parent, bool& inoutbValid )
            {
                const std::optional<string> key = node.readRequiredAttribute<string>( "key" );
                std::optional<float32>      repathDistance;
                inoutbValid = key.has_value() && inoutbValid;
                inoutbValid = node.readOptionalAttribute<float32>( "repathDistance", repathDistance ) && inoutbValid;
                if ( repathDistance.has_value() && *repathDistance < 0.0f )
                {
                    SW_LOG_ERROR( "%#: <MoveTo> attribute 'repathDistance' must not be negative (%#)", _sourceName, *repathDistance );
                    inoutbValid = false;
                }
                return _tree.addMoveTo( parent, hashed_string( key.value_or( string{} ) ), repathDistance.value_or( 0.5f ) );
            }

            BehaviorTree& _tree;
            string_view   _sourceName;
        };

        bool BehaviorTreeReader::readNode( const XMLNode& node, int32 parent, uint32 depth )
        {
            using Internal = BehaviorTreeAssetInternal;
            if ( depth > BehaviorTreeNames::kMaxDepth )
            {
                SW_LOG_ERROR( "%#: <%#> is nested deeper than %# levels", _sourceName, node.getName(), BehaviorTreeNames::kMaxDepth );
                return false;
            }
            BehaviorNodeType type{};
            if ( Internal::kNodeTypeTable.parse( node.getName(), type ) == false )
            {
                SW_LOG_ERROR( "%#: unknown behavior tree node <%#> (%#)", _sourceName, node.getName(), Internal::kNodeTypeTable.joinNames().c_str() );
                return false;
            }

            bool       bValid = checkAttributes( node, type );
            int32      index  = -1;
            string     name;
            const bool bNamed = type == BehaviorNodeType::Selector || type == BehaviorNodeType::Sequence;
            if ( bNamed )
                bValid = readName( node, name ) && bValid;
            switch ( type )
            {
                case BehaviorNodeType::Selector:
                {
                    std::optional<bool> bReactive;
                    bValid = node.readOptionalAttribute<bool>( "reactive", bReactive ) && bValid;
                    index  = _tree.addSelector( parent, name.c_str(), bReactive.value_or( false ) );
                    break;
                }
                case BehaviorNodeType::Sequence:
                {
                    index = _tree.addSequence( parent, name.c_str() );
                    break;
                }
                case BehaviorNodeType::Inverter:
                {
                    index = _tree.addInverter( parent );
                    break;
                }
                case BehaviorNodeType::ForceSuccess:
                {
                    index = _tree.addForceSuccess( parent );
                    break;
                }
                case BehaviorNodeType::Repeat:
                {
                    index = addRepeat( node, parent, bValid );
                    break;
                }
                case BehaviorNodeType::Cooldown:
                case BehaviorNodeType::TimeLimit:
                case BehaviorNodeType::Wait:
                {
                    index = addTimed( node, type, parent, bValid );
                    break;
                }
                case BehaviorNodeType::BlackboardCondition:
                {
                    index = addBlackboardCondition( node, parent, bValid );
                    break;
                }
                case BehaviorNodeType::Action:
                case BehaviorNodeType::Condition:
                {
                    index = addTask( node, type, parent, bValid );
                    break;
                }
                case BehaviorNodeType::MoveTo:
                {
                    index = addMoveTo( node, parent, bValid );
                    break;
                }
            }

            if ( Internal::isLeaf( type ) )
            {
                if ( node.findChild() )
                {
                    SW_LOG_ERROR( "%#: <%#> is a leaf and cannot have child nodes", _sourceName, node.getName() );
                    bValid = false;
                }
                return bValid;
            }
            if ( index < 0 )
                return false; // 노드를 더하지 못했다 - 아래를 읽을 부모가 없다
            for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
            {
                bValid = readNode( child, index, depth + 1 ) && bValid;
            }
            return bValid;
        }
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* BehaviorTreeNames::toString( BehaviorNodeType type )
    {
        return BehaviorTreeAssetInternal::kNodeTypeTable.toString( type );
    }

    const utf8* BehaviorTreeNames::toString( BlackboardCompare compare )
    {
        return BehaviorTreeAssetInternal::kCompareTable.toString( compare );
    }

    const utf8* BehaviorTreeNames::toString( BehaviorAbortMode mode )
    {
        return BehaviorTreeAssetInternal::kAbortTable.toString( mode );
    }

    bool BehaviorTreeNames::parse( string_view text, BehaviorNodeType& outValue )
    {
        return BehaviorTreeAssetInternal::kNodeTypeTable.parse( text, outValue );
    }

    bool BehaviorTreeNames::parse( string_view text, BlackboardCompare& outValue )
    {
        return BehaviorTreeAssetInternal::kCompareTable.parse( text, outValue );
    }

    bool BehaviorTreeNames::parse( string_view text, BehaviorAbortMode& outValue )
    {
        return BehaviorTreeAssetInternal::kAbortTable.parse( text, outValue );
    }

    string BehaviorTreeNames::joinNodeTypeNames()
    {
        return BehaviorTreeAssetInternal::kNodeTypeTable.joinNames();
    }

    bool BehaviorTreeNames::isLeaf( BehaviorNodeType type )
    {
        return BehaviorTreeAssetInternal::isLeaf( type );
    }

    BehaviorTreeNames::AttributeList BehaviorTreeNames::getAttributeNames( BehaviorNodeType type )
    {
        using Internal = BehaviorTreeAssetInternal;
        switch ( type )
        {
            case BehaviorNodeType::Selector:
                return { Internal::kArrSelectorAttr, 2 };
            case BehaviorNodeType::Sequence:
                return { Internal::kArrNameAttr, 1 };
            case BehaviorNodeType::Inverter:
            case BehaviorNodeType::ForceSuccess:
                return {};
            case BehaviorNodeType::Repeat:
                return { Internal::kArrCountAttr, 1 };
            case BehaviorNodeType::Cooldown:
            case BehaviorNodeType::TimeLimit:
            case BehaviorNodeType::Wait:
                return { Internal::kArrSecondsAttr, 1 };
            case BehaviorNodeType::BlackboardCondition:
                return { Internal::kArrBlackboardConditionAttr, 4 };
            case BehaviorNodeType::Action:
            case BehaviorNodeType::Condition:
                return { Internal::kArrTaskAttr, 2 };
            case BehaviorNodeType::MoveTo:
                return { Internal::kArrMoveToAttr, 2 };
        }
        return {};
    }

    bool BehaviorTreeNames::isDecorator( BehaviorNodeType type )
    {
        const bool bComposite = type == BehaviorNodeType::Selector || type == BehaviorNodeType::Sequence;
        return bComposite == false && BehaviorTreeAssetInternal::isLeaf( type ) == false;
    }
} // namespace sw

namespace sw
{
    BehaviorTreeAsset::BehaviorTreeAsset()
        : _tree{}
    {
    }

    uint32 BehaviorTreeAsset::loadRoot( const XMLNode& root, string_view sourceName )
    {
        _tree                = BehaviorTree{};
        bool          bValid = XMLNameCheck::reportUnknownAttributes( root, nullptr, 0, sourceName, LogLevel::Error );
        const XMLNode first  = root.findChild();
        if ( first.isValid() == false )
        {
            SW_LOG_ERROR( "%#: the behavior tree has no root node", sourceName );
            return 0;
        }
        if ( first.findNextSibling().isValid() )
        {
            SW_LOG_ERROR( "%#: the behavior tree has more than one root node - wrap them in a <Selector> or <Sequence>", sourceName );
            bValid = false;
        }
        BehaviorTreeReader reader( _tree, sourceName );
        bValid = reader.readNode( first, -1, 0 ) && bValid;
        if ( bValid && _tree.isValid() == false )
        {
            SW_LOG_ERROR( "%#: the behavior tree is not valid (decorators need exactly one child, composites at least one)", sourceName );
            bValid = false;
        }
        if ( bValid == false )
        {
            _tree = BehaviorTree{};
            return 0;
        }
        return static_cast<uint32>( _tree.getNodeCount() );
    }
} // namespace sw
