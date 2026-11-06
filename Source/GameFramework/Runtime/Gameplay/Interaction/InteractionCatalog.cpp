#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Interaction/InteractionCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/AssetTypeRegistry.h"
#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    namespace
    {
        struct InteractionCatalogInternal
        {
            // <Interaction> 은 상호작용 칸(InteractionDef)과 첫 단계 칸(InteractionStepDef)을 함께 적는다 — 두 스키마가 서로의 칸을 손 읽기로 넘긴다.
            static constexpr const utf8* kArrStepField[]        = { "prompt", "mode", "duration", "presses", "decay", "maxParticipants", "montage", "pairedAnimation", "interactorRole", "interactableRole" };
            static constexpr const utf8* kArrInteractionField[] = { "id", "maxDistance", "maxAngle", "cooldown", "lineOfSight", "requiredTags", "forbiddenTags", "alignment", "highlight", "highlightColor", "authority" };

            template <size_t Count>
            static bool isIn( const utf8* pName, const utf8* const ( &arrName )[Count] )
            {
                for ( const utf8* pKnown : arrName )
                {
                    if ( StringUtil::equals( pName, pKnown ) )
                        return true;
                }
                return false;
            }
            static bool isStepField( const utf8* pName ) { return isIn( pName, kArrStepField ); }
            static bool isInteractionField( const utf8* pName ) { return isIn( pName, kArrInteractionField ); }

            static void readTags( string_view text, TagContainer& outTags )
            {
                StringUtil::forEachToken( text, ", ;", [&outTags]( string_view token )
                { outTags.addTag( TagID::request( token ) ); } );
            }

            /** @brief 단계 속성(mode, prompt, montage, duration, presses, decay, maxParticipants, pairedAnimation, interactorRole, interactableRole)을 읽습니다. 빠진 속성은 @p inoutStep 의 값을 그대로 둡니다. */
            [[nodiscard]] static bool readStep( const XMLNode& node, InteractionStepDef& inoutStep, string_view sourceName, XMLFields::AcceptAttributeFunction pfnOtherField )
            {
                // 칸은 InteractionStepDef 의 PROPERTY(적지 않은 칸은 @p inoutStep 의 값 그대로). 페어드 애니메이션 경로 목록만 손으로 푼다.
                const bool  bValid           = XMLFields::readFields( node, inoutStep, sourceName, { "pairedAnimation" }, pfnOtherField );
                const utf8* pPairedAnimation = node.findAttribute( "pairedAnimation" );
                if ( pPairedAnimation != nullptr )
                {
                    inoutStep._listPairedAnimationPath.clear();
                    StringUtil::forEachToken( string_view( pPairedAnimation ), "; ", [&inoutStep]( string_view token )
                    { inoutStep._listPairedAnimationPath.push_back( string{ token } ); } );
                }
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
                    SW_LOG_WARNING_ID( "InteractionCatalog.InteractionIdPairedAnimationNeeds", "%#: <Interaction id=\"%#\"> pairedAnimation needs interactorRole and interactableRole", sourceName, interactionID.c_str() );
                    bValid = false;
                }
                if ( step._montage.empty() == false )
                {
                    SW_LOG_WARNING_ID( "InteractionCatalog.InteractionIdBothMontage", "%#: <Interaction id=\"%#\"> has both montage and pairedAnimation on one step", sourceName, interactionID.c_str() );
                    bValid = false;
                }
                return bValid;
            }
        };

    } // namespace

    /** 진행 중인 세션이 옛 정의를 읽으므로 제자리에서 바꾸지 않고 떼어 낸다. 스키마 테스트와 카탈로그 편집기도 이 정의로 파일을 읽는다. */
    SW_MODULE_SHARED_ASSET_CACHE( Interactions, InteractionCatalog, SW_GF_API,
                                  AssetTypeDef::makeDataFile( "interactions", AssetPathRule{ {}, {}, ".interactions.xml" }, AssetTypeLoader::of<InteractionCatalog>( MemoryTag::Game, SharedAssetReloadMode::Detach ) ) );

    namespace
    {
        /** @brief 공유 캐시를 읽는 감시 값입니다 - 캐시 등록 줄(특성 특수화) 뒤에 둬야 그 전에 인스턴스화되지 않는다. */
        struct InteractionCatalogWatch
        {
            /** @brief 감시 값 `Interaction.MaxDistance` - 누가 쥔 공유 표(target = 리소스 id)에서 상호작용(key = id)의 사거리입니다. 다시 읽기가 값까지 닿는지 시나리오가 봅니다(`editor/catalogcascade`). */
            [[nodiscard]] static bool readMaxDistanceWatch( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const shared_ptr<const InteractionCatalog> pCatalog = SharedAssetCache<InteractionCatalog>::findHeld( query._target ); // 읽지 않는다 - 새로 읽으면 다시 읽기가 쥔 쪽에 닿지 않아도 새 값이 보인다
                const InteractionDef*                      pDef     = pCatalog != nullptr ? pCatalog->findInteraction( hashed_string( query._key ) ) : nullptr;
                if ( pDef == nullptr )
                {
                    outValue.setText( pCatalog == nullptr ? "nobody holds that catalog" : "no such interaction in that catalog" );
                    return false;
                }
                outValue.setNumber( pDef->_maxDistance );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( interactionMaxDistance, "Interaction.MaxDistance", "Max distance (m) of interaction key in the shared catalog target (a resource id)", &InteractionCatalogWatch::readMaxDistanceWatch, true, true );
} // namespace sw

namespace sw
{
    InteractionDef::InteractionDef()
        : _id{}
        , _listStep{}
        , _requiredTags{}
        , _forbiddenTags{}
        , _alignmentMarker{}
        , _maxDistance{ 2.0f }
        , _maxAngle{ 0.0f }
        , _cooldown{ 0.0f }
        , _highlight{ InteractionHighlight::Outline }
        , _highlightColor{ 1.0f, 0.85f, 0.3f }
        , _authority{ InteractionAuthority::Local }
        , _bLineOfSight{ SW_TRUE }
    {
    }

    bool InteractionDef::allowsInteractor( const TagContainer& interactorTags ) const
    {
        return interactorTags.matchesTags( _requiredTags, _forbiddenTags );
    }

    bool InteractionCatalog::readInteraction( const XMLNode& node, InteractionDef& outDef, string_view sourceName ) const
    {
        using Internal = InteractionCatalogInternal;
        // 적지 않은 칸은 InteractionDef 의 초기값이다. 각은 라디안이다. 범위 밖 값은 자르지 않고 오류다(PROPERTY 가 본다).
        bool bValid = XMLFields::readFields( node, outDef, sourceName, { "requiredTags", "forbiddenTags" }, &Internal::isStepField );
        Internal::readTags( node.getAttributeText( "requiredTags" ), outDef._requiredTags );
        Internal::readTags( node.getAttributeText( "forbiddenTags" ), outDef._forbiddenTags );

        InteractionStepDef rootStep;
        bValid        = Internal::readStep( node, rootStep, sourceName, &Internal::isInteractionField ) && bValid;
        XMLNode child = node.findChild();
        for ( ; child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Step" ) == false )
            {
                SW_LOG_WARNING_ID( "InteractionCatalog.UnknownElementInteraction", "%#: unknown element <%#> in <Interaction>", sourceName, child.getName() );
                bValid = false;
                continue;
            }
            InteractionStepDef step = rootStep;
            bValid                  = Internal::readStep( child, step, sourceName, nullptr ) && bValid;
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

    bool InteractionCatalog::readSmartObject( const XMLNode& node, UsableObjectDef& outDef, string_view sourceName ) const
    {
        using Internal = InteractionCatalogInternal;
        bool bValid    = XMLFields::readFields( node, outDef, sourceName );
        for ( XMLNode slotNode = node.findChild(); slotNode; slotNode = slotNode.findNextSibling() )
        {
            if ( StringUtil::equals( slotNode.getName(), "Slot" ) == false )
            {
                SW_LOG_WARNING_ID( "InteractionCatalog.UnknownElementUsableObject", "%#: unknown element <%#> in <UsableObject>", sourceName, slotNode.getName() );
                bValid = false;
                continue;
            }
            UsableObjectSlotDef slot; // 각은 라디안(±2π)이다 — PROPERTY 가 본다
            bValid = XMLFields::readFields( slotNode, slot, sourceName, { "tags" } ) && bValid;
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
            if ( StringUtil::equals( child.getName(), "Interaction" ) )
            {
                InteractionDef def;
                bValid = readInteraction( child, def, sourceName ) && bValid;
                if ( def._id.empty() )
                {
                    SW_LOG_WARNING_ID( "InteractionCatalog.InteractionWithoutIdSkipped", "%#: <Interaction> without an id - skipped", sourceName );
                    bValid = false;
                    continue;
                }
                addInteraction( def );
                continue;
            }
            if ( StringUtil::equals( child.getName(), "UsableObject" ) )
            {
                UsableObjectDef def;
                bValid = readSmartObject( child, def, sourceName ) && bValid;
                if ( def._id.empty() )
                {
                    SW_LOG_WARNING_ID( "InteractionCatalog.UsableObjectWithoutIdSkipped", "%#: <UsableObject> without an id - skipped", sourceName );
                    bValid = false;
                    continue;
                }
                addSmartObject( def );
                continue;
            }
            SW_LOG_WARNING_ID( "InteractionCatalog.UnknownElementInteractions", "%#: unknown element <%#> in <Interactions>", sourceName, child.getName() );
            bValid = false;
        }
        return bValid;
    }

} // namespace sw
