#include "pch.h"

#include "GameFramework/Runtime/Character/AI/BehaviorTreeAsset.h"

#include "Core/String/StringUtil.h"

#include "Engine/Behavior/ActionRegistry.h"
#include "Engine/Behavior/BehaviorTreeSchema.h"
#include "Engine/Behavior/ConditionExpr.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

#include "GameFramework/Runtime/Character/AI/BehaviorTreeNodeParams.h"

namespace sw
{
    namespace
    {
        struct BehaviorTreeAssetInternal
        {
            /** @brief 원소에 적을 수 있는 공통 속성입니다. 칸 구조체가 아니라 노드가 직접 듭니다. */
            static bool isHandRead( const utf8* pName )
            {
                return StringUtil::equals( pName, "id" ) || StringUtil::equals( pName, "name" );
            }

            /** @brief 작업 노드에서 동작 칸이 아닌 속성입니다. */
            static bool isTaskOwnAttribute( const utf8* pName )
            {
                return isHandRead( pName ) || StringUtil::equals( pName, "task" );
            }

            /** @brief 작업 노드의 스키마를 읽을 때 `task` 말고는 모두 동작의 칸이다. */
            static bool isNotTaskField( const utf8* pName )
            {
                return StringUtil::equals( pName, "task" ) == false;
            }

            /** @brief 종류와 스키마의 짝은 이 switch 하나다 — 읽기 · 쓰기 · 인스펙터가 모두 이것을 지난다. */
            static const InstancedStructOps& findFieldsOps( BehaviorNodeKind type )
            {
                switch ( type )
                {
                    case BehaviorNodeKind::Selector:
                        return InstancedStructOps::of<BehaviorSelectorParams>();
                    case BehaviorNodeKind::Repeat:
                        return InstancedStructOps::of<BehaviorRepeatParams>();
                    case BehaviorNodeKind::Cooldown:
                    case BehaviorNodeKind::TimeLimit:
                    case BehaviorNodeKind::Wait:
                        return InstancedStructOps::of<BehaviorTimedParams>();
                    case BehaviorNodeKind::BlackboardCondition:
                        return InstancedStructOps::of<BehaviorConditionDecoratorParams>();
                    case BehaviorNodeKind::Condition:
                        return InstancedStructOps::of<BehaviorConditionParams>();
                    case BehaviorNodeKind::Action:
                        return InstancedStructOps::of<BehaviorActionParams>();
                    case BehaviorNodeKind::MoveTo:
                        return InstancedStructOps::of<BehaviorMoveToParams>();
                    case BehaviorNodeKind::SubTree:
                        return InstancedStructOps::of<BehaviorSubTreeParams>();
                    case BehaviorNodeKind::Sequence:
                    case BehaviorNodeKind::Inverter:
                    case BehaviorNodeKind::ForceSuccess:
                        break;
                }
                return InstancedStructOps::of<VocabularyNoParams>();
            }

            /** @brief 열거자 이름(리플렉션 `ENUM()`)을 그대로 찾습니다. 0 부터 @p last 까지 본다. */
            template <typename E>
            static bool parseEnumName( string_view text, E last, E& outValue )
            {
                for ( int32 value = 0; value <= static_cast<int32>( last ); ++value )
                {
                    if ( StringUtil::equals( getEnumName( static_cast<E>( value ) ), text ) )
                    {
                        outValue = static_cast<E>( value );
                        return true;
                    }
                }
                return false;
            }

            /** @brief 잎 노드(자식을 가질 수 없는 종류)인지 판정합니다. */
            static bool isLeaf( BehaviorNodeKind type )
            {
                return type == BehaviorNodeKind::Action || type == BehaviorNodeKind::Condition || type == BehaviorNodeKind::Wait || type == BehaviorNodeKind::MoveTo || type == BehaviorNodeKind::SubTree;
            }
        };

        /** @brief 노드 원소 하나와 그 아래를 읽어 트리에 더하는 읽기 상태입니다. */
        class BehaviorTreeReader
        {
        public:
            BehaviorTreeReader( BehaviorTree& tree, string_view sourceName, IDataInheritanceSource& source )
                : _tree{ tree }
                , _source{ source }
                , _sourceName{ sourceName }
                , _listChain{}
            {
            }

