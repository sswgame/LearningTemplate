#include "pch.h"

#include "GameFramework/Base/Interaction/InteractionCatalog.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Utility/Xml/XmlDocument.h"
#include "Engine/Utility/Xml/XmlNameCheck.h"

#include "GameFramework/Base/Data/GameDataCache.h"
#include "GameFramework/Base/Data/GameDataXml.h"

namespace sw
{
    SW_LOG_CALLER( "InteractionCatalog" );

    namespace
    {
        struct InteractionCatalogInternal
        {
            static constexpr const utf8* kRootName                  = "Interactions";
            static constexpr const utf8* kArrInteractionAttribute[] = { "id", "prompt", "mode", "duration", "presses", "decay",
                                                                        "maxParticipants", "maxDistance", "maxAngle", "lineOfSight", "cooldown", "requiredTags",
                                                                        "forbiddenTags", "alignment", "highlight", "authority" };
            static constexpr const utf8* kArrStepAttribute[]        = { "prompt", "mode", "duration", "presses", "decay", "maxParticipants" };
            static constexpr const utf8* kArrSmartObjectAttribute[] = { "id" };
            static constexpr const utf8* kArrSlotAttribute[]        = { "id", "offset", "yaw", "tags" };

            template <size_t Count>
            static bool checkAttributes( const XmlNode& node, const utf8* const ( &arrKnown )[Count], string_view sourceName )
            {
                return XmlNameCheck::reportUnknownAttributes( node, arrKnown, sourceName, LogLevel::Warning );
            }

            template <typename TEnum>
            [[nodiscard]] static bool readEnum( const XmlNode& node, const utf8* pName, TEnum& inoutValue, string_view sourceName )
            {
                const utf8* pText = node.findAttribute( pName );
                if ( pText == nullptr )
                    return true;
                TEnum parsed{};
                if ( engine::getTypeRegistry().enumFromString( string_view( pText ), parsed ) )
                {
                    inoutValue = parsed;
                    return true;
                }
                SW_LOG_WARNING( "%#: <%#> has unknown %# '%#'", sourceName, node.getName(), pName, pText );
                return false;
            }

            static void readTags( string_view text, TagContainer& outTags )
            {
                GameDataXml::forEachToken( text, ", ;", [&outTags]( string_view token )
                { outTags.addTag( TagID::request( token ) ); } );
            }

            /** @brief 단계 속성(mode · prompt · duration · presses · decay · maxParticipants)을 읽습니다. 빠진 칸은 @p inoutStep 의 것입니다. */
            [[nodiscard]] static bool readStep( const XmlNode& node, InteractionStepDef& inoutStep, string_view sourceName )
            {
                const bool  bValid  = readEnum( node, "mode", inoutStep._mode, sourceName );
                const utf8* pPrompt = node.findAttribute( "prompt" );
                if ( pPrompt != nullptr )
                    inoutStep._prompt = hashed_string( pPrompt );
                inoutStep._duration        = MathUtil::max( 0.0f, node.getAttributeFloat( "duration", inoutStep._duration ) );
                inoutStep._presses         = MathUtil::max( 1, node.getAttributeInt( "presses", inoutStep._presses ) );
                inoutStep._decay           = MathUtil::max( 0.0f, node.getAttributeFloat( "decay", inoutStep._decay ) );
                inoutStep._maxParticipants = MathUtil::max( 1, node.getAttributeInt( "maxParticipants", inoutStep._maxParticipants ) );
                return bValid;
            }
        };

        /** @brief 경로마다 한 번 읽어 나눠 쓰는 표입니다(GameFramework 모듈 정적 — 등록부는 `GameDataCacheRegistry` 가 맞춘다). */
        GameDataCache<InteractionCatalog>& getSharedCatalogCache()
        {
            static GameDataCache<InteractionCatalog> s_cache{ "InteractionCatalog" };
            return s_cache;
        }
    } // namespace
} // namespace sw

namespace sw
{
    bool InteractionDef::allowsInteractor( const TagContainer& interactorTags ) const { return interactorTags.matchesTags( _requiredTags, _forbiddenTags ); }

    bool InteractionCatalog::loadFromResource( string_view path )
    {
        XmlDocument doc;
        XmlNode     root;
        string      sourceName;
        if ( GameDataXml::loadRoot( doc, path, InteractionCatalogInternal::kRootName, root, sourceName ) == false )
            return false;
        return loadRoot( root, sourceName );
    }

