#include "pch.h"

#include "Engine/Serialization/Inherit/ResourceTags.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include "Engine/Serialization/Inherit/ResourceComposition.h"

namespace sw
{
    namespace
    {
        struct ResourceTagsInternal
        {
            static constexpr const utf8* kArrPlatformValue[]    = { "windows", "linux", "android", "web" }; ///< `build::kPlatformName` 을 소문자로(플랫폼을 더하면 한 줄 — 빠지면 `Engine` 기동이 멈춘다)
            static constexpr const utf8* kArrScreenValue[]      = { "regular", "compact" };                 ///< UI 뷰포트 너비가 `UIScaleSettings::_compactMaxWidth` 보다 좁으면 compact(`UISystem::syncScreenTags`)
            static constexpr const utf8* kArrOrientationValue[] = { "landscape", "portrait" };              ///< UI 뷰포트가 세로로 길면 portrait
            static constexpr const utf8* kArrTargetValue[]      = { "client", "server" };                   ///< 팩 쿠킹 대상(`PackCookParams::_buildTarget`)과 같은 둘

            /** @brief 축 표입니다. 순서는 오류 문장 · 편집기 목록의 순서입니다. */
            static constexpr ResourceTagAxis kArrAxis[] = {
                {   "platform", ResourceTagTiming::Cook, ResourceTagArity::Single,    kArrPlatformValue,    static_cast<uint32>( SW_COUNT_OF( kArrPlatformValue ) ),    ResourceTagScope::Shared},
                {     "target", ResourceTagTiming::Cook, ResourceTagArity::Single,      kArrTargetValue,      static_cast<uint32>( SW_COUNT_OF( kArrTargetValue ) ),    ResourceTagScope::Shared},
                {    "quality", ResourceTagTiming::Load, ResourceTagArity::Single,              nullptr,                                                          0, ResourceTagScope::PerClient}, ///< 사용자 설정 `graphics.quality`(핸들러 `resource.tag`)
                {     "locale", ResourceTagTiming::Load, ResourceTagArity::Single,              nullptr,                                                          0, ResourceTagScope::PerClient}, ///< 현지화의 지금 언어
                {     "region", ResourceTagTiming::Load, ResourceTagArity::Single,              nullptr,                                                          0,    ResourceTagScope::Shared}, ///< 출시 지역(게임 코드가 넣는다)
                {      "layer", ResourceTagTiming::Load,  ResourceTagArity::Multi,              nullptr,                                                          0,    ResourceTagScope::Shared}, ///< 켜진 씬 데이터 레이어(`SceneDataLayerSubsystem`)
                {    "feature", ResourceTagTiming::Load,  ResourceTagArity::Multi,              nullptr,                                                          0,    ResourceTagScope::Shared}, ///< 게임이 켜는 기능 태그(행사 · 계절)
                {       "flag", ResourceTagTiming::Load,  ResourceTagArity::Multi,              nullptr,                                                          0,    ResourceTagScope::Shared}, ///< 게임 플래그를 리소스에 비추는 태그(게임 코드가 켠다)
                {     "screen", ResourceTagTiming::Load, ResourceTagArity::Single,      kArrScreenValue,      static_cast<uint32>( SW_COUNT_OF( kArrScreenValue ) ), ResourceTagScope::PerClient}, ///< 좁은 화면 재배치(UI 문서의 `_when="screen.compact"`)
                {"orientation", ResourceTagTiming::Load, ResourceTagArity::Single, kArrOrientationValue, static_cast<uint32>( SW_COUNT_OF( kArrOrientationValue ) ), ResourceTagScope::PerClient}, ///< 가로 · 세로 배치
            };

            /** @brief 태그 조건입니다. 문맥의 태그 칸이 없으면 "모름" 이라 거짓입니다. */
            static bool hasTag( const GameplayContext& context, const ConditionNameArg& args )
            {
                const ResourceTagSet* pTags = context.find<ResourceTagSlot>();
                return pTags != nullptr && pTags->contains( args._name.c_str() );
            }