            /** @brief @p node 와 자손을 읽어 @p parent 아래에 더합니다. 하나라도 틀리면 false 이고, 읽을 수 있는 나머지는 계속 읽어 오류를 모두 알립니다. */
            [[nodiscard]] bool readNode( const XMLNode& node, optional<int32> parent, uint32 depth );

        private:
            /** @brief 칸을 읽은 노드 하나를 트리에 더합니다. 틀린 식 · 모르는 동작이면 더하지 않고 오류입니다(이유는 로그). */
            [[nodiscard]] expected<int32> addNode( const BehaviorTreeNodeData& data, const XMLNode& node, optional<int32> parent, bool& inoutbValid );
            /** @brief `<SubTree tree="…">` 를 그 자리에 펼칩니다. 순환 · 없는 파일 · 틀린 파일은 오류입니다. */
            [[nodiscard]] bool expandSubTree( const string& treePath, optional<int32> parent, uint32 depth );

        private:
            BehaviorTree&           _tree;
            IDataInheritanceSource& _source;
            string_view             _sourceName;
            vector<string>          _listChain; ///< 펼치는 중인 하위 트리 파일(순환 검사)
        };

        bool BehaviorTreeReader::expandSubTree( const string& treePath, optional<int32> parent, uint32 depth )
        {
            for ( const string& active : _listChain )
            {
                if ( active == treePath )
                {
                    SW_LOG_ERROR_ID( "BehaviorTreeAsset.SubTreeCycle", "%#: <SubTree tree=\"%#\"> includes itself", _sourceName, treePath.c_str() );
                    return false;
                }
            }
            string text;
            if ( _source.readText( treePath, text ) == false )
            {
                SW_LOG_ERROR_ID( "BehaviorTreeAsset.SubTreeNotFound", "%#: <SubTree tree=\"%#\"> cannot be read", _sourceName, treePath.c_str() );
                return false;
            }
            string flatText;
            if ( DataInheritance::flatten( text, treePath, BehaviorTreeSchema::make(), _source, flatText ) == false )
                return false;
            XMLDocument document;
            if ( document.parse( flatText, treePath ) == false )
            {
                SW_LOG_ERROR_ID( "BehaviorTreeAsset.SubTreeUnreadable", "%#: <SubTree tree=\"%#\">: %#", _sourceName, treePath.c_str(), document.getLastError().c_str() );
                return false;
            }
            const XMLNode root  = document.getRoot( "BehaviorTree" );
            const XMLNode first = root.isValid() ? root.findChild() : XMLNode{};
            if ( first.isValid() == false || first.findNextSibling().isValid() )
            {
                SW_LOG_ERROR_ID( "BehaviorTreeAsset.SubTreeNeedsOneRoot", "%#: <SubTree tree=\"%#\"> needs a <BehaviorTree> with exactly one root node", _sourceName, treePath.c_str() );
                return false;
            }
            _listChain.push_back( treePath );
            const bool bValid = readNode( first, parent, depth );
            _listChain.pop_back();
            return bValid;
        }

