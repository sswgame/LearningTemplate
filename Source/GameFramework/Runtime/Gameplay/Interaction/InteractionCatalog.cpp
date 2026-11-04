#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Interaction/InteractionCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetDataKind.h"
#include "Engine/Module/ModuleSubsystemRegistrar.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/GameData/GameDataCache.h"

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

        /**
         * @class InteractionCatalogCache
         * @brief 경로마다 한 번 읽어서 공유하는 상호작용 카탈로그 캐시입니다 — GameFramework 의 모듈 서브시스템(엔진 범위)이 소유하고 에셋 캐시 레지스트리에 올린다.
         */
        class InteractionCatalogCache final : public GameDataCacheSubsystem<InteractionCatalog>
        {
        public:
            static constexpr const utf8* kSubsystemName = "InteractionCatalogCache";

            InteractionCatalogCache()
                : GameDataCacheSubsystem<InteractionCatalog>{ "InteractionCatalog" }
            {
            }

            const utf8* getSubsystemName() const override { return kSubsystemName; }
        };

        /** @brief 공유 카탈로그 캐시입니다. 모듈 서브시스템이 서기 전 · 내려간 뒤의 접근은 결함이라 멈춘다 — 카탈로그를 읽을 엔진이 없다. */
        GameDataCache<InteractionCatalog>& getSharedCatalogCache()
        {
            return game::getSubsystem<InteractionCatalogCache>().getCache();
        }
    } // namespace

    SW_MODULE_SUBSYSTEM( Engine, InteractionCatalogCache, InteractionCatalogCache );
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

    const InteractionCatalog* InteractionCatalog::findShared( string_view path )
    {
        return getSharedCatalogCache().find( path );
    }

    uint32 InteractionCatalog::getSharedReloadCount()
    {
        return getSharedCatalogCache().getReloadCount();
    }
} // namespace sw

namespace sw
{
    /** @brief `InteractionCatalog` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_RESOURCE_DATA_KIND( Interactions, "interactions", nullptr, nullptr, ".interactions.xml", &AssetDataKindUtil::loadCatalog<InteractionCatalog> );
} // namespace sw
