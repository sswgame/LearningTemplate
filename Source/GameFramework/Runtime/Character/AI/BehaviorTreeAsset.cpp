#include "pch.h"

#include "GameFramework/Runtime/Character/AI/BehaviorTreeAsset.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Reflection/EnumName.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

#include "GameFramework/Runtime/Character/AI/BehaviorTaskRegistry.h"
#include "GameFramework/Runtime/Character/AI/BehaviorTreeNodeFields.h"

namespace sw
{
    namespace
    {
        struct BehaviorTreeAssetInternal
        {
            /** @brief 스키마 구조체 하나를 값 없이 넘기는 꼬리표입니다(`visitFields` 의 람다가 `typename decltype( tag )::Type` 으로 꺼낸다). */
            template <typename T>
            struct FieldsTag
            {
                using Type = T;
            };

            /**
             * @brief 종류 @p type 의 칸 스키마로 @p fn 을 부릅니다. 칸이 없는 종류(Inverter · ForceSuccess)면 부르지 않고 false 입니다.
             * @details 종류와 스키마의 짝은 이 switch 하나다 — 읽기 · 쓰기 · 인스펙터가 모두 이것을 지난다.
             */
            template <typename Fn>
            static bool visitFields( BehaviorNodeType type, Fn&& fn )
            {
                switch ( type )
                {
                    case BehaviorNodeType::Selector:
                        fn( FieldsTag<BehaviorSelectorFields>{} );
                        return true;
                    case BehaviorNodeType::Sequence:
                        fn( FieldsTag<BehaviorSequenceFields>{} );
                        return true;
                    case BehaviorNodeType::Repeat:
                        fn( FieldsTag<BehaviorRepeatFields>{} );
                        return true;
                    case BehaviorNodeType::Cooldown:
                    case BehaviorNodeType::TimeLimit:
                    case BehaviorNodeType::Wait:
                        fn( FieldsTag<BehaviorTimedFields>{} );
                        return true;
                    case BehaviorNodeType::BlackboardCondition:
                        fn( FieldsTag<BehaviorBlackboardConditionFields>{} );
                        return true;
                    case BehaviorNodeType::Action:
                    case BehaviorNodeType::Condition:
                        fn( FieldsTag<BehaviorTaskFields>{} );
                        return true;
                    case BehaviorNodeType::MoveTo:
                        fn( FieldsTag<BehaviorMoveToFields>{} );
                        return true;
                    case BehaviorNodeType::Inverter:
                    case BehaviorNodeType::ForceSuccess:
                        return false;
                }
                return false;
            }

            /** @brief 열거자 이름(리플렉션 `ENUM()`)을 대소문자 없이 찾습니다. 0 부터 @p last 까지 본다. */
            template <typename E>
            static bool parseEnumName( string_view text, E last, E& outValue )
            {
                for ( int32 value = 0; value <= static_cast<int32>( last ); ++value )
                {
                    if ( StringUtil::equals( getEnumName( static_cast<E>( value ) ), text, true ) )
                    {
                        outValue = static_cast<E>( value );
                        return true;
                    }
                }
                return false;
            }

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
            /** @brief 칸을 읽은 노드 하나를 트리에 더합니다. 작업 함수가 없는 Action · Condition 은 더하지 않고 -1 입니다(함수 없는 잎이 트리에 남지 않게 한다). */
            int32 addNode( const BehaviorTreeNodeData& data, const XMLNode& node, int32 parent, bool& inoutbValid );

        private:
            BehaviorTree& _tree;
            string_view   _sourceName;
        };

