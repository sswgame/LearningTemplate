#include "pch.h"

#include "GameFramework/Base/Gameplay/Interaction/InteractionCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/LifetimeScope.h"

#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

#include "GameFramework/Base/Foundation/Data/GameDataCache.h"
#include "GameFramework/Base/Foundation/Data/GameDataXML.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw
{
    SW_LOG_CALLER( "InteractionCatalog" );

    namespace
    {
        struct InteractionCatalogInternal
        {
            static constexpr const utf8* kArrInteractionAttribute[] = { "id", "prompt", "mode", "duration", "presses", "decay",
                                                                        "maxParticipants", "maxDistance", "maxAngle", "lineOfSight", "cooldown", "requiredTags",
                                                                        "forbiddenTags", "alignment", "highlight", "highlightColor", "authority", "montage",
                                                                        "pairedAnimation", "interactorRole", "interactableRole" };
            static constexpr const utf8* kArrStepAttribute[]        = { "prompt", "mode", "duration", "presses", "decay", "maxParticipants", "montage",
                                                                        "pairedAnimation", "interactorRole", "interactableRole" };
            static constexpr const utf8* kArrSmartObjectAttribute[] = { "id" };
            static constexpr const utf8* kArrSlotAttribute[]        = { "id", "offset", "yaw", "tags" };

            template <size_t Count>
            static bool checkAttributes( const XMLNode& node, const utf8* const ( &arrKnown )[Count], string_view sourceName )
            {
                return XMLNameCheck::reportUnknownAttributes( node, arrKnown, sourceName, LogLevel::Warning );
            }

            template <typename TEnum>
            [[nodiscard]] static bool readEnum( const XMLNode& node, const utf8* pName, TEnum& inoutValue, string_view sourceName )
            {
                const utf8* pText = node.findAttribute( pName );
                if ( pText == nullptr )
                    return true;
                TEnum parsed{};
                if ( game::getSubsystem<TypeRegistry>().enumFromString( string_view( pText ), parsed ) )
                {
                    inoutValue = parsed;
                    return true;
                }
                SW_LOG_WARNING( "%#: <%#> has unknown %# '%#'", sourceName, node.getName(), pName, pText );
                return false;
            }

            static void readTags( string_view text, TagContainer& outTags )
            {
                StringUtil::forEachToken( text, ", ;", [&outTags]( string_view token )
                { outTags.addTag( TagID::request( token ) ); } );
            }

            /** @brief 단계 속성(mode, prompt, montage, duration, presses, decay, maxParticipants, pairedAnimation, interactorRole, interactableRole)을 읽습니다. 빠진 속성은 @p inoutStep 의 값을 그대로 둡니다. */
            [[nodiscard]] static bool readStep( const XMLNode& node, InteractionStepDef& inoutStep, string_view sourceName )
            {
                bool        bValid  = readEnum( node, "mode", inoutStep._mode, sourceName );
                const utf8* pPrompt = node.findAttribute( "prompt" );
                if ( pPrompt != nullptr )
                    inoutStep._prompt = hashed_string( pPrompt );
                const utf8* pMontage = node.findAttribute( "montage" );
                if ( pMontage != nullptr )
                    inoutStep._montage = hashed_string( pMontage );
                const utf8* pPairedAnimation = node.findAttribute( "pairedAnimation" );
                if ( pPairedAnimation != nullptr )
                {
                    inoutStep._listPairedAnimationPath.clear();
                    StringUtil::forEachToken( string_view( pPairedAnimation ), "; ", [&inoutStep]( string_view token )
                    { inoutStep._listPairedAnimationPath.push_back( string{ token } ); } );
                }
                const utf8* pInteractorRole = node.findAttribute( "interactorRole" );
                if ( pInteractorRole != nullptr )
                    inoutStep._interactorRole = hashed_string( pInteractorRole );
                const utf8* pInteractableRole = node.findAttribute( "interactableRole" );
                if ( pInteractableRole != nullptr )
                    inoutStep._interactableRole = hashed_string( pInteractableRole );
                bValid = GameDataXML::readOptionalNumber( node, "duration", sourceName, 0.001f, MathUtil::kMaxFloat, inoutStep._duration ) && bValid;
                bValid = GameDataXML::readOptionalNumber( node, "presses", sourceName, 1, MathUtil::kMaxInt32, inoutStep._presses ) && bValid;
                bValid = GameDataXML::readOptionalNumber( node, "decay", sourceName, 0.0f, MathUtil::kMaxFloat, inoutStep._decay ) && bValid;
                bValid = GameDataXML::readOptionalNumber( node, "maxParticipants", sourceName, 1, MathUtil::kMaxInt32, inoutStep._maxParticipants ) && bValid;
                return bValid;
            }

            /** @brief 페어드 애니메이션을 쓰는 단계는 두 역할이 있어야 하고, 몽타주와 함께 쓰지 않습니다(둘 다 하는 쪽의 애니메이터를 재생한다). */
            [[nodiscard]] static bool validateStep( const InteractionStepDef& step, const hashed_string& interactionID, string_view sourceName )
            {
                if ( step._listPairedAnimationPath.empty() )
                    return true;
                bool bValid = true;
                if ( step._interactorRole.empty() || step._interactableRole.empty() )
                {
                    SW_LOG_WARNING( "%#: <Interaction id=\"%#\"> pairedAnimation needs interactorRole and interactableRole", sourceName, interactionID.c_str() );
                    bValid = false;
                }
                if ( step._montage.empty() == false )
                {
                    SW_LOG_WARNING( "%#: <Interaction id=\"%#\"> has both montage and pairedAnimation on one step", sourceName, interactionID.c_str() );
                    bValid = false;
                }
                return bValid;
            }
        };

        /** @brief 캐시를 엔진 수명 범위가 만들 수 있게 종류 이름을 쥔 껍데기입니다(`ScopeOwned` 는 기본 생성한다). */
        struct InteractionCatalogCacheHolder
        {
            GameDataCache<InteractionCatalog> _cache{ "InteractionCatalog" };
        };

        /** @brief 공유 카탈로그 캐시의 소유자입니다 — 엔진 수명 범위가 소유해 엔진 종료 때(또는 이 모듈이 내려갈 때) 사라지고, 레지스트리는 `GameDataCacheRegistry` 가 맞춥니다. */
        ScopeOwned<InteractionCatalogCacheHolder> s_sharedCatalogCache;

        /** @brief 경로마다 한 번 읽어서 공유하는 테이블입니다. */
        GameDataCache<InteractionCatalog>& getSharedCatalogCache()
        {
            InteractionCatalogCacheHolder* const pHolder = s_sharedCatalogCache.getOrCreate();
            SW_ASSERT( pHolder != nullptr ); // nullptr 은 엔진 수명 범위가 닫힌 뒤의 접근이다 — 카탈로그를 읽을 엔진이 없다
            return pHolder->_cache;
        }
    } // namespace
} // namespace sw

