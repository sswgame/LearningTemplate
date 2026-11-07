#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/SurvivalHorrorCatalog.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"

namespace sw
{
    namespace
    {
        struct SurvivalHorrorCatalogInternal
        {

            /** @brief `code="3 1 4"` 를 숫자 목록으로 읽습니다. 비었거나 깨진 숫자가 있으면 오류입니다. */
            [[nodiscard]] static bool readDigits( string_view text, vector<int32>& outListDigit, string_view sourceName, const hashed_string& id )
            {
                outListDigit.clear();
                bool bValid = true;
                StringUtil::forEachToken( text, ",; ", [&]( string_view token )
                {
                    int32 digit = 0;
                    if ( StringUtil::parseInt( token, digit ) )
                        outListDigit.push_back( digit );
                    else
                    {
                        SW_LOG_ERROR_ID( "SurvivalHorrorCatalog.DialLockBadDigit", "%#: dial lock '%#' has a bad digit '%#'", sourceName, id.c_str(), token );
                        bValid = false;
                    }
                } );
                if ( outListDigit.empty() )
                {
                    SW_LOG_ERROR_ID( "SurvivalHorrorCatalog.DialLockNeedsCode", "%#: dial lock '%#' needs code=\"...\"", sourceName, id.c_str() );
                    bValid = false;
                }
                return bValid;
            }

            /** @brief "a-b,c-d" 를 연결 목록으로 읽습니다. 모양이 틀린 연결은 오류입니다. */
            [[nodiscard]] static bool readLinks( string_view text, vector<SurvivalHorrorClueLink>& outListLink, string_view sourceName, const hashed_string& id )
            {
                outListLink.clear();
                bool bValid = true;
                StringUtil::forEachToken( text, ",; ", [&]( string_view token )
                {
                    const size_t dash           = token.find( '-' );
                    const bool   bMalformedLink = dash == string_view::npos || dash == 0 || dash + 1 >= token.size();
                    // 단서를 자기 자신과 잇는 연결은 보드(`linkClues`)가 받지 않으므로 그런 추리는 풀 수 없다.
                    if ( bMalformedLink || token.substr( 0, dash ) == token.substr( dash + 1 ) )
                    {
                        SW_LOG_ERROR_ID( "SurvivalHorrorCatalog.DeductionBadLinkExpected", "%#: deduction '%#' has a bad link '%#' (expected clueA-clueB)", sourceName, id.c_str(), token );
                        bValid = false;
                        return;
                    }
                    SurvivalHorrorClueLink link;
                    link._first  = hashed_string( token.substr( 0, dash ) );
                    link._second = hashed_string( token.substr( dash + 1 ) );
                    outListLink.push_back( link );
                } );
                return bValid;
            }
        };
    } // namespace

    SurvivalHorrorCatalog::SurvivalHorrorCatalog()
        : _itemCatalog{}
        , _monsterCatalog{}
        , _keyLockCatalog{}
        , _dialLockCatalog{}
        , _sequenceCatalog{}
        , _documentCatalog{}
        , _deductionCatalog{}
        , _recipeCatalog{}
        , _rules{}
    {
    }

    const RecipeDef* SurvivalHorrorCatalog::findCombine( const hashed_string& firstItem, const hashed_string& secondItem ) const
    {
        const hashed_string station = getCombineStation();
        for ( const RecipeDef& recipe : _recipeCatalog.getAll() )
        {
            if ( recipe._station != station || recipe._inputs.getTotalCount() != 2 )
                continue;
            if ( firstItem == secondItem )
            {
                if ( recipe._inputs.getItemCount( firstItem ) == 2 )
                    return &recipe;
                continue;
            }
            if ( recipe._inputs.getItemCount( firstItem ) == 1 && recipe._inputs.getItemCount( secondItem ) == 1 )
                return &recipe;
        }
        return nullptr;
    }