            static string makeTag( string_view axisName, string_view value )
            {
                string tag{ axisName };
                tag += ResourceTags::kSeparator;
                tag += value;
                return tag;
            }

            /** @brief @p value 가 @p axis 의 값으로 쓸 수 있는지 봅니다. 아니면 그 까닭(대상은 `축.값`)입니다. */
            static expected<void> isValidValue( const ResourceTagAxis& axis, string_view value )
            {
                if ( value.empty() || value.find( ResourceTags::kSeparator ) != string_view::npos )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "not a value of the axis (empty, or it has the tag separator)", axis._pName }.appendDetail( "." ).appendDetail( value ) );
                if ( axis._ppValue == nullptr )
                    return {};
                for ( uint32 index = 0; index < axis._valueCount; ++index )
                {
                    if ( value == axis._ppValue[index] )
                        return {};
                }
                return unexpected( Error{ ErrorKind::NotFound, "not one of the values of the axis", axis._pName }.appendDetail( "." ).appendDetail( value ) );
            }

            /** @brief 축을 찾고 값 수가 @p arity 인지 봅니다. 틀리면 오류를 남기고 nullptr 입니다. */
            static const ResourceTagAxis* findAxisFor( string_view axisName, ResourceTagArity arity )
            {
                const ResourceTagAxis* pAxis = ResourceTags::findAxis( axisName );
                if ( pAxis == nullptr )
                {
                    SW_LOG_ERROR_ID( "ResourceTags.UnknownAxis", "Resource tag axis '%#' does not exist", axisName );
                    return nullptr;
                }
                if ( pAxis->_arity != arity )
                {
                    SW_LOG_ERROR_ID( "ResourceTags.WrongArity", "Resource tag axis '%#' holds %# - use %#", axisName, pAxis->_arity == ResourceTagArity::Single ? "one value" : "several values",
                                     pAxis->_arity == ResourceTagArity::Single ? "setAxisValue" : "setAxisValues / setTagActive" );
                    return nullptr;
                }
                return pAxis;
            }
        };
    } // namespace

    SW_CONDITION( resourceTag, ConditionNameArg, &ResourceTagsInternal::hasTag );
} // namespace sw

namespace sw
{
    ResourceTagSet::ResourceTagSet()
        : _listTag{}
    {
    }

    bool ResourceTagSet::contains( string_view tag ) const
    {
        return VectorUtil::containsIf( _listTag, [tag]( const string& existing )
        { return existing == tag; } );
    }

    void ResourceTagSet::add( string_view tag )
    {
        if ( contains( tag ) )
            return;
        _listTag.emplace_back( tag );
        VectorUtil::sort( _listTag );
    }

    void ResourceTagSet::remove( string_view tag )
    {
        (void)VectorUtil::removeIf( _listTag, [tag]( const string& existing )
        { return existing == tag; } );
    }

    void ResourceTagSet::removeAxis( string_view axisName )
    {
        (void)VectorUtil::removeIf( _listTag, [axisName]( const string& existing )
        { return existing.size() > axisName.size() && StringUtil::startsWith( existing, axisName ) && existing[axisName.size()] == ResourceTags::kSeparator; } );
    }

    void ResourceTagSet::overrideAxes( const ResourceTagSet& overrideTags )
    {
        for ( const string& tag : overrideTags._listTag )
        {
            const size_t separator = tag.find( ResourceTags::kSeparator );
            SW_ASSERT( separator != string::npos && "a resource tag is axis.value" );
            removeAxis( string_view( tag ).substr( 0, separator ) );
            add( tag );
        }
    }

    bool ResourceTagTest::evaluate( const ResourceTagSet& tags ) const
    {
        GameplayContext context;
        context.set<ResourceTagSlot>( &tags );
        return _expr.evaluate( context );
    }

    const ResourceTagAxis* ResourceTags::findAxis( string_view axisName )
    {
        for ( const ResourceTagAxis& axis : ResourceTagsInternal::kArrAxis )
        {
            if ( axisName == axis._pName )
                return &axis;
        }
        return nullptr;
    }

