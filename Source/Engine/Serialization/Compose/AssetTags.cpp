#include "pch.h"

#include "Engine/Serialization/Compose/AssetTags.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include "Engine/Behavior/ConditionRegistry.h"
#include "Engine/Serialization/Compose/AssetComposition.h"

namespace sw
{
    namespace
    {
        struct AssetTagsInternal
        {
            static constexpr const utf8* kArrPlatformValue[]    = { "windows", "linux", "android", "web" }; ///< `build::kPlatformName` 을 소문자로(플랫폼을 더하면 한 줄 — 빠지면 `Engine` 기동이 멈춘다)
            static constexpr const utf8* kArrScreenValue[]      = { "regular", "compact" };                 ///< UI 뷰포트 너비가 `UIScaleSettings::_compactMaxWidth` 보다 좁으면 compact(`UISystem::syncScreenTags`)
            static constexpr const utf8* kArrOrientationValue[] = { "landscape", "portrait" };              ///< UI 뷰포트가 세로로 길면 portrait
            static constexpr const utf8* kArrTargetValue[]      = { "client", "server" };                   ///< 팩 쿠킹 대상(`PackCookParams::_buildTarget`)과 같은 둘

            /** @brief 축 표입니다. 순서는 오류 문장 · 편집기 목록의 순서입니다. */
            static constexpr AssetTagAxis kArrAxis[] = {
                {   "platform", AssetTagTiming::Cook, AssetTagArity::Single,    kArrPlatformValue,    static_cast<uint32>( SW_COUNT_OF( kArrPlatformValue ) ),    AssetTagScope::Shared},
                {     "target", AssetTagTiming::Cook, AssetTagArity::Single,      kArrTargetValue,      static_cast<uint32>( SW_COUNT_OF( kArrTargetValue ) ),    AssetTagScope::Shared},
                {    "quality", AssetTagTiming::Load, AssetTagArity::Single,              nullptr,                                                          0, AssetTagScope::PerClient}, ///< 사용자 설정 `graphics.quality`(핸들러 `asset.tag`)
                {     "locale", AssetTagTiming::Load, AssetTagArity::Single,              nullptr,                                                          0, AssetTagScope::PerClient}, ///< 현지화의 지금 언어
                {     "region", AssetTagTiming::Load, AssetTagArity::Single,              nullptr,                                                          0,    AssetTagScope::Shared}, ///< 출시 지역(게임 코드가 넣는다)
                {      "layer", AssetTagTiming::Load,  AssetTagArity::Multi,              nullptr,                                                          0,    AssetTagScope::Shared}, ///< 켜진 씬 데이터 레이어(`SceneDataLayerSubsystem`)
                {    "feature", AssetTagTiming::Load,  AssetTagArity::Multi,              nullptr,                                                          0,    AssetTagScope::Shared}, ///< 게임이 켜는 기능 태그(행사 · 계절)
                {       "flag", AssetTagTiming::Load,  AssetTagArity::Multi,              nullptr,                                                          0,    AssetTagScope::Shared}, ///< 게임 플래그를 에셋에 비추는 태그(게임 코드가 켠다)
                {     "screen", AssetTagTiming::Load, AssetTagArity::Single,      kArrScreenValue,      static_cast<uint32>( SW_COUNT_OF( kArrScreenValue ) ), AssetTagScope::PerClient}, ///< 좁은 화면 재배치(UI 문서의 `_when="screen.compact"`)
                {"orientation", AssetTagTiming::Load, AssetTagArity::Single, kArrOrientationValue, static_cast<uint32>( SW_COUNT_OF( kArrOrientationValue ) ), AssetTagScope::PerClient}, ///< 가로 · 세로 배치
            };

            /** @brief 태그 조건입니다. 문맥의 태그 칸이 없으면 "모름" 이라 거짓입니다. */
            static bool hasTag( const GameplayContext& context, const ConditionNameArg& args )
            {
                const AssetTagSet* pTags = context.find<AssetTagSlot>();
                return pTags != nullptr && pTags->contains( args._name.c_str() );
            }

            static string makeTag( string_view axisName, string_view value )
            {
                string tag{ axisName };
                tag += AssetTags::kSeparator;
                tag += value;
                return tag;
            }