    bool InteractionCatalog::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        XmlDocument doc;
        XmlNode     root;
        if ( GameDataXml::parseRoot( doc, xmlText, sourceName, InteractionCatalogInternal::kRootName, root ) == false )
            return false;
        return loadRoot( root, sourceName );
    }

    bool InteractionCatalog::readInteraction( const XmlNode& node, InteractionDef& outDef, string_view sourceName ) const
    {
        using Internal       = InteractionCatalogInternal;
        bool bValid          = Internal::checkAttributes( node, Internal::kArrInteractionAttribute, sourceName );
        outDef._id           = hashed_string( node.findAttribute( "id" ) != nullptr ? node.findAttribute( "id" ) : "" );
        outDef._maxDistance  = MathUtil::max( 0.0f, node.getAttributeFloat( "maxDistance", outDef._maxDistance ) );
        outDef._maxAngle     = MathUtil::clamp( node.getAttributeFloat( "maxAngle", 0.0f ), 0.0f, 180.0f ) * MathUtil::DegreeToRadian;
        outDef._cooldown     = MathUtil::max( 0.0f, node.getAttributeFloat( "cooldown", outDef._cooldown ) );
        outDef._bLineOfSight = node.getAttributeBool( "lineOfSight", true ) ? SW_TRUE : SW_FALSE;
        Internal::readTags( node.getAttributeText( "requiredTags" ), outDef._requiredTags );
        Internal::readTags( node.getAttributeText( "forbiddenTags" ), outDef._forbiddenTags );
        const utf8* pAlignment = node.findAttribute( "alignment" );
        if ( pAlignment != nullptr )
            outDef._alignmentMarker = hashed_string( pAlignment );
        bValid = Internal::readEnum( node, "highlight", outDef._highlight, sourceName ) && bValid;
        bValid = Internal::readEnum( node, "authority", outDef._authority, sourceName ) && bValid;

        InteractionStepDef rootStep;
        bValid        = Internal::readStep( node, rootStep, sourceName ) && bValid;
        XmlNode child = node.findChild();
        for ( ; child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Step", true ) == false )
            {
                SW_LOG_WARNING( "%#: unknown element <%#> in <Interaction>", sourceName, child.getName() );
                bValid = false;
                continue;
            }
            bValid                  = Internal::checkAttributes( child, Internal::kArrStepAttribute, sourceName ) && bValid;
            InteractionStepDef step = rootStep;
            bValid                  = Internal::readStep( child, step, sourceName ) && bValid;
            outDef._listStep.push_back( step );
        }
        if ( outDef._listStep.empty() )
            outDef._listStep.push_back( rootStep );
        return bValid;
    }

    bool InteractionCatalog::readSmartObject( const XmlNode& node, SmartObjectDef& outDef, string_view sourceName ) const
    {
        using Internal = InteractionCatalogInternal;
        bool bValid    = Internal::checkAttributes( node, Internal::kArrSmartObjectAttribute, sourceName );
        outDef._id     = hashed_string( node.findAttribute( "id" ) != nullptr ? node.findAttribute( "id" ) : "" );
        for ( XmlNode slotNode = node.findChild(); slotNode; slotNode = slotNode.findNextSibling() )
        {
            if ( StringUtil::equals( slotNode.getName(), "Slot", true ) == false )
            {
                SW_LOG_WARNING( "%#: unknown element <%#> in <SmartObject>", sourceName, slotNode.getName() );
                bValid = false;
                continue;
            }
            bValid = Internal::checkAttributes( slotNode, Internal::kArrSlotAttribute, sourceName ) && bValid;
            SmartObjectSlotDef slot;
            slot._id     = hashed_string( slotNode.findAttribute( "id" ) != nullptr ? slotNode.findAttribute( "id" ) : "" );
            slot._offset = GameDataXml::parseFloat3( slotNode.getAttributeText( "offset" ), float3{} );
            slot._yaw    = slotNode.getAttributeFloat( "yaw", 0.0f ) * MathUtil::DegreeToRadian;
            Internal::readTags( slotNode.getAttributeText( "tags" ), slot._tags );
            outDef._listSlot.push_back( slot );
        }
        return bValid;
    }

    bool InteractionCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        bool bValid = true;
        for ( XmlNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Interaction", true ) )
            {
                InteractionDef def;
                bValid = readInteraction( child, def, sourceName ) && bValid;
                if ( def._id.empty() )
                {
                    SW_LOG_WARNING( "%#: <Interaction> without an id - skipped", sourceName );
                    bValid = false;
                    continue;
                }
                addInteraction( def );
                continue;
            }
            if ( StringUtil::equals( child.getName(), "SmartObject", true ) )
            {
                SmartObjectDef def;
                bValid = readSmartObject( child, def, sourceName ) && bValid;
                if ( def._id.empty() )
                {
                    SW_LOG_WARNING( "%#: <SmartObject> without an id - skipped", sourceName );
                    bValid = false;
                    continue;
                }
                addSmartObject( def );
                continue;
            }
            SW_LOG_WARNING( "%#: unknown element <%#> in <Interactions>", sourceName, child.getName() );
            bValid = false;
        }
        return bValid;
    }

    const InteractionCatalog* InteractionCatalog::findShared( string_view path )
    {
        return getSharedCatalogCache().find( path );
    }

    uint32 InteractionCatalog::getSharedReloadCount()
    {
        return getSharedCatalogCache().getReloadCount();
    }
} // namespace sw