    const ResourceTagAxis* ResourceTags::findAxisOfTag( string_view tag )
    {
        const size_t separator = tag.find( kSeparator );
        if ( separator == string_view::npos || separator == 0 || separator + 1 >= tag.size() )
            return nullptr;
        return findAxis( tag.substr( 0, separator ) );
    }

    void ResourceTags::collectAxisNames( vector<string>& outListName )
    {
        for ( const ResourceTagAxis& axis : ResourceTagsInternal::kArrAxis )
        {
            outListName.emplace_back( axis._pName );
        }
    }

    bool ResourceTags::setAxisValue( string_view axisName, string_view value )
    {
        const ResourceTagAxis* pAxis = ResourceTagsInternal::findAxisFor( axisName, ResourceTagArity::Single );
        if ( pAxis == nullptr )
            return false;
        const expected<void> valid = value.empty() ? expected<void>{} : ResourceTagsInternal::isValidValue( *pAxis, value );
        if ( valid.has_value() == false )
        {
            SW_LOG_ERROR_ID( "ResourceTags.InvalidValue", "Resource tag not set: %# '%#'", valid.error().message(), valid.error().detail() );
            return false;
        }
        ResourceTagSet next = capture();
        next.removeAxis( axisName );
        if ( value.empty() == false )
            next.add( ResourceTagsInternal::makeTag( axisName, value ) );
        replace( next, nullptr );
        return true;
    }

    bool ResourceTags::setAxisValues( string_view axisName, const vector<string>& listValue )
    {
        const ResourceTagAxis* pAxis = ResourceTagsInternal::findAxisFor( axisName, ResourceTagArity::Multi );
        if ( pAxis == nullptr )
            return false;
        ResourceTagSet next = capture();
        next.removeAxis( axisName );
        for ( const string& value : listValue )
        {
            if ( const expected<void> valid = ResourceTagsInternal::isValidValue( *pAxis, value ); valid.has_value() == false )
            {
                SW_LOG_ERROR_ID( "ResourceTags.InvalidValue2", "Resource tags not set: %# '%#'", valid.error().message(), valid.error().detail() );
                return false;
            }
            next.add( ResourceTagsInternal::makeTag( axisName, value ) );
        }
        replace( next, nullptr );
        return true;
    }

    bool ResourceTags::setTagActive( string_view axisName, string_view value, bool bActive )
    {
        const ResourceTagAxis* pAxis = ResourceTagsInternal::findAxisFor( axisName, ResourceTagArity::Multi );
        if ( pAxis == nullptr )
            return false;
        if ( const expected<void> valid = ResourceTagsInternal::isValidValue( *pAxis, value ); valid.has_value() == false )
        {
            SW_LOG_ERROR_ID( "ResourceTags.InvalidValue3", "Resource tag not set: %# '%#'", valid.error().message(), valid.error().detail() );
            return false;
        }
        ResourceTagSet next = capture();
        const string   tag  = ResourceTagsInternal::makeTag( axisName, value );
        if ( bActive )
            next.add( tag );
        else
            next.remove( tag );
        replace( next, nullptr );
        return true;
    }

    ResourceTags::ResourceTags( ResourceComposition& composition )
        : _composition{ composition }
        , _mutex{}
        , _tags{}
    {
    }

    bool ResourceTags::setAxisTags( const ResourceTagSet& tags )
    {
        for ( const string& tag : tags.getTags() )
        {
            const size_t separator = tag.find( kSeparator );
            if ( separator == string::npos )
            {
                SW_LOG_ERROR_ID( "ResourceTags.NotATag", "Resource tag '%#' is not axis.value - tags not set", tag );
                return false;
            }
            const ResourceTagAxis* pAxis = ResourceTagsInternal::findAxisFor( string_view( tag ).substr( 0, separator ), ResourceTagArity::Single );
            if ( pAxis == nullptr )
                return false;
            if ( const expected<void> valid = ResourceTagsInternal::isValidValue( *pAxis, string_view( tag ).substr( separator + 1 ) ); valid.has_value() == false )
            {
                SW_LOG_ERROR_ID( "ResourceTags.InvalidValue4", "Resource tags not set: %# '%#'", valid.error().message(), valid.error().detail() );
                return false;
            }
        }
        replace( captureWith( tags ), nullptr );
        return true;
    }