            /** @brief @p value 가 @p axis 의 값으로 쓸 수 있는지 봅니다. 아니면 그 까닭(대상은 `축.값`)입니다. */
            static expected<void> isValidValue( const AssetTagAxis& axis, string_view value )
            {
                if ( value.empty() || value.find( AssetTags::kSeparator ) != string_view::npos )
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
            static const AssetTagAxis* findAxisFor( string_view axisName, AssetTagArity arity )
            {
                const AssetTagAxis* pAxis = AssetTags::findAxis( axisName );
                if ( pAxis == nullptr )
                {
                    SW_LOG_ERROR_ID( "AssetTags.UnknownAxis", "Asset tag axis '%#' does not exist", axisName );
                    return nullptr;
                }
                if ( pAxis->_arity != arity )
                {
                    SW_LOG_ERROR_ID( "AssetTags.WrongArity", "Asset tag axis '%#' holds %# - use %#", axisName, pAxis->_arity == AssetTagArity::Single ? "one value" : "several values",
                                     pAxis->_arity == AssetTagArity::Single ? "setAxisValue" : "setAxisValues / setTagActive" );
                    return nullptr;
                }
                return pAxis;
            }
        };
    } // namespace

    SW_CONDITION( assetTag, ConditionNameArg, &AssetTagsInternal::hasTag );
} // namespace sw

namespace sw
{
    AssetTagSet::AssetTagSet()
        : _listTag{}
    {
    }

    bool AssetTagSet::contains( string_view tag ) const
    {
        return VectorUtil::containsIf( _listTag, [tag]( const string& existing )
        { return existing == tag; } );
    }

    void AssetTagSet::add( string_view tag )
    {
        if ( contains( tag ) )
            return;
        _listTag.emplace_back( tag );
        VectorUtil::sort( _listTag );
    }

    void AssetTagSet::remove( string_view tag )
    {
        (void)VectorUtil::removeIf( _listTag, [tag]( const string& existing )
        { return existing == tag; } );
    }

    void AssetTagSet::removeAxis( string_view axisName )
    {
        (void)VectorUtil::removeIf( _listTag, [axisName]( const string& existing )
        { return existing.size() > axisName.size() && StringUtil::startsWith( existing, axisName ) && existing[axisName.size()] == AssetTags::kSeparator; } );
    }

    void AssetTagSet::overrideAxes( const AssetTagSet& overrideTags )
    {
        for ( const string& tag : overrideTags._listTag )
        {
            const size_t separator = tag.find( AssetTags::kSeparator );
            SW_ASSERT( separator != string::npos && "a resource tag is axis.value" );
            removeAxis( string_view( tag ).substr( 0, separator ) );
            add( tag );
        }
    }

    bool AssetTagTest::evaluate( const AssetTagSet& tags ) const
    {
        GameplayContext context;
        context.set<AssetTagSlot>( &tags );
        return _expr.evaluate( context );
    }

    const AssetTagAxis* AssetTags::findAxis( string_view axisName )
    {
        for ( const AssetTagAxis& axis : AssetTagsInternal::kArrAxis )
        {
            if ( axisName == axis._pName )
                return &axis;
        }
        return nullptr;
    }

    const AssetTagAxis* AssetTags::findAxisOfTag( string_view tag )
    {
        const size_t separator = tag.find( kSeparator );
        if ( separator == string_view::npos || separator == 0 || separator + 1 >= tag.size() )
            return nullptr;
        return findAxis( tag.substr( 0, separator ) );
    }

    void AssetTags::collectAxisNames( vector<string>& outListName )
    {
        for ( const AssetTagAxis& axis : AssetTagsInternal::kArrAxis )
        {
            outListName.emplace_back( axis._pName );
        }
    }

    bool AssetTags::setAxisValue( string_view axisName, string_view value )
    {
        const AssetTagAxis* pAxis = AssetTagsInternal::findAxisFor( axisName, AssetTagArity::Single );
        if ( pAxis == nullptr )
            return false;
        const expected<void> valid = value.empty() ? expected<void>{} : AssetTagsInternal::isValidValue( *pAxis, value );
        if ( valid.has_value() == false )
        {
            SW_LOG_ERROR_ID( "AssetTags.InvalidValue", "Asset tag not set: %# '%#'", valid.error().message(), valid.error().detail() );
            return false;
        }
        AssetTagSet next = captureStored();
        next.removeAxis( axisName );
        if ( value.empty() == false )
            next.add( AssetTagsInternal::makeTag( axisName, value ) );
        replace( next, nullptr );
        return true;
    }

