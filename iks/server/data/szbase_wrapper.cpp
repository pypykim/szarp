#include "szbase_wrapper.h"
#include "sz4/exceptions.h"

#include <boost/lexical_cast.hpp>

#include <conversion.h>

SzbaseObserverImpl::SzbaseObserverImpl( TParam* param , sz4::base* base , std::function<void( void )> callback )
									  : param( param ) , base( base ) , callback( callback )
{
	base->register_observer( this , std::vector<TParam*>{ param } );
}

void SzbaseObserverImpl::param_data_changed( TParam* )
{
	callback();
}

SzbaseObserverImpl::~SzbaseObserverImpl()
{
	base->deregister_observer( this , std::vector<TParam*>{ param } );
}


bool SzbaseWrapper::initialized = false;
boost::filesystem::path SzbaseWrapper::szarp_dir;
sz4::base* SzbaseWrapper::base;

bool SzbaseWrapper::init( const std::string& _szarp_dir )
{
	if( initialized )
		return boost::filesystem::path(_szarp_dir) == szarp_dir;

	szarp_dir = _szarp_dir;

	IPKContainer::Init( szarp_dir.wstring(), szarp_dir.wstring(), L"pl_PL" );

	base = new sz4::base( szarp_dir.wstring() , IPKContainer::GetObject() );

	initialized = true;

	return true;
}

SzbaseWrapper::SzbaseWrapper( const std::string& base )
	: base_name(base)
{
	std::wstring wbp( base_name.begin() , base_name.end() );
	IPKContainer::GetObject()->GetConfig( wbp );
}

std::wstring SzbaseWrapper::convert_string( const std::string& str ) const
{
	std::basic_string<unsigned char> ubp( str.begin() , str.end() );
	return SC::U2S( ubp );
}

time_t SzbaseWrapper::get_latest(
			const std::string& param ,
			ProbeType type ) const
	throw( szbase_init_error, szbase_get_value_error )
{
	if( !SzbaseWrapper::is_initialized() )
		throw szbase_init_error("Szbase not initialized");

	TParam* tparam = IPKContainer::GetObject()->GetParam( convert_string( base_name + ":" + param ) );
	if( !tparam )
		throw szbase_get_value_error( "Cannot get latest time of param " + param + ", param not found" );

	unsigned t;
	try {
		base->get_last_time( tparam, t );
	} catch( sz4::exception& e) {
		throw szbase_get_value_error( "Cannot get latest time of param " + param + ": " + e.what() );
	}
	
	/**
	 * Round by hand because search returns probes rounded to 
	 * either 10min or 10sec, not to exact pt
	 */
	return t == unsigned( -1 ) ? -1 : round( t , type );
}

double SzbaseWrapper::get_avg(
			const std::string& param ,
			time_t time ,
			ProbeType type ) const
	throw( szbase_init_error, szbase_get_value_error )
{
	if( !SzbaseWrapper::is_initialized() )
		throw szbase_init_error("Szbase not initialized");

	TParam* tparam = IPKContainer::GetObject()->GetParam( convert_string( base_name + ":" + param ) );
	if( !tparam )
		throw szbase_get_value_error( "Cannot get value from param " + param + ", param not found" );

	sz4::weighted_sum<double, unsigned> sum;
	try {
		base->get_weighted_sum( tparam ,
                                unsigned( time ) ,
                                unsigned( next( time , type , 1 ) ) ,
                                type.get_szarp_pt() , 
                                sum );
	} catch( sz4::exception& e ) {
		throw szbase_get_value_error( "Cannot get value from param " + param + ": " + e.what() );
	}

	return sum.avg();
}

namespace {

class no_data_search_condition : public sz4::search_condition {
public:
	bool operator()(const short& v) const {
		return v != std::numeric_limits<short>::min();
	}

	bool operator()(const int& v) const {
		return v != std::numeric_limits<int>::min();

	}

	bool operator()(const float& v) const {
		return !isnanf(v);
	}

	bool operator()(const double& v) const {
		return !std::isnan(v);
	}
};

}

namespace
{

template<class T> std::string search_data_helper( sz4::base* base,
												  TParam* param,
												  const std::string& from ,
									  			  const std::string& to ,
									  			  SearchDir dir ,
									  			  ProbeType pt )
{
									 
	T _from, _to;
	try {
		_from = boost::lexical_cast<T>( from );
		_to   = boost::lexical_cast<T>( to );
	} catch( const boost::bad_lexical_cast& ) {
		throw szbase_error( "Invalid time specification from: " + from + " to: " + to );
	}

	T result = ( dir == SearchDir::LEFT ) ?
			   base->search_data_left ( param , _from , _to , pt.get_szarp_pt() , no_data_search_condition() ) :
			   base->search_data_right( param , _from , _to , pt.get_szarp_pt() , no_data_search_condition() ) ;

	return boost::lexical_cast<std::string>(result);
}

}