        int32 BehaviorTreeReader::addNode( const BehaviorTreeNodeData& data, const XMLNode& node, int32 parent, bool& inoutbValid )
        {
            const string name = data._name.empty() ? string( node.getName() ) : data._name; // 이름이 없으면 원소 이름(편집기 표시와 같다)
            switch ( data._type )
            {
                case BehaviorNodeType::Selector:
                    return _tree.addSelector( parent, name.c_str(), data._bReactive );
                case BehaviorNodeType::Sequence:
                    return _tree.addSequence( parent, name.c_str() );
                case BehaviorNodeType::Inverter:
                    return _tree.addInverter( parent );
                case BehaviorNodeType::ForceSuccess:
                    return _tree.addForceSuccess( parent );
                case BehaviorNodeType::Repeat:
                    return _tree.addRepeat( parent, data._count );
                case BehaviorNodeType::Cooldown:
                    return _tree.addCooldown( parent, data._seconds );
                case BehaviorNodeType::TimeLimit:
                    return _tree.addTimeLimit( parent, data._seconds );
                case BehaviorNodeType::Wait:
                    return _tree.addWait( parent, data._seconds );
                case BehaviorNodeType::BlackboardCondition:
                    return _tree.addBlackboardCondition( parent, hashed_string( data._key ), data._compare, data._value, data._abort );
                case BehaviorNodeType::MoveTo:
                    return _tree.addMoveTo( parent, hashed_string( data._key ), data._repathDistance );
                case BehaviorNodeType::Action:
                case BehaviorNodeType::Condition:
                    break;
            }
            // 작업 이름이 레지스트리에 있는지는 런타임만 본다 — 편집기는 아직 안 만든 작업의 트리도 연다(`BehaviorTreeEditModel::collectIssues`).
            const BehaviorTaskFunction function = BehaviorTaskRegistry::get().findTask( data._task );
            if ( function == nullptr )
            {
                SW_LOG_ERROR_ID( "BehaviorTreeAsset.NamesUnknownTaskRegistered", "%#: <%#> names the unknown task '%#' (registered: %#)", _sourceName, node.getName(), data._task.c_str(),
                                 BehaviorTaskRegistry::get().joinNames().c_str() );
                inoutbValid = false;
                return -1;
            }
            return data._type == BehaviorNodeType::Action ? _tree.addAction( parent, name.c_str(), function ) : _tree.addCondition( parent, name.c_str(), function );
        }