        expected<int32> BehaviorTreeReader::addNode( const BehaviorTreeNodeData& data, const XMLNode& node, optional<int32> parent, bool& inoutbValid )
        {
            const string name = data._name.empty() ? string( node.getName() ) : data._name; // 이름이 없으면 원소 이름(편집기 표시와 같다)
            switch ( data._type )
            {
                case BehaviorNodeKind::Selector:
                    return _tree.addSelector( parent, name.c_str(), data._params.find<BehaviorSelectorParams>()->_bReactive );
                case BehaviorNodeKind::Sequence:
                    return _tree.addSequence( parent, name.c_str() );
                case BehaviorNodeKind::Inverter:
                    return _tree.addInverter( parent );
                case BehaviorNodeKind::ForceSuccess:
                    return _tree.addForceSuccess( parent );
                case BehaviorNodeKind::Repeat:
                    return _tree.addRepeat( parent, data._params.find<BehaviorRepeatParams>()->_count );
                case BehaviorNodeKind::Cooldown:
                    return _tree.addCooldown( parent, data._params.find<BehaviorTimedParams>()->_seconds );
                case BehaviorNodeKind::TimeLimit:
                    return _tree.addTimeLimit( parent, data._params.find<BehaviorTimedParams>()->_seconds );
                case BehaviorNodeKind::Wait:
                    return _tree.addWait( parent, data._params.find<BehaviorTimedParams>()->_seconds );
                case BehaviorNodeKind::MoveTo:
                {
                    const BehaviorMoveToParams* pParams = data._params.find<BehaviorMoveToParams>();
                    return _tree.addMoveTo( parent, hashed_string( pParams->_key ), pParams->_repathDistance );
                }
                case BehaviorNodeKind::BlackboardCondition:
                {
                    const BehaviorConditionDecoratorParams* pParams = data._params.find<BehaviorConditionDecoratorParams>();
                    ConditionExpr                           condition;
                    if ( ConditionExpr::compile( pParams->_test, _sourceName, condition ) )
                        return _tree.addConditionDecorator( parent, name.c_str(), condition, pParams->_abort );
                    break;
                }
                case BehaviorNodeKind::Condition:
                {
                    ConditionExpr condition;
                    if ( ConditionExpr::compile( data._params.find<BehaviorConditionParams>()->_test, _sourceName, condition ) )
                        return _tree.addCondition( parent, name.c_str(), condition );
                    break;
                }
                case BehaviorNodeKind::Action:
                {
                    // 동작 이름이 있는지는 런타임만 본다 — 편집기는 아직 안 만든 동작의 트리도 연다(`BehaviorTreeEditModel::collectIssues`).
                    const string&            task   = data._params.find<BehaviorActionParams>()->_task;
                    const ActionEntry* const pEntry = ActionRegistry::find( task );
                    if ( pEntry != nullptr && data._actionParams.isValid() )
                        return _tree.addAction( parent, name.c_str(), GameActionCall::make( *pEntry, data._actionParams ) );
                    SW_LOG_ERROR_ID( "BehaviorTreeAsset.NamesUnknownTaskRegistered", "%#: <%#> names the unknown task '%#' (registered: %#)", _sourceName, node.getName(), task.c_str(), ActionRegistry::joinNames().c_str() );
                    break;
                }
                case BehaviorNodeKind::SubTree:
                    return _tree.addSubTreeSlot( parent, name.c_str(), hashed_string( data._params.find<BehaviorSubTreeParams>()->_slot ) );
            }
            inoutbValid = false;
            return unexpected( Error{ ErrorKind::InvalidArgument, "the behavior tree node was not added" } ); // 이유는 위에서 로그로 남겼다
        }

        bool BehaviorTreeReader::readNode( const XMLNode& node, optional<int32> parent, uint32 depth )
        {
            if ( depth > BehaviorTreeNames::kMaxDepth )
            {
                SW_LOG_ERROR_ID( "BehaviorTreeAsset.NestedDeeperLevels", "%#: <%#> is nested deeper than %# levels", _sourceName, node.getName(), BehaviorTreeNames::kMaxDepth );
                return false;
            }
            BehaviorNodeKind type{};
            if ( BehaviorTreeNames::parse( node.getName(), type ) == false )
            {
                SW_LOG_ERROR_ID( "BehaviorTreeAsset.UnknownBehaviorTreeNode", "%#: unknown behavior tree node <%#> (%#)", _sourceName, node.getName(), BehaviorTreeNames::joinNodeTypeNames().c_str() );
                return false;
            }

            BehaviorTreeNodeData data;
            bool                 bValid = BehaviorTreeXML::readNodeFields( node, type, _sourceName, data );
            if ( bValid == false )
                return false; // 칸이 틀린 노드는 뜻을 모른다 - 스키마 값을 믿고 더하지 않는다
            if ( type == BehaviorNodeKind::SubTree )
            {
                const BehaviorSubTreeParams* pParams = data._params.find<BehaviorSubTreeParams>();
                if ( pParams->_tree.empty() == pParams->_slot.empty() )
                {
                    SW_LOG_ERROR_ID( "BehaviorTreeAsset.SubTreeNeedsOne", "%#: <SubTree> needs exactly one of tree / slot", _sourceName );
                    return false;
                }
                if ( pParams->_tree.empty() == false )
                    return expandSubTree( pParams->_tree.path(), parent, depth );
            }
            const expected<int32> index = addNode( data, node, parent, bValid );
            if ( BehaviorTreeNames::isLeaf( type ) )
            {
                if ( node.findChild() )
                {
                    SW_LOG_ERROR_ID( "BehaviorTreeAsset.LeafCannotChildNodes", "%#: <%#> is a leaf and cannot have child nodes", _sourceName, node.getName() );
                    bValid = false;
                }
                return bValid;
            }
            if ( index.has_value() == false )
                return false; // 노드를 더하지 못했다 - 아래를 읽을 부모가 없다
            for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
            {
                bValid = readNode( child, *index, depth + 1 ) && bValid;
            }
            return bValid;
        }
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* BehaviorTreeNames::toString( BehaviorNodeKind type )
    {
        return getEnumName( type );
    }

