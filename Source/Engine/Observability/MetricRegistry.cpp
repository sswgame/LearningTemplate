#include "pch.h"

#include "Engine/Observability/MetricRegistry.h"

#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/MonotonicClock.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace sw
{
    SW_LOG_CALLER( "MetricRegistry" );

    namespace
    {
        struct MetricRegistryInternal
        {
            static uint64 toBits( float64 value )
            {
                uint64 bits = 0;
                Memory::copy( &bits, &value, sizeof( bits ) );
                return bits;
            }

            static float64 fromBits( uint64 bits )
            {
                float64 value = 0.0;
                Memory::copy( &value, &bits, sizeof( value ) );
                return value;
            }

            static void addFloat( atomic<uint64>& inoutBits, float64 delta )
            {
                uint64 expected = inoutBits.load( std::memory_order_relaxed );
                while ( inoutBits.compare_exchange_weak( expected, toBits( fromBits( expected ) + delta ), std::memory_order_relaxed ) == false )
                {
                }
            }

            static bool isNameChar( utf8 character, bool bFirst, bool bAllowColon )
            {
                const bool bLetter = ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' ) || character == '_' ||
                                     ( bAllowColon && character == ':' );
                return bLetter || ( bFirst == false && '0' <= character && character <= '9' );
            }

            static bool isValidName( string_view name, bool bAllowColon )
            {
                if ( name.empty() )
                    return false;
                for ( size_t charIndex = 0; charIndex < name.size(); ++charIndex )
                {
                    if ( isNameChar( name[charIndex], charIndex == 0, bAllowColon ) == false )
                        return false;
                }
                return true;
            }

            /** @brief 역슬래시 · 줄바꿈(그리고 @p bQuote 면 따옴표)을 이스케이프해 붙입니다. HELP 는 따옴표를 이스케이프하지 않는다(0.0.4). */
            static void appendEscaped( string& outText, string_view value, bool bQuote )
            {
                for ( const utf8 character : value )
                {
                    if ( character == '\\' )
                        outText += "\\\\";
                    else if ( character == '\n' )
                        outText += "\\n";
                    else if ( bQuote && character == '"' )
                        outText += "\\\"";
                    else
                        outText.push_back( character );
                }
            }

            static void appendUint( string& outText, uint64 value )
            {
                utf8                       arrBuffer[constant::kMaxBuffer32];
                const std::to_chars_result result = std::to_chars( arrBuffer, arrBuffer + sizeof( arrBuffer ), value );
                outText.append( arrBuffer, result.ptr );
            }

            /** @brief 최단 왕복 표현(`0.25` · `1` · `2.5`)으로 붙입니다. NaN · 무한은 Prometheus 철자. */
            static void appendFloat( string& outText, float64 value )
            {
                if ( std::isnan( value ) )
                {
                    outText += "NaN";
                    return;
                }
                if ( std::isinf( value ) )
                {
                    outText += value > 0.0 ? "+Inf" : "-Inf";
                    return;
                }
                utf8                       arrBuffer[constant::kMaxBuffer64];
                const std::to_chars_result result = std::to_chars( arrBuffer, arrBuffer + sizeof( arrBuffer ), value );
                outText.append( arrBuffer, result.ptr );
            }

            /** @brief `name<suffix>{labels,extra}` 를 붙입니다(라벨이 모두 비면 중괄호 없이). */
            static void appendSeriesName( string& outText, string_view name, string_view suffix, string_view labelText, string_view extraLabel )
            {
                outText += name;
                outText += suffix;
                if ( labelText.empty() && extraLabel.empty() )
                    return;
                outText.push_back( '{' );
                outText += labelText;
                if ( labelText.empty() == false && extraLabel.empty() == false )
                    outText.push_back( ',' );
                outText += extraLabel;
                outText.push_back( '}' );
            }

            static void appendSample( string& outText, string_view name, string_view suffix, string_view labelText, string_view extraLabel )
            {
                appendSeriesName( outText, name, suffix, labelText, extraLabel );
                outText.push_back( ' ' );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    MetricCounter::MetricCounter()
        : _value{ 0 }
    {
    }

    MetricGauge::MetricGauge()
        : _bits{ MetricRegistryInternal::toBits( 0.0 ) }
    {
    }

    void MetricGauge::set( float64 value ) { _bits.store( MetricRegistryInternal::toBits( value ), std::memory_order_relaxed ); }

    void MetricGauge::add( float64 delta ) { MetricRegistryInternal::addFloat( _bits, delta ); }

    float64 MetricGauge::getValue() const { return MetricRegistryInternal::fromBits( _bits.load( std::memory_order_relaxed ) ); }

    MetricHistogram::MetricHistogram( const vector<float64>& listUpperBound )
        : _listUpperBound( listUpperBound.begin(), listUpperBound.end() )
        , _arrBucketValue{ make_unique<atomic<uint64>[]>( listUpperBound.size() + 1 ) }
        , _count{ 0 }
        , _sumBits{ MetricRegistryInternal::toBits( 0.0 ) }
    {
        for ( size_t bucketIndex = 0; bucketIndex <= _listUpperBound.size(); ++bucketIndex )
            _arrBucketValue[bucketIndex].store( 0, std::memory_order_relaxed );
    }

    void MetricHistogram::observe( float64 value )
    {
        const auto   found       = std::lower_bound( _listUpperBound.begin(), _listUpperBound.end(), value );
        const size_t bucketIndex = static_cast<size_t>( found - _listUpperBound.begin() );
        _arrBucketValue[bucketIndex].fetch_add( 1, std::memory_order_relaxed );
        _count.fetch_add( 1, std::memory_order_relaxed );
        MetricRegistryInternal::addFloat( _sumBits, value );
    }

    uint64 MetricHistogram::getBucketValue( int32 bucketIndex ) const
    {
        if ( bucketIndex < 0 || bucketIndex > getBucketCount() )
            return 0;
        return _arrBucketValue[static_cast<size_t>( bucketIndex )].load( std::memory_order_relaxed );
    }

    float64 MetricHistogram::getSum() const { return MetricRegistryInternal::fromBits( _sumBits.load( std::memory_order_relaxed ) ); }

    ScopedMetricTimer::ScopedMetricTimer( MetricHistogram* pHistogram )
        : _startNanoseconds{ pHistogram != nullptr ? MonotonicClock::nowNanoseconds() : 0 }
        , _pHistogram{ pHistogram }
    {
    }

    ScopedMetricTimer::~ScopedMetricTimer()
    {
        if ( _pHistogram != nullptr )
            _pHistogram->observe( static_cast<float64>( MonotonicClock::nowNanoseconds() - _startNanoseconds ) * 1.0e-9 );
    }

    MetricRegistry::MetricRegistry()
        : _listFamily{}
        , _mutex{}
    {
    }

    MetricRegistry::~MetricRegistry() = default;

    MetricCounter* MetricRegistry::registerCounter( string_view name, string_view help, const vector<MetricLabel>& listLabel )
    {
        Series* pSeries = findOrAddSeries( Kind::Counter, name, help, listLabel, nullptr );
        return pSeries != nullptr ? pSeries->_counter.get() : nullptr;
    }

    MetricGauge* MetricRegistry::registerGauge( string_view name, string_view help, const vector<MetricLabel>& listLabel )
    {
        Series* pSeries = findOrAddSeries( Kind::Gauge, name, help, listLabel, nullptr );
        return pSeries != nullptr ? pSeries->_gauge.get() : nullptr;
    }

    MetricHistogram* MetricRegistry::registerHistogram( string_view name, string_view help, const vector<float64>& listUpperBound, const vector<MetricLabel>& listLabel )
    {
        bool bAscending = listUpperBound.empty() == false;
        for ( size_t boundIndex = 1; bAscending && boundIndex < listUpperBound.size(); ++boundIndex )
            bAscending = listUpperBound[boundIndex - 1] < listUpperBound[boundIndex];
        if ( bAscending == false )
        {
            SW_LOG_ERROR( "Metric histogram '%#' needs strictly ascending bucket bounds", name );
            return nullptr;
        }
        Series* pSeries = findOrAddSeries( Kind::Histogram, name, help, listLabel, &listUpperBound );
        return pSeries != nullptr ? pSeries->_histogram.get() : nullptr;
    }

    MetricRegistry::Series* MetricRegistry::findOrAddSeries( Kind kind, string_view name, string_view help, const vector<MetricLabel>& listLabel,
                                                             const vector<float64>* pUpperBound )
    {
        if ( MetricRegistryInternal::isValidName( name, true ) == false )
        {
            SW_LOG_ERROR( "Metric name '%#' is not a valid Prometheus name", name );
            return nullptr;
        }
        string labelText;
        for ( const MetricLabel& label : listLabel )
        {
            const bool bReserved = label._name.size() >= 2 && label._name[0] == '_' && label._name[1] == '_';
            if ( MetricRegistryInternal::isValidName( label._name, false ) == false || bReserved || label._name == "le" )
            {
                SW_LOG_ERROR( "Metric '%#' has an invalid label name '%#'", name, label._name.c_str() );
                return nullptr;
            }
            if ( labelText.empty() == false )
                labelText.push_back( ',' );
            labelText += label._name;
            labelText += "=\"";
            MetricRegistryInternal::appendEscaped( labelText, label._value, true );
            labelText.push_back( '"' );
        }

        std::scoped_lock<mutex> lock{ _mutex };
        Family*                 pFamily = nullptr;
        for ( unique_ptr<Family>& family : _listFamily )
        {
            if ( family->_name == name )
            {
                pFamily = family.get();
                break;
            }
        }
        if ( pFamily == nullptr )
        {
            pFamily        = _listFamily.emplace_back( make_unique<Family>() ).get();
            pFamily->_name = string( name );
            pFamily->_help = string( help );
            pFamily->_kind = kind;
            for ( const MetricLabel& label : listLabel )
                pFamily->_listLabelName.push_back( label._name );
            if ( pUpperBound != nullptr )
                pFamily->_listUpperBound = *pUpperBound;
        }
        bool bSameShape = pFamily->_kind == kind && pFamily->_listLabelName.size() == listLabel.size();
        for ( size_t labelIndex = 0; bSameShape && labelIndex < listLabel.size(); ++labelIndex )
            bSameShape = pFamily->_listLabelName[labelIndex] == listLabel[labelIndex]._name;
        if ( bSameShape && pUpperBound != nullptr )
            bSameShape = pFamily->_listUpperBound == *pUpperBound;
        if ( bSameShape == false )
        {
            SW_LOG_ERROR( "Metric '%#' was registered before with another kind, label names or buckets", name );
            return nullptr;
        }
        for ( unique_ptr<Series>& series : pFamily->_listSeries )
        {
            if ( series->_labelText == labelText )
                return series.get();
        }
        Series* pSeries     = pFamily->_listSeries.emplace_back( make_unique<Series>() ).get();
        pSeries->_labelText = std::move( labelText );
        switch ( kind )
        {
            case Kind::Counter:
            {
                pSeries->_counter = make_unique<MetricCounter>();
                break;
            }
            case Kind::Gauge:
            {
                pSeries->_gauge = make_unique<MetricGauge>();
                break;
            }
            case Kind::Histogram:
            {
                pSeries->_histogram = sw::make_unique<MetricHistogram>( *pUpperBound );
                break;
            }
        }
        return pSeries;
    }

    void MetricRegistry::writePrometheusText( string& outText ) const
    {
        static constexpr const utf8* kArrTypeName[] = { "counter", "gauge", "histogram" };

        std::scoped_lock<mutex> lock{ _mutex };
        vector<const Family*>   listFamily;
        listFamily.reserve( _listFamily.size() );
        for ( const unique_ptr<Family>& family : _listFamily )
            listFamily.push_back( family.get() );
        std::sort( listFamily.begin(), listFamily.end(), &MetricRegistry::isFamilyNameLess );
        for ( const Family* pFamily : listFamily )
        {
            outText += "# HELP ";
            outText += pFamily->_name;
            outText.push_back( ' ' );
            MetricRegistryInternal::appendEscaped( outText, pFamily->_help, false );
            outText += "\n# TYPE ";
            outText += pFamily->_name;
            outText.push_back( ' ' );
            outText += kArrTypeName[static_cast<size_t>( pFamily->_kind )];
            outText.push_back( '\n' );
            for ( const unique_ptr<Series>& series : pFamily->_listSeries )
            {
                switch ( pFamily->_kind )
                {
                    case Kind::Counter:
                    {
                        MetricRegistryInternal::appendSample( outText, pFamily->_name, "", series->_labelText, "" );
                        MetricRegistryInternal::appendUint( outText, series->_counter->getValue() );
                        outText.push_back( '\n' );
                        break;
                    }
                    case Kind::Gauge:
                    {
                        MetricRegistryInternal::appendSample( outText, pFamily->_name, "", series->_labelText, "" );
                        MetricRegistryInternal::appendFloat( outText, series->_gauge->getValue() );
                        outText.push_back( '\n' );
                        break;
                    }
                    case Kind::Histogram:
                    {
                        const MetricHistogram& histogram  = *series->_histogram;
                        uint64                 cumulative = 0;
                        for ( int32 bucketIndex = 0; bucketIndex <= histogram.getBucketCount(); ++bucketIndex )
                        {
                            cumulative += histogram.getBucketValue( bucketIndex );
                            string leLabel{ "le=\"" };
                            if ( bucketIndex < histogram.getBucketCount() )
                                MetricRegistryInternal::appendFloat( leLabel, histogram.getUpperBound( bucketIndex ) );
                            else
                                leLabel += "+Inf";
                            leLabel.push_back( '"' );
                            MetricRegistryInternal::appendSample( outText, pFamily->_name, "_bucket", series->_labelText, leLabel );
                            MetricRegistryInternal::appendUint( outText, cumulative );
                            outText.push_back( '\n' );
                        }
                        MetricRegistryInternal::appendSample( outText, pFamily->_name, "_sum", series->_labelText, "" );
                        MetricRegistryInternal::appendFloat( outText, histogram.getSum() );
                        outText.push_back( '\n' );
                        // 개수는 칸 합과 같은 값을 쓴다 — 긁는 동안의 증가로 +Inf 칸과 _count 가 어긋나지 않게.
                        MetricRegistryInternal::appendSample( outText, pFamily->_name, "_count", series->_labelText, "" );
                        MetricRegistryInternal::appendUint( outText, cumulative );
                        outText.push_back( '\n' );
                        break;
                    }
                }
            }
        }
    }

    int32 MetricRegistry::getSeriesCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        int32                   seriesCount = 0;
        for ( const unique_ptr<Family>& family : _listFamily )
            seriesCount += static_cast<int32>( family->_listSeries.size() );
        return seriesCount;
    }

    vector<float64> MetricRegistry::makeLatencyBounds() { return vector<float64>{ 0.001, 0.0025, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0 }; }
} // namespace sw