    uint32 SurvivalHorrorCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const string_view name = node.getName() != nullptr ? string_view( node.getName() ) : string_view{};
            if ( loadFromXML( node, name, sourceName ) )
                ++loadedCount;
        }
        // 열쇠 자물쇠가 가리키는 열쇠는 모두 읽은 뒤에 확인한다(자물쇠가 아이템보다 앞에 적혀도 된다).
        for ( const SurvivalHorrorKeyLockDef& lock : _keyLockCatalog.getAll() )
        {
            if ( _itemCatalog.find( lock._keyItem ) == nullptr )
                SW_LOG_ERROR_ID( "SurvivalHorrorCatalog.KeyLockNamesUnknown", "%#: key lock '%#' names an unknown key item '%#'", sourceName, lock._id.c_str(), lock._keyItem.c_str() );
        }
        return loadedCount;
    }

    bool SurvivalHorrorCatalog::loadFromXML( const XMLNode& node, string_view name, string_view sourceName )
    {
        if ( XMLReadUtil::isElement( name, "Rules" ) )
            return GameDataXML::readSetting( node, sourceName, _rules );
        if ( XMLReadUtil::isElement( name, "Item" ) )
            return GameDataXML::readInto( node, sourceName, _itemCatalog );
        if ( XMLReadUtil::isElement( name, "Combine" ) )
            return loadCombine( node, sourceName );
        if ( XMLReadUtil::isElement( name, "Monster" ) )
            return GameDataXML::readInto( node, sourceName, _monsterCatalog );
        if ( XMLReadUtil::isElement( name, "KeyLock" ) )
            return GameDataXML::readInto( node, sourceName, _keyLockCatalog );
        if ( XMLReadUtil::isElement( name, "DialLock" ) )
            return loadDialLock( node, sourceName );
        if ( XMLReadUtil::isElement( name, "Sequence" ) )
            return loadSequence( node, sourceName );
        if ( XMLReadUtil::isElement( name, "Document" ) )
            return loadDocument( node, sourceName );
        if ( XMLReadUtil::isElement( name, "Deduction" ) )
            return loadDeduction( node, sourceName );
        SW_LOG_ERROR_ID( "SurvivalHorrorCatalog.UnknownElementSkipped", "%#: unknown element <%#> - skipped", sourceName, name );
        return false;
    }

    bool SurvivalHorrorCatalog::loadCombine( const XMLNode& node, string_view sourceName )
    {
        SurvivalHorrorCombineDef combine;
        if ( XMLFields::readFields( node, combine, sourceName ) == false )
            return false;
        RecipeDef recipe;
        recipe._id      = combine._id;
        recipe._station = getCombineStation();
        recipe._inputs.addItem( combine._firstItem, 1 );
        recipe._inputs.addItem( combine._secondItem, 1 );
        recipe._outputs.addItem( combine._outItem, combine._count );
        return GameDataXML::addUnique( recipe, sourceName, _recipeCatalog );
    }

    bool SurvivalHorrorCatalog::loadDialLock( const XMLNode& node, string_view sourceName )
    {
        SurvivalHorrorDialLockDef lock;
        bool                      bValid = XMLFields::readFields( node, lock, sourceName, { "code" } );
        bValid                           = SurvivalHorrorCatalogInternal::readDigits( node.getAttributeText( "code" ), lock._listDigit, sourceName, lock._id ) && bValid;
        return bValid && GameDataXML::addUnique( lock, sourceName, _dialLockCatalog );
    }

    bool SurvivalHorrorCatalog::loadSequence( const XMLNode& node, string_view sourceName )
    {
        SurvivalHorrorSequenceDef sequence;
        if ( XMLFields::readFields( node, sequence, sourceName ) == false )
            return false;
        if ( sequence._listStep.empty() )
        {
            SW_LOG_ERROR_ID( "SurvivalHorrorCatalog.SequenceNeedsStepsB", "%#: sequence '%#' needs steps=\"a,b,...\"", sourceName, sequence._id.c_str() );
            return false;
        }
        return GameDataXML::addUnique( sequence, sourceName, _sequenceCatalog );
    }

    bool SurvivalHorrorCatalog::loadDocument( const XMLNode& node, string_view sourceName )
    {
        SurvivalHorrorDocumentDef document;
        if ( XMLFields::readFields( node, document, sourceName ) == false )
            return false;
        return GameDataXML::addUnique( document, sourceName, _documentCatalog );
    }

    bool SurvivalHorrorCatalog::loadDeduction( const XMLNode& node, string_view sourceName )
    {
        SurvivalHorrorDeductionDef deduction;
        bool                       bValid = XMLFields::readFields( node, deduction, sourceName, { "links" } );
        bValid                            = SurvivalHorrorCatalogInternal::readLinks( node.getAttributeText( "links" ), deduction._listRequiredLink, sourceName, deduction._id ) && bValid;
        return bValid && GameDataXML::addUnique( deduction, sourceName, _deductionCatalog );
    }

    GridInventory::ShapeDelegate SurvivalHorrorCatalog::makeShapeLookup() const
    {
        const SurvivalHorrorCatalog* pCatalog = this;
        return GridInventory::ShapeDelegate::create(
            [pCatalog]( const hashed_string& itemID, GridItemShape& outShape )
        {
            const SurvivalHorrorItemDef* pItem = pCatalog->findItem( itemID );
            if ( pItem == nullptr )
                return false;
            outShape._width    = pItem->_width;
            outShape._height   = pItem->_height;
            outShape._maxStack = pItem->_maxStack;
            return true;
        } );
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<SurvivalHorrorCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( SurvivalHorrorCatalog, SurvivalHorrorCatalog, SW_KIT_API, SurvivalHorrorCatalog::kAssetType.withLoader( AssetTypeLoader::of<SurvivalHorrorCatalog>( MemoryTag::Game ) ) );
} // namespace sw
