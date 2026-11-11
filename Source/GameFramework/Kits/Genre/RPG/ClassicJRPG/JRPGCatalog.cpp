#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/JRPGCatalog.h"

#include "Core/Foundation/Expected.h"
#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        struct JRPGCatalogInternal
        {
            static constexpr const utf8* kArrStatName[kJRPGStatCount]   = { "hp", "mp", "str", "agi", "vit", "intellect", "luck" };
            static constexpr const utf8* kArrGrowthName[kJRPGStatCount] = { "growHP", "growMP", "growStr", "growAgi", "growVit", "growIntellect", "growLuck" };

            /** @brief 능력치 일곱 칸을 이름(@p arrName)으로 읽습니다. 적지 않은 칸은 Def 의 초기값이고, 깨졌거나 0 보다 작으면 오류입니다. */
            [[nodiscard]] static bool readStats( const XMLNode& node, const utf8* const ( &arrName )[kJRPGStatCount], string_view sourceName,
                                                 int32 ( &inoutArrValue )[kJRPGStatCount] )
            {
                bool bValid = true;
                for ( int32 index = 0; index < kJRPGStatCount; ++index )
                {
                    optional<int32> value;
                    const bool      bRead     = node.readOptionalAttribute( arrName[index], value );
                    const bool      bNegative = value.has_value() && *value < 0;
                    if ( bRead == false || bNegative )
                    {
                        SW_LOG_ERROR_ID( "JRPGCatalog.NeedsIntegerMore", "%#: <%# %#=\"%#\"> needs an integer of 0 or more", sourceName, node.getName(), arrName[index], node.getAttributeText( arrName[index] ) );
                        bValid = false;
                    }
                    else if ( value.has_value() )
                        inoutArrValue[index] = *value;
                }
                return bValid;
            }

            /** @brief 최대 HP 는 1 이상이어야 합니다(0 이면 처음부터 쓰러진 상태다). */
            [[nodiscard]] static bool checkMaxHP( const int32 ( &arrStat )[kJRPGStatCount], const XMLNode& node, string_view sourceName )
            {
                if ( arrStat[static_cast<size_t>( JRPGStat::MaxHP )] >= 1 )
                    return true;
                SW_LOG_ERROR_ID( "JRPGCatalog.IdNeedsHpMore", "%#: <%# id=\"%#\"> needs hp of 1 or more", sourceName, node.getName(), node.getAttributeText( "id" ) );
                return false;
            }
        };
    } // namespace

    JRPGCatalog::JRPGCatalog()
        : _classCatalog{}
        , _spellCatalog{}
        , _comboCatalog{}
        , _manualCatalog{}
        , _enemyCatalog{}
        , _areaCatalog{}
        , _curve{}
    {
    }

    void JRPGCatalog::clear()
    {
        _classCatalog.clear();
        _spellCatalog.clear();
        _comboCatalog.clear();
        _manualCatalog.clear();
        _enemyCatalog.clear();
        _areaCatalog.clear();
        _curve = ExperienceCurve{};
    }

    uint32 JRPGCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readProperties 가 남긴다).
        using Element                          = XMLCatalogElement<JRPGCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&JRPGCatalog::_curve>( "ExperienceCurve" ),
            Element::of<&JRPGCatalog::loadClass>( "Class" ),
            Element::of<&JRPGCatalog::_spellCatalog>( "Spell" ),
            Element::of<&JRPGCatalog::loadCombo>( "Combo" ),
            Element::of<&JRPGCatalog::loadManual>( "Manual" ),
            Element::of<&JRPGCatalog::loadEnemy>( "Enemy" ),
            Element::of<&JRPGCatalog::loadArea>( "Area" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );

        // 전투는 모르는 주문을 위력 0 으로 터뜨린다 — 오타가 말없이 시전만 하는 적이 되지 않게 참조 표(`engine/settings/datacatalogreferences.xml`)가 로드를 멈춘다.
        return result._loadedCount;
    }

    bool JRPGCatalog::loadClass( const XMLNode& node, string_view sourceName )
    {
        using Internal = JRPGCatalogInternal;
        JRPGClassDef def;
        bool         bValid = XMLFields::readProperties( node, def, sourceName,
                                                         { "hp", "mp", "str", "agi", "vit", "intellect", "luck", "growHP", "growMP", "growStr", "growAgi", "growVit",
                                                           "growIntellect", "growLuck" } );
        bValid              = Internal::readStats( node, Internal::kArrStatName, sourceName, def._arrBase ) && bValid;
        bValid              = Internal::readStats( node, Internal::kArrGrowthName, sourceName, def._arrGrowth ) && bValid;
        bValid              = Internal::checkMaxHP( def._arrBase, node, sourceName ) && bValid;
        for ( XMLNode learn = node.findChild( "Learn" ); learn; learn = learn.findNextSibling( "Learn" ) )
        {
            bValid = GameDataXML::readInto( learn, sourceName, def._listLearn ) && bValid;
        }
        return bValid && GameDataXML::addUnique( def, sourceName, _classCatalog );
    }

    bool JRPGCatalog::loadCombo( const XMLNode& node, string_view sourceName )
    {
        JRPGComboDef def;
        if ( XMLFields::readProperties( node, def, sourceName ) == false )
            return false;
        if ( def._listMemberID.size() < 2 )
        {
            SW_LOG_ERROR_ID( "JRPGCatalog.ComboNeedsTwoMore", "%#: combo '%#' needs two or more members", sourceName, def._id.c_str() );
            return false;
        }
        return GameDataXML::addUnique( def, sourceName, _comboCatalog );
    }

    bool JRPGCatalog::loadManual( const XMLNode& node, string_view sourceName )
    {
        JRPGManualDef def;
        bool          bValid = XMLFields::readProperties( node, def, sourceName );
        for ( XMLNode stage = node.findChild( "Stage" ); stage; stage = stage.findNextSibling( "Stage" ) )
        {
            bValid = GameDataXML::readInto( stage, sourceName, def._listStage ) && bValid;
        }
        return bValid && GameDataXML::addUnique( def, sourceName, _manualCatalog );
    }

    bool JRPGCatalog::loadEnemy( const XMLNode& node, string_view sourceName )
    {
        using Internal = JRPGCatalogInternal;
        JRPGEnemyDef def;
        bool         bValid = XMLFields::readProperties( node, def, sourceName, { "hp", "mp", "str", "agi", "vit", "intellect", "luck" } );
        bValid              = Internal::readStats( node, Internal::kArrStatName, sourceName, def._arrStat ) && bValid;
        bValid              = Internal::checkMaxHP( def._arrStat, node, sourceName ) && bValid;
        return bValid && GameDataXML::addUnique( def, sourceName, _enemyCatalog );
    }

    bool JRPGCatalog::loadArea( const XMLNode& node, string_view sourceName )
    {
        JRPGAreaDef def;
        bool        bValid = XMLFields::readProperties( node, def, sourceName );
        for ( XMLNode groupNode = node.findChild( "Group" ); groupNode; groupNode = groupNode.findNextSibling( "Group" ) )
        {
            JRPGEncounterGroup group;
            bValid = XMLFields::readProperties( groupNode, group, sourceName ) && bValid;
            if ( group._listEnemyID.empty() )
            {
                SW_LOG_ERROR_ID( "JRPGCatalog.AreaGroupWithoutEnemies", "%#: area '%#' has a <Group> without enemies", sourceName, def._id.c_str() );
                bValid = false;
                continue;
            }
            def._listGroup.push_back( group );
        }
        return bValid && GameDataXML::addUnique( def, sourceName, _areaCatalog );
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<JRPGCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( JRPGCatalog, JRPGCatalog, SW_KIT_API, JRPGCatalog::kAssetType.withLoader( AssetTypeLoader::of<JRPGCatalog>( MemoryTag::Game ) ) );
} // namespace sw