namespace sw
{
    bool InteractionDef::allowsInteractor( const TagContainer& interactorTags ) const
    {
        return interactorTags.matchesTags( _requiredTags, _forbiddenTags );
    }

    bool InteractionCatalog::readInteraction( const XMLNode& node, InteractionDef& outDef, string_view sourceName ) const
    {
        using Internal = InteractionCatalogInternal;
        bool bValid    = Internal::checkAttributes( node, Internal::kArrInteractionAttribute, sourceName );
        outDef._id     = hashed_string( node.findAttribute( "id" ) != nullptr ? node.findAttribute( "id" ) : "" );
        // 적지 않은 칸은 InteractionDef 의 초기값이다. 각은 라디안이다. 범위 밖 값은 자르지 않고 오류다.
        bValid = GameDataXML::readOptionalNumber( node, "maxDistance", sourceName, 0.0f, MathUtil::kMaxFloat, outDef._maxDistance ) && bValid;
        bValid = GameDataXML::readOptionalNumber( node, "maxAngle", sourceName, 0.0f, MathUtil::kPi, outDef._maxAngle ) && bValid;
        bValid = GameDataXML::readOptionalNumber( node, "cooldown", sourceName, 0.0f, MathUtil::kMaxFloat, outDef._cooldown ) && bValid;
        bValid = GameDataXML::readOptionalFlag( node, "lineOfSight", outDef._bLineOfSight ) && bValid;
        Internal::readTags( node.getAttributeText( "requiredTags" ), outDef._requiredTags );
        Internal::readTags( node.getAttributeText( "forbiddenTags" ), outDef._forbiddenTags );
        const utf8* pAlignment = node.findAttribute( "alignment" );
        if ( pAlignment != nullptr )
            outDef._alignmentMarker = hashed_string( pAlignment );
        bValid = Internal::readEnum( node, "highlight", outDef._highlight, sourceName ) && bValid;
        bValid = GameDataXML::readOptionalFloat3( node, "highlightColor", sourceName, outDef._highlightColor ) && bValid;
        bValid = Internal::readEnum( node, "authority", outDef._authority, sourceName ) && bValid;

        InteractionStepDef rootStep;
        bValid        = Internal::readStep( node, rootStep, sourceName ) && bValid;
        XMLNode child = node.findChild();
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
        for ( const InteractionStepDef& step : outDef._listStep )
        {
            bValid = Internal::validateStep( step, outDef._id, sourceName ) && bValid;
        }
        return bValid;
    }

    bool InteractionCatalog::readSmartObject( const XMLNode& node, SmartObjectDef& outDef, string_view sourceName ) const
    {
        using Internal = InteractionCatalogInternal;
        bool bValid    = Internal::checkAttributes( node, Internal::kArrSmartObjectAttribute, sourceName );
        outDef._id     = hashed_string( node.findAttribute( "id" ) != nullptr ? node.findAttribute( "id" ) : "" );
        for ( XMLNode slotNode = node.findChild(); slotNode; slotNode = slotNode.findNextSibling() )
        {
            if ( StringUtil::equals( slotNode.getName(), "Slot", true ) == false )
            {
                SW_LOG_WARNING( "%#: unknown element <%#> in <SmartObject>", sourceName, slotNode.getName() );
                bValid = false;
                continue;
            }
            bValid = Internal::checkAttributes( slotNode, Internal::kArrSlotAttribute, sourceName ) && bValid;
            SmartObjectSlotDef slot;
            float3             offset{};
            bValid = GameDataXML::readOptionalFloat3( slotNode, "offset", sourceName, offset ) && bValid;

            slot._id     = hashed_string( slotNode.findAttribute( "id" ) != nullptr ? slotNode.findAttribute( "id" ) : "" );
            slot._offset = offset;
            bValid       = GameDataXML::readOptionalNumber( slotNode, "yaw", sourceName, -MathUtil::kTwoPi, MathUtil::kTwoPi, slot._yaw ) && bValid;
            Internal::readTags( slotNode.getAttributeText( "tags" ), slot._tags );
            outDef._listSlot.push_back( slot );
        }
        return bValid;
    }

    bool InteractionCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        bool bValid = true;
        for ( XMLNode child = root.findChild(); child; child = child.findNextSibling() )
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