    const utf8* BehaviorTreeNames::toString( BehaviorAbortMode mode )
    {
        return getEnumName( mode );
    }

    bool BehaviorTreeNames::parse( string_view text, BehaviorNodeKind& outValue )
    {
        return BehaviorTreeAssetInternal::parseEnumName( text, BehaviorNodeKind::SubTree, outValue );
    }

    bool BehaviorTreeNames::parse( string_view text, BehaviorAbortMode& outValue )
    {
        return BehaviorTreeAssetInternal::parseEnumName( text, BehaviorAbortMode::Both, outValue );
    }

    string BehaviorTreeNames::joinNodeTypeNames()
    {
        string text;
        for ( int32 value = 0; value <= static_cast<int32>( BehaviorNodeKind::SubTree ); ++value )
        {
            text += text.empty() ? "" : ", ";
            text += getEnumName( static_cast<BehaviorNodeKind>( value ) );
        }
        return text;
    }

    bool BehaviorTreeNames::isLeaf( BehaviorNodeKind type )
    {
        return BehaviorTreeAssetInternal::isLeaf( type );
    }

    bool BehaviorTreeNames::isDecorator( BehaviorNodeKind type )
    {
        const bool bComposite = type == BehaviorNodeKind::Selector || type == BehaviorNodeKind::Sequence;
        return bComposite == false && BehaviorTreeAssetInternal::isLeaf( type ) == false;
    }
} // namespace sw

namespace sw
{
    void BehaviorTreeXML::resetNodeFields( BehaviorNodeKind type, BehaviorTreeNodeData& inoutData )
    {
        inoutData._type         = type;
        inoutData._params       = InstancedStruct( BehaviorTreeAssetInternal::findFieldsOps( type ) );
        inoutData._actionParams = InstancedStruct{};
        inoutData._listRawParam.clear();
    }