    bool AssetTags::setAxisValues( string_view axisName, const vector<string>& listValue )
    {
        const AssetTagAxis* pAxis = AssetTagsInternal::findAxisFor( axisName, AssetTagArity::Multi );
        if ( pAxis == nullptr )
            return false;
        AssetTagSet next = captureStored();
        next.removeAxis( axisName );
        for ( const string& value : listValue )
        {
            if ( const expected<void> valid = AssetTagsInternal::isValidValue( *pAxis, value ); valid.has_value() == false )
            {
                SW_LOG_ERROR_ID( "AssetTags.InvalidValue2", "Asset tags not set: %# '%#'", valid.error().message(), valid.error().detail() );
                return false;
            }
            next.add( AssetTagsInternal::makeTag( axisName, value ) );
        }
        replace( next, nullptr );
        return true;
    }

    bool AssetTags::setTagActive( string_view axisName, string_view value, bool bActive )
    {
        const AssetTagAxis* pAxis = AssetTagsInternal::findAxisFor( axisName, AssetTagArity::Multi );
        if ( pAxis == nullptr )
            return false;
        if ( const expected<void> valid = AssetTagsInternal::isValidValue( *pAxis, value ); valid.has_value() == false )
        {
            SW_LOG_ERROR_ID( "AssetTags.InvalidValue3", "Asset tag not set: %# '%#'", valid.error().message(), valid.error().detail() );
            return false;
        }
        AssetTagSet  next = captureStored();
        const string tag  = AssetTagsInternal::makeTag( axisName, value );
        if ( bActive )
            next.add( tag );
        else
            next.remove( tag );
        replace( next, nullptr );
        return true;
    }

    AssetTags::AssetTags( AssetComposition& composition )
        : _composition{ composition }
        , _mutex{}
        , _storedTags{}
        , _listContribution{}
        , _tags{}
        , _nextContributionID{ 1 }
    {
    }

    AssetTags::~AssetTags()
    {
        closeSubscriptions();
    }

    bool AssetTags::setAxisTags( const AssetTagSet& tags )
    {
        for ( const string& tag : tags.getTags() )
        {
            const size_t separator = tag.find( kSeparator );
            if ( separator == string::npos )
            {
                SW_LOG_ERROR_ID( "AssetTags.NotATag", "Asset tag '%#' is not axis.value - tags not set", tag );
                return false;
            }
            const AssetTagAxis* pAxis = AssetTagsInternal::findAxisFor( string_view( tag ).substr( 0, separator ), AssetTagArity::Single );
            if ( pAxis == nullptr )
                return false;
            if ( const expected<void> valid = AssetTagsInternal::isValidValue( *pAxis, string_view( tag ).substr( separator + 1 ) ); valid.has_value() == false )
            {
                SW_LOG_ERROR_ID( "AssetTags.InvalidValue4", "Asset tags not set: %# '%#'", valid.error().message(), valid.error().detail() );
                return false;
            }
        }
        AssetTagSet next = captureStored();
        next.overrideAxes( tags );
        replace( next, nullptr );
        return true;
    }

    AssetTagSet AssetTags::capture() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _tags;
    }

