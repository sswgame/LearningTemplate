#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/SurvivalHorrorCatalog.h"

#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

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

    uint32 SurvivalHorrorCatalog::loadRoot( const XMLCatalogRoot& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readProperties 가 남긴다).
        using Element                          = XMLCatalogElement<SurvivalHorrorCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&SurvivalHorrorCatalog::_rules>( "Rules" ),
            Element::of<&SurvivalHorrorCatalog::_itemCatalog>( "Item" ),
            Element::of<&SurvivalHorrorCatalog::loadCombine>( "Combine" ),
            Element::of<&SurvivalHorrorCatalog::_monsterCatalog>( "Monster" ),
            Element::of<&SurvivalHorrorCatalog::_keyLockCatalog>( "KeyLock" ),
            Element::of<&SurvivalHorrorCatalog::loadDialLock>( "DialLock" ),
            Element::of<&SurvivalHorrorCatalog::loadSequence>( "Sequence" ),
            Element::of<&SurvivalHorrorCatalog::loadDocument>( "Document" ),
            Element::of<&SurvivalHorrorCatalog::loadDeduction>( "Deduction" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        return result._loadedCount;
    }

    bool SurvivalHorrorCatalog::loadCombine( const XMLNode& node, string_view sourceName )
    {
        SurvivalHorrorCombineDef combine;
        if ( XMLFields::readProperties( node, combine, sourceName ) == false )
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
        bool                      bValid = XMLFields::readProperties( node, lock, sourceName, { "code" } );
        bValid                           = SurvivalHorrorCatalogInternal::readDigits( node.getAttributeText( "code" ), lock._listDigit, sourceName, lock._id ) && bValid;
        return bValid && GameDataXML::addUnique( lock, sourceName, _dialLockCatalog );
    }

    bool SurvivalHorrorCatalog::loadSequence( const XMLNode& node, string_view sourceName )
    {
        SurvivalHorrorSequenceDef sequence;
        if ( XMLFields::readProperties( node, sequence, sourceName ) == false )
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
        if ( XMLFields::readProperties( node, document, sourceName ) == false )
            return false;
        return GameDataXML::addUnique( document, sourceName, _documentCatalog );
    }

    bool SurvivalHorrorCatalog::loadDeduction( const XMLNode& node, string_view sourceName )
    {
        SurvivalHorrorDeductionDef deduction;
        bool                       bValid = XMLFields::readProperties( node, deduction, sourceName, { "links" } );
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