    bool BehaviorTreeXML::readNodeFields( const XMLNode& node, BehaviorNodeKind type, string_view sourceName, BehaviorTreeNodeData& inoutData )
    {
        using Internal = BehaviorTreeAssetInternal;
        resetNodeFields( type, inoutData );
        inoutData._inheritID = string( node.getAttributeText( "id" ) );
        inoutData._name      = string( node.getAttributeText( "name" ) );
        if ( type != BehaviorNodeKind::Action )
        {
            return XMLFields::readFields( node, inoutData._params.getInstance(), *inoutData._params.getType(), sourceName, { "id", "name" } );
        }
        bool                     bValid = XMLFields::readFields( node, inoutData._params.getInstance(), *inoutData._params.getType(), sourceName, {}, &Internal::isNotTaskField );
        const ActionEntry* const pEntry = ActionRegistry::find( inoutData._params.find<BehaviorActionParams>()->_task );
        if ( pEntry != nullptr )
        {
            inoutData._actionParams = InstancedStruct( *pEntry->_pParamsOps );
            return XMLFields::readFields( node, inoutData._actionParams.getInstance(), *inoutData._actionParams.getType(), sourceName, { "id", "name", "task" } ) && bValid;
        }
        for ( XMLAttribute attribute = node.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
        {
            if ( Internal::isTaskOwnAttribute( attribute.getName() ) == false )
                inoutData._listRawParam.push_back( BehaviorRawParam{ string( attribute.getName() ), string( attribute.getValue() ) } );
        }
        return bValid;
    }

    void BehaviorTreeXML::writeNodeFields( const XMLNode& node, const BehaviorTreeNodeData& data )
    {
        if ( data._inheritID.empty() == false )
            node.appendAttribute( "id", string_view{ data._inheritID } );
        if ( data._name.empty() == false )
            node.appendAttribute( "name", string_view{ data._name } );
        const InstancedStruct defaults( BehaviorTreeAssetInternal::findFieldsOps( data._type ) );
        XMLFields::writeFields( node, data._params.getInstance(), defaults.getInstance(), *data._params.getType() );
        if ( data._actionParams.isValid() )
        {
            const InstancedStruct defaultParams( *data._actionParams.getOps() );
            XMLFields::writeFields( node, data._actionParams.getInstance(), defaultParams.getInstance(), *data._actionParams.getType() );
        }
        for ( const BehaviorRawParam& param : data._listRawParam )
        {
            node.appendAttribute( param._name.c_str(), string_view{ param._value } );
        }
    }

    void BehaviorTreeXML::selectTask( BehaviorTreeNodeData& inoutData, string_view task )
    {
        BehaviorActionParams* const pParams = inoutData._params.find<BehaviorActionParams>();
        if ( pParams == nullptr )
            return;
        pParams->_task = string( task );
        inoutData._listRawParam.clear();
        const ActionEntry* const pEntry = ActionRegistry::find( task );
        inoutData._actionParams         = pEntry != nullptr ? InstancedStruct( *pEntry->_pParamsOps ) : InstancedStruct{};
    }
} // namespace sw

namespace sw
{
    BehaviorTreeAsset::BehaviorTreeAsset()
        : _tree{}
        , _pSource{ nullptr }
    {
    }

    bool BehaviorTreeAsset::loadFromAsset( const AssetID& assetID )
    {
        string text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "BehaviorTreeAsset.FileNotFound", "Behavior tree '%#' cannot be read", assetID.view() );
            return false;
        }
        // 리소스 id 로 읽는다 - 조건 · 덮어쓰기 층 · 다시 읽기 기록이 이 합성 한 길을 지난다.
        string flatText;
        bool   bComposed = false;
        if ( ResourceComposer::compose( text, assetID.view(), ResourceContext::forSchema( &BehaviorTreeSchema::make ).withAsset( assetID ), flatText, bComposed ) == false )
        {
            _tree = BehaviorTree{};
            return false;
        }
        DataInheritanceResourceSource source;
        return readFlatText( bComposed ? string_view( flatText ) : string_view( text ), assetID.view(), source );
    }

    bool BehaviorTreeAsset::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        DataInheritanceResourceSource source;
        return loadFromXMLText( xmlText, sourceName, source );
    }

    bool BehaviorTreeAsset::loadFromXMLText( string_view xmlText, string_view sourceName, IDataInheritanceSource& source )
    {
        // 쿠킹본은 이미 펼쳤다 — 글 검색 한 번으로 지나간다.
        string flatText;
        if ( DataInheritance::flatten( xmlText, sourceName, BehaviorTreeSchema::make(), source, flatText ) == false )
        {
            _tree = BehaviorTree{};
            return false;
        }
        return readFlatText( flatText, sourceName, source );
    }

    bool BehaviorTreeAsset::readFlatText( string_view flatText, string_view sourceName, IDataInheritanceSource& source )
    {
        _pSource           = &source;
        const bool bLoaded = XMLCatalog<BehaviorTreeAsset>::loadFromXMLText( flatText, sourceName );
        _pSource           = nullptr;
        return bLoaded;
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
        // 쓰는 쪽이 글을 직접 넘기는 길이 없도록 공개 읽기가 모두 `_pSource` 를 채운다.
        BehaviorTreeReader reader( _tree, sourceName, *_pSource );
        bValid = reader.readNode( first, nullopt, 0 ) && bValid;
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

namespace sw
{
    BehaviorSubTreeParams::BehaviorSubTreeParams() = default;

    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<BehaviorTreeAsset>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( BehaviorTreeAsset, BehaviorTreeAsset, SW_GF_API, BehaviorTreeAsset::kAssetType.withLoader( AssetTypeLoader::of<BehaviorTreeAsset>( MemoryTag::Game ) ) );
} // namespace sw