    AssetTagSet AssetTags::captureStored() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _storedTags;
    }

    AssetTagSet AssetTags::captureWith( const AssetTagSet& overrideTags ) const
    {
        AssetTagSet tags = capture();
        tags.overrideAxes( overrideTags );
        return tags;
    }

    AssetTagSet AssetTags::exchange( const AssetTagSet& tags )
    {
        AssetTagSet previous;
        replace( tags, &previous );
        return previous;
    }

    void AssetTags::replace( const AssetTagSet& next, AssetTagSet* pOutPrevious )
    {
        vector<string> listChanged;
        {
            ScopedLock<mutex> lock{ _mutex };
            if ( pOutPrevious != nullptr )
                *pOutPrevious = _storedTags;
            _storedTags = next;
            listChanged = rebuildTags();
        }
        if ( listChanged.empty() == false )
            _composition.notifyTagsChanged( listChanged );
    }

    vector<string> AssetTags::rebuildTags()
    {
        AssetTagSet next = _storedTags;
        for ( const Contribution& contribution : _listContribution )
        {
            for ( const string& tag : contribution._listTag )
            {
                next.add( tag );
            }
        }
        vector<string> listChanged;
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
        _tags = std::move( next );
        return listChanged;
    }

    Subscription AssetTags::addAxisValues( string_view axisName, const vector<string>& listValue )
    {
        const AssetTagAxis* pAxis = AssetTagsInternal::findAxisFor( axisName, AssetTagArity::Multi );
        if ( pAxis == nullptr || listValue.empty() )
            return {};
        Contribution contribution;
        for ( const string& value : listValue )
        {
            if ( const expected<void> valid = AssetTagsInternal::isValidValue( *pAxis, value ); valid.has_value() == false )
            {
                SW_LOG_ERROR_ID( "AssetTags.InvalidValue5", "Asset tags not added: %# '%#'", valid.error().message(), valid.error().detail() );
                return {};
            }
            contribution._listTag.push_back( AssetTagsInternal::makeTag( axisName, value ) );
        }
        vector<string> listChanged;
        uint64         id{ 0 };
        {
            ScopedLock<mutex> lock{ _mutex };
            id               = _nextContributionID++;
            contribution._id = id;
            _listContribution.push_back( std::move( contribution ) );
            listChanged = rebuildTags();
        }
        Subscription subscription = issueSubscription( id );
        if ( listChanged.empty() == false )
            _composition.notifyTagsChanged( listChanged );
        return subscription;
    }

    void AssetTags::releaseSubscription( uint64 id )
    {
        vector<string> listChanged;
        {
            ScopedLock<mutex> lock{ _mutex };
            (void)VectorUtil::removeIf( _listContribution, [id]( const Contribution& contribution )
            { return contribution._id == id; } );
            listChanged = rebuildTags();
        }
        if ( listChanged.empty() == false )
            _composition.notifyTagsChanged( listChanged );
    }

    bool AssetTags::compileTest( string_view text, string_view sourceName, AssetTagTest& outTest )
    {
        outTest = AssetTagTest{};
        if ( StringUtil::trim( text ).empty() )
        {
            SW_LOG_ERROR_ID( "AssetCondition.Invalid", "%#: a resource condition has an empty test - write the tags it asks for", sourceName );
            return false;
        }
        AssetTagTest compiled;
        if ( ConditionExpr::compile( text, sourceName, compiled._expr, kConditionName ) == false )
            return false; // 까닭은 `ConditionExpr.Invalid` 가 남겼다
        const ConditionEntry* pTagEntry = ConditionRegistry::find( kConditionName );
        SW_ASSERT( pTagEntry != nullptr && "the assetTag condition is registered in this file" );
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
            const string_view   tag   = static_cast<const ConditionNameArg*>( pArgs )->_name.c_str();
            const AssetTagAxis* pAxis = findAxisOfTag( tag );
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
            if ( const expected<void> valid = AssetTagsInternal::isValidValue( *pAxis, tag.substr( string_view( pAxis->_pName ).size() + 1 ) ); valid.has_value() == false )
            {
                StringBuilder<constant::buffer::kMax256> errorBuilder;
                errorBuilder.appendFormat( "%# '%#'", valid.error().message(), valid.error().detail() );
                error = string( errorBuilder.view() );
                return;
            }
            if ( pAxis->_timing == AssetTagTiming::Load )
            {
                compiled._timing = AssetTagTiming::Load;
                VectorUtil::addUnique( compiled._listLoadTag, tag );
            }
        } );
        if ( error.empty() == false )
        {
            SW_LOG_ERROR_ID( "AssetCondition.Invalid2", "%#: invalid resource condition '%#': %#", sourceName, text, error.c_str() );
            return false;
        }
        outTest = std::move( compiled );
        return true;
    }
} // namespace sw