std::string SzbaseWrapper::search_data( const std::string& param ,
									    const std::string& from ,
										const std::string& to ,
										TimeType time_type ,
										SearchDir dir ,
										ProbeType pt
										) const
	throw( szbase_init_error, szbase_error )
{

	TParam* tparam = IPKContainer::GetObject()->GetParam( convert_string( base_name + ":" + param ) );
	if( !tparam )
		throw szbase_error( "Param " + param + ", does not exist." );


	switch (time_type) {
		case TimeType::NANOSECOND:
			return search_data_helper<sz4::nanosecond_time_t>( base , tparam , from , to , dir , pt );
		case TimeType::SECOND:
			return search_data_helper<sz4::second_time_t>    ( base , tparam , from , to , dir , pt );
	}

	/* NOT REACHED */
	return std::string();
}

namespace {

template<class value_time, class time_type>
std::ostream& get_data( sz4::base*       base ,
					  TParam*          param ,
					  time_type        from ,
					  time_type        to ,
					  SZARP_PROBE_TYPE pt ,
					  std::ostream&    os )
{
	bool first = true;

	while ( from < to )
	{
		time_type next = szb_move_time( from , 1 , pt , 0 );

		sz4::weighted_sum< value_time, time_type > sum;
		base->get_weighted_sum( param , from , next , pt , sum );

		if (first)
			first = false;
		else
			os << " ";
	
		os << sum._sum() << " " << sum.weight() << " " << sum.no_data_weight() << " " << sum.fixed();

		from = next;
	}

	return os;
}

template<class time_type>
std::ostream& get_data( sz4::base*         base ,
					  TParam*            param ,
					  const std::string& from ,
					  const std::string& to ,
					  ValueType          vt ,
					  SZARP_PROBE_TYPE   pt ,
					  std::ostream&      os )
{

	time_type _from, _to;
	try{
		_from = boost::lexical_cast<time_type>( from );
		_to   = boost::lexical_cast<time_type>( to );
	} catch( const boost::bad_lexical_cast& ) {
		throw szbase_error( "Invalid time specification from: " + from + " to: " + to );
	}

	switch (vt) {
		case ValueType::DOUBLE:
			get_data<double       , time_type>( base , param , _from , _to , pt , os );
			break;
		case ValueType::FLOAT:
			get_data<float        , time_type>( base , param , _from , _to , pt , os );
			break;
		case ValueType::INT:
			get_data<int          , time_type>( base , param , _from , _to , pt , os );
			break;
		case ValueType::SHORT:
			get_data<short        , time_type>( base , param , _from , _to , pt , os );
			break;
	}

	return os;
}

}

std::string SzbaseWrapper::get_data( const std::string& param ,
									 const std::string& from ,
									 const std::string& to ,
									 ValueType value_type ,
									 TimeType time_type ,
									 ProbeType pt ) const
		throw( szbase_init_error, szbase_error )
{
	std::ostringstream ss;

	TParam* tparam = IPKContainer::GetObject()->GetParam( convert_string( base_name + ":" + param ) );
	if( !tparam )
		throw szbase_error( "Param " + param + ", does not exist." );

	try{
		switch (time_type) {
			case TimeType::NANOSECOND:
				::get_data<sz4::nanosecond_time_t>( base , tparam ,
								    from , to ,
								    value_type , pt.get_szarp_pt() ,
								    ss );
				break;
			case TimeType::SECOND:
				::get_data<sz4::second_time_t>    ( base , tparam ,
								    from , to ,
								    value_type , pt.get_szarp_pt() ,
								    ss );
				break;
		}
	} catch ( sz4::exception& e ) {
		throw szbase_error( "Cannot get data for param " + param + ": " + e.what() );
	}

	return ss.str();
}

SzbaseObserverToken SzbaseWrapper::register_observer( const std::string& param , std::function<void( void )> callback )
	throw( szbase_init_error, szbase_error )
{
	TParam* tparam = IPKContainer::GetObject()->GetParam( convert_string( base_name + ":" + param ) );
	if( !tparam )
		throw szbase_error( "Cannot register observer for param " + param + ", param not found" );

	return std::make_shared<SzbaseObserverImpl>( tparam , base , callback );
}

time_t SzbaseWrapper::next( time_t t , ProbeType pt , int num )
{
	return szb_move_time( t , num , pt.get_szarp_pt() , pt.get_len() );
}

time_t SzbaseWrapper::round( time_t t , ProbeType pt )
{
	return szb_round_time( t , pt.get_szarp_pt() , pt.get_len() );
}

