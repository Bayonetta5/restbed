/*
 * Copyright (c) 2013, 2014, 2015 Corvusoft
 */

//System Includes
#include <regex>
#include <vector>
#include <utility>
#include <sstream>
#include <stdexcept>

//Project Includes
#include "corvusoft/restbed/method.h"
#include "corvusoft/restbed/request.h"
#include "corvusoft/restbed/status_code.h"
#include "corvusoft/restbed/detail/request_builder_impl.h"

//External Includes
#include <corvusoft/framework/uri>
#include <corvusoft/framework/map>
#include <corvusoft/framework/string>
#include <corvusoft/framework/istream>
#include <corvusoft/framework/string_option>

//System Namespaces
using std::map;
using std::stod;
using std::string;
using std::vector;
using std::istream;
using std::multimap;
using std::make_pair;
using std::to_string;
using std::shared_ptr;
using std::invalid_argument;
using std::istreambuf_iterator;

//Project Namespaces
using restbed::Request;

//External Namespaces
using asio::ip::tcp;
using framework::Uri;
using framework::Map;
using framework::Bytes;
using framework::String;
using framework::IStream;
using framework::StringOption;

namespace restbed
{
    namespace detail
    {
        RequestBuilderImpl::RequestBuilderImpl( const shared_ptr< tcp::socket >& socket ) : RequestImpl( )
        {
            parse( socket );
        }
        
        RequestBuilderImpl::RequestBuilderImpl( const RequestBuilderImpl& original ) : RequestImpl( original )
        {
            return;
        }
        
        RequestBuilderImpl::~RequestBuilderImpl( void )
        {
            return;
        }
        
        Request RequestBuilderImpl::build( void ) const
        {
            return *this;
        }

        framework::Bytes RequestBuilderImpl::to_bytes( const Request& request )
        {
            string headers = String::format( "%s%s\r\n", generate_status_section( request ).data( ),
                                                         generate_header_section( request ).data( ) );

            Bytes bytes( headers.begin( ), headers.end( ) );
            Bytes body = request.get_body( );
            bytes.insert( bytes.end( ), body.begin( ), body.end( ) );

            return bytes;
        }
        
        void RequestBuilderImpl::parse( const shared_ptr< tcp::socket >& socket )
        {
            asio::error_code code;
            asio::streambuf* buffer = new asio::streambuf;
            asio::read_until( *socket, *buffer, "\r\n\r\n", code );

            if ( code )
            {
                throw asio::system_error( code );
            }

            istream stream( buffer );

            set_method( parse_http_method( stream ) );
            set_path( parse_http_path( stream ) );
            set_query_parameters( parse_http_query_parameters( stream ) );
            set_protocol( parse_http_protocol( stream ) );
            set_version( parse_http_version( stream ) );
            set_headers( parse_http_headers( stream ) );

            auto endpoint = socket->remote_endpoint( );
            auto address = endpoint.address( );
            string value = address.is_v4( ) ? address.to_string( ) : "[" + address.to_string( ) + "]:";
            value += ::to_string( endpoint.port( ) );
            set_origin( value );

            endpoint = socket->local_endpoint( );
            address = endpoint.address( );
            value = address.is_v4( ) ? address.to_string( ) : "[" + address.to_string( ) + "]:";
            value += ::to_string( endpoint.port( ) );
            set_destination( value );

            set_socket( socket, buffer );
        }

        void RequestBuilderImpl::set_path_parameters( const map< string, string >& parameters )
        {
            RequestImpl::set_path_parameters( parameters );
        }
        
        RequestBuilderImpl& RequestBuilderImpl::operator =( const RequestBuilderImpl& value )
        {
            *this = value;
            
            return *this;
        }

        string RequestBuilderImpl::generate_path_section( const Request& request )
        {
            string section = request.get_path( );

            auto query_parameters = request.get_query_parameters( );

            if ( not query_parameters.empty( ) )
            {
                section += "?";

                for ( auto parameter : query_parameters )
                {
                    section += String::format( "%s=%s&", parameter.first.data( ), parameter.second.data( ) );
                }
            }

            return String::trim_lagging( section, "&" );
        }

        string RequestBuilderImpl::generate_status_section( const Request& request )
        {
            return String::format( "%s %s %s/%.1f\r\n", request.get_method( ).to_string( ).data( ),
                                                        generate_path_section( request ).data( ),
                                                        request.get_protocol( ).data( ),
                                                        request.get_version( ) );
        }

        string RequestBuilderImpl::generate_header_section( const Request& request )
        {
            string section = String::empty;

            for ( auto header : request.get_headers( ) )
            {
                section += String::format( "%s: %s\r\n", header.first.data( ), header.second.data( ) );
            }

            return section;
        }
        
        double RequestBuilderImpl::parse_http_version( istream& socket )
        {
            string version = String::empty;
            
            socket >> version;
            socket.ignore( 2 );
            
            double result = 0;
            
            try
            {
                result = stod( version );
            }
            catch ( const invalid_argument& ia )
            {
                throw StatusCode::BAD_REQUEST;
            }
            
            return result;
        }
        
        string RequestBuilderImpl::parse_http_path( istream& socket )
        {
            string path = String::empty;
            
            for ( char character = socket.get( ); character not_eq ' ' and character not_eq '?'; character = socket.get( ) )
            {
                path.push_back( character );
            }
            
            return Uri::decode( path );
        }
        
        string RequestBuilderImpl::parse_http_method( istream& socket )
        {
            string method = String::empty;
            
            socket >> method;
            socket.ignore( 1 );
            
            return method;
        }
        
        string RequestBuilderImpl::parse_http_protocol( istream& socket )
        {
            string protocol = String::empty;
            
            for ( char character = socket.get( ); character not_eq '/'; character = socket.get( ) )
            {
                protocol.push_back( character );
            }
            
            return String::trim( protocol );
        }
        
        multimap< string, string > RequestBuilderImpl::parse_http_headers( istream& socket )
        {
            multimap< string, string > headers;
            
            string header = String::empty;
            
            while ( getline( socket, header ) and header not_eq "\r" )
            {
                header.erase( header.length( ) - 1 );
                
                string::size_type index = header.find_first_of( ':' );
                
                string name = String::trim( header.substr( 0, index ) );
                
                string value = String::trim( header.substr( index + 1 ) );
                
                headers.insert( make_pair( name, value ) );
            }
            
            return headers;
        }
        
        multimap< string, string > RequestBuilderImpl::parse_http_query_parameters( istream& socket )
        {
            multimap< string, string > parameters;
            
            char previous_byte = IStream::reverse_peek( socket );
            
            if ( previous_byte == '?' )
            {
                string query_string = String::empty;
                
                socket >> query_string;
                
                const auto& query = String::split( query_string, '&' );
                
                for ( const auto& parameter : query )
                {
                    string::size_type index = parameter.find_first_of( '=' );

                    string name = Uri::decode_parameter( parameter.substr( 0, index ) );
                    string value = Uri::decode_parameter( parameter.substr( index + 1, parameter.length( ) ) );

                    parameters.insert( make_pair( name, value ) );
                }
            }
            
            return parameters;
        }
    }
}