    ResourceTagSet ResourceTags::capture() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _tags;
    }

    ResourceTagSet ResourceTags::captureWith( const ResourceTagSet& overrideTags ) const
    {
        ResourceTagSet tags = capture();
        tags.overrideAxes( overrideTags );
        return tags;
    }

    ResourceTagSet ResourceTags::exchange( const ResourceTagSet& tags )
    {
        ResourceTagSet previous;
        replace( tags, &previous );
        return previous;
    }

    void ResourceTags::replace( const ResourceTagSet& next, ResourceTagSet* pOutPrevious )
    {
        vector<string> listChanged;
        {
            ScopedLock<mutex> lock{ _mutex };
            for ( const string& tag : _tags.getTags() )
            {
                if ( next.contains( tag ) == false )
                    listChanged.push_back( tag );
            }
            for ( const string& tag : next.getTags() )
            {
                if ( _tags.contains( tag ) == false )
                    listChanged.push_back( tag );
            }
            if ( pOutPrevious != nullptr )
                *pOutPrevious = _tags;
            _tags = next;
        }
        if ( listChanged.empty() == false )
            _composition.notifyTagsChanged( listChanged );
    }

    bool ResourceTags::compileTest( string_view text, string_view sourceName, ResourceTagTest& outTest )
    {
        outTest = ResourceTagTest{};
        if ( StringUtil::trim( text ).empty() )
        {
            SW_LOG_ERROR_ID( "ResourceCondition.Invalid", "%#: a resource condition has an empty test - write the tags it asks for", sourceName );
            return false;
        }
        ResourceTagTest compiled;
        if ( ConditionExpr::compile( text, sourceName, compiled._expr, kConditionName ) == false )
            return false; // 까닭은 `ConditionExpr.Invalid` 가 남겼다
        const ConditionEntry* pTagEntry = ConditionRegistry::find( kConditionName );
        SW_ASSERT( pTagEntry != nullptr && "the resourceTag condition is registered in this file" );
        string error;
        compiled._expr.forEachCall( [&]( ConditionFunction function, const void* pArgs )
        {
            if ( error.empty() == false )
                return;
            if ( function != pTagEntry->_function )
            {
                error = "it calls a game condition - a resource condition only asks for tags (axis.value)";
                return;
            }
            const string_view      tag   = static_cast<const ConditionNameArg*>( pArgs )->_name.c_str();
            const ResourceTagAxis* pAxis = findAxisOfTag( tag );
            if ( pAxis == nullptr )
            {
                vector<string> listAxis;
                collectAxisNames( listAxis );
                string axisText;
                for ( const string& axis : listAxis )
                {
                    if ( !axisText.empty() )
                    {
                        axisText += ", ";
                    }
                    axisText += axis;
                }
                StringBuilder<constant::buffer::kMax256> errorBuilder;
                errorBuilder.appendFormat( "'%#' is not axis.value of a known axis (%#)", tag, axisText.c_str() );
                error = string( errorBuilder.view() );
                return;
            }
            if ( const expected<void> valid = ResourceTagsInternal::isValidValue( *pAxis, tag.substr( string_view( pAxis->_pName ).size() + 1 ) ); valid.has_value() == false )
            {
                error = string( valid.error().message().data(), valid.error().message().size() ) + " '" + string( valid.error().detail().data(), valid.error().detail().size() ) + "'";
                return;
            }
            if ( pAxis->_timing == ResourceTagTiming::Load )
            {
                compiled._timing = ResourceTagTiming::Load;
                if ( VectorUtil::contains( compiled._listLoadTag, string{ tag } ) == false )
                    compiled._listLoadTag.emplace_back( tag );
            }
        } );
        if ( error.empty() == false )
        {
            SW_LOG_ERROR_ID( "ResourceCondition.Invalid2", "%#: invalid resource condition '%#': %#", sourceName, text, error.c_str() );
            return false;
        }
        outTest = std::move( compiled );
        return true;
    }
} // namespace sw