        bool BehaviorTreeReader::readNode( const XMLNode& node, int32 parent, uint32 depth )
        {
            if ( depth > BehaviorTreeNames::kMaxDepth )
            {
                SW_LOG_ERROR_ID( "BehaviorTreeAsset.NestedDeeperLevels", "%#: <%#> is nested deeper than %# levels", _sourceName, node.getName(), BehaviorTreeNames::kMaxDepth );
                return false;
            }
            BehaviorNodeType type{};
            if ( BehaviorTreeNames::parse( node.getName(), type ) == false )
            {
                SW_LOG_ERROR_ID( "BehaviorTreeAsset.UnknownBehaviorTreeNode", "%#: unknown behavior tree node <%#> (%#)", _sourceName, node.getName(), BehaviorTreeNames::joinNodeTypeNames().c_str() );
                return false;
            }

            BehaviorTreeNodeData data;
            bool                 bValid = BehaviorTreeXML::readNodeFields( node, type, _sourceName, data );
            const int32          index  = addNode( data, node, parent, bValid );
            if ( BehaviorTreeNames::isLeaf( type ) )
            {
                if ( node.findChild() )
                {
                    SW_LOG_ERROR_ID( "BehaviorTreeAsset.LeafCannotChildNodes", "%#: <%#> is a leaf and cannot have child nodes", _sourceName, node.getName() );
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
        return getEnumName( type );
    }

    const utf8* BehaviorTreeNames::toString( BlackboardCompare compare )
    {
        return getEnumName( compare );
    }

    const utf8* BehaviorTreeNames::toString( BehaviorAbortMode mode )
    {
        return getEnumName( mode );
    }

    bool BehaviorTreeNames::parse( string_view text, BehaviorNodeType& outValue )
    {
        return BehaviorTreeAssetInternal::parseEnumName( text, BehaviorNodeType::MoveTo, outValue );
    }

    bool BehaviorTreeNames::parse( string_view text, BlackboardCompare& outValue )
    {
        return BehaviorTreeAssetInternal::parseEnumName( text, BlackboardCompare::GreaterOrEqual, outValue );
    }

    bool BehaviorTreeNames::parse( string_view text, BehaviorAbortMode& outValue )
    {
        return BehaviorTreeAssetInternal::parseEnumName( text, BehaviorAbortMode::Both, outValue );
    }

    string BehaviorTreeNames::joinNodeTypeNames()
    {
        string text;
        for ( int32 value = 0; value <= static_cast<int32>( BehaviorNodeType::MoveTo ); ++value )
        {
            text += text.empty() ? "" : ", ";
            text += getEnumName( static_cast<BehaviorNodeType>( value ) );
        }
        return text;
    }

    bool BehaviorTreeNames::isLeaf( BehaviorNodeType type )
    {
        return BehaviorTreeAssetInternal::isLeaf( type );
    }

    bool BehaviorTreeNames::isDecorator( BehaviorNodeType type )
    {
        const bool bComposite = type == BehaviorNodeType::Selector || type == BehaviorNodeType::Sequence;
        return bComposite == false && BehaviorTreeAssetInternal::isLeaf( type ) == false;
    }
} // namespace sw

namespace sw
{
    bool BehaviorTreeXML::readNodeFields( const XMLNode& node, BehaviorNodeType type, string_view sourceName, BehaviorTreeNodeData& inoutData )
    {
        inoutData._type    = type;
        bool       bValid  = true;
        const bool bFields = BehaviorTreeAssetInternal::visitFields( type, [&]( auto tag )
        {
            using Fields = typename decltype( tag )::Type;
            Fields fields;
            bValid = XMLFields::readFields( node, fields, sourceName );
            bValid = SerializerUtil::copySameNamedProperties( &fields, *Fields::StaticType(), &inoutData, *BehaviorTreeNodeData::StaticType() ) && bValid;
        } );
        if ( bFields == false )
            bValid = XMLNameCheck::reportUnknownAttributes( node, nullptr, 0, sourceName, LogLevel::Error ); // 칸이 없는 종류 — 적힌 속성은 모두 오타다
        return bValid;
    }

    void BehaviorTreeXML::writeNodeFields( const XMLNode& node, const BehaviorTreeNodeData& data )
    {
        (void)BehaviorTreeAssetInternal::visitFields( data._type, [&]( auto tag )
        {
            using Fields = typename decltype( tag )::Type;
            Fields fields;
            // 이름이 같은 칸끼리 옮긴다 — 같은 형식이라 실패하지 않는다.
            (void)SerializerUtil::copySameNamedProperties( &data, *BehaviorTreeNodeData::StaticType(), &fields, *Fields::StaticType() );
            XMLFields::writeFields( node, fields );
        } );
    }

    bool BehaviorTreeXML::hasField( BehaviorNodeType type, string_view propertyName )
    {
        bool bFound = false;
        (void)BehaviorTreeAssetInternal::visitFields( type, [&]( auto tag )
        {
            using Fields = typename decltype( tag )::Type;
            for ( const PropertyInfo& prop : Fields::StaticType()->getPropertiesWithBase() )
            {
                bFound = bFound || prop._name.view() == propertyName;
            }
        } );
        return bFound;
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
            SW_LOG_ERROR_ID( "BehaviorTreeAsset.BehaviorTreeNoRoot", "%#: the behavior tree has no root node", sourceName );
            return 0;
        }
        if ( first.findNextSibling().isValid() )
        {
            SW_LOG_ERROR_ID( "BehaviorTreeAsset.BehaviorTreeMoreOne", "%#: the behavior tree has more than one root node - wrap them in a <Selector> or <Sequence>", sourceName );
            bValid = false;
        }
        BehaviorTreeReader reader( _tree, sourceName );
        bValid = reader.readNode( first, -1, 0 ) && bValid;
        if ( bValid && _tree.isValid() == false )
        {
            SW_LOG_ERROR_ID( "BehaviorTreeAsset.BehaviorTreeNotValid", "%#: the behavior tree is not valid (decorators need exactly one child, composites at least one)", sourceName );
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
