/* mockturtle: C++ logic network library
 * Copyright (C) 2018-2022  EPFL
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/*!
  \file dont_care_view.hpp
  \brief Implements methods to store external don't-cares

  \author Siang-Yun Lee
*/

#pragma once

#include "../traits.hpp"
#include "../algorithms/simulation.hpp"
#include "../algorithms/cnf.hpp"
#include "../utils/node_map.hpp"

#include <bill/sat/interface/common.hpp>

#include <vector>
#include <fstream>
#include <string>
#include <sstream> 

namespace mockturtle
{

namespace detail
{
  template<class Ntk>
  void parse_excdc( Ntk& ntk, std::string filename, uint32_t num_pis )
  {
    using signal = typename Ntk::signal;

    std::vector<signal> pis;
    for ( auto i = 0u; i < num_pis; ++i )
      pis.emplace_back( ntk.create_pi() );

    std::ifstream in( filename, std::ifstream::in );
    std::string line, str;

    // ignore 4 lines (file header)
    std::getline( in, line ); std::getline( in, line ); std::getline( in, line ); std::getline( in, line );
    
    std::vector<signal> terms; // output signals of big ANDs
    while ( std::getline( in, line ) && line.substr( 0, 4 ) != ".end" )
    {
      std::stringstream ss( line );
      std::getline( ss, str, ' ' );
      assert( str.length() == num_pis );

      signal top; // top signal of the AND under construction
      bool top_is_set = false;
      for ( auto i = 0u; i < num_pis; ++i )
      {
        if ( str[i] == '0' )
        {
          top = top_is_set ? ntk.create_and( top, !pis[i] ) : !pis[i];
          top_is_set = true;
        }
        else if ( str[i] == '1' )
        {
          top = top_is_set ? ntk.create_and( top, pis[i] ) : pis[i];
          top_is_set = true;
        }
      }

      if ( top_is_set )
      {
        terms.emplace_back( top );
      }

      std::getline( ss, str );
      assert( str[0] == '0' ); // assuming the truth table gives only the offset cubes
    }
    assert( terms.size() > 0 );

    signal top = terms[0]; // top signal of the OR under construction
    for ( auto i = 1u; i < terms.size(); ++i )
      top = ntk.create_or( top, terms[i] );

    ntk.create_po( !top );
  }
} // namespace detail

template<class Ntk>
class dont_care_view : public Ntk
{
public:
  using storage = typename Ntk::storage;
  using node = typename Ntk::node;
  using signal = typename Ntk::signal;

public:
  dont_care_view( Ntk const& ntk, Ntk const& cdc_ntk )
    : Ntk( ntk ), _excdc( cdc_ntk )
  {
    assert( ntk.num_pis() == cdc_ntk.num_pis() );
    assert( cdc_ntk.num_pos() == 1 );
  }

  dont_care_view( Ntk const& ntk, std::string dc_filename )
    : Ntk( ntk )
  {
    detail::parse_excdc( _excdc, dc_filename, ntk.num_pis() );
  }

  dont_care_view( dont_care_view<Ntk> const& ntk )
    : Ntk( ntk ), _excdc( ntk._excdc )
  {
  }

  dont_care_view<Ntk>& operator=( dont_care_view<Ntk> const& other )
  {
    Ntk::operator=( other );
    _excdc = other._excdc;
    return *this;
  }

  bool pattern_is_EXCDC( std::vector<bool> const& pattern ) const
  {
    assert( pattern.size() == this->num_pis() );

    default_simulator<bool> sim( pattern );
    auto const vals = simulate<bool>( _excdc, sim );
    return vals[0];
  }

  template<typename solver_t>
  void add_EXCDC_clauses( solver_t& solver ) const
  {
    using add_clause_fn_t = std::function<void( std::vector<bill::lit_type> const& )>;

    // topological order of the gates in _excdc is assumed
    node_map<bill::lit_type, Ntk> cdc_lits( _excdc );
    cdc_lits[_excdc.get_constant( false )] = bill::lit_type( 0, bill::lit_type::polarities::positive );
    if ( _excdc.get_node( _excdc.get_constant( false ) ) != _excdc.get_node( _excdc.get_constant( true ) ) )
    {
      cdc_lits[_excdc.get_constant( true )] = bill::lit_type( 0, bill::lit_type::polarities::negative );
    }
    _excdc.foreach_pi( [&]( auto const& n, auto i ) {
      cdc_lits[n] = bill::lit_type( i + 1, bill::lit_type::polarities::positive );
    } );

    _excdc.foreach_gate( [&]( auto const& n ){
      std::vector<bill::lit_type> child_lits;
      _excdc.foreach_fanin( n, [&]( auto const& f ) {
        child_lits.push_back( lit_not_cond( cdc_lits[f], _excdc.is_complemented( f ) ) );
      } );
      bill::lit_type node_lit = cdc_lits[n] = bill::lit_type( solver.add_variable(), bill::lit_type::polarities::positive );
      if ( _excdc.is_and( n ) )
      {
        detail::on_and<add_clause_fn_t>( node_lit, child_lits[0], child_lits[1], [&]( auto const& clause ) {
          solver.add_clause( clause );
        } );
      }
      else if ( _excdc.is_xor( n ) )
      {
        detail::on_xor<add_clause_fn_t>( node_lit, child_lits[0], child_lits[1], [&]( auto const& clause ) {
          solver.add_clause( clause );
        } );
      }
      else if ( _excdc.is_xor3( n ) )
      {
        detail::on_xor3<add_clause_fn_t>( node_lit, child_lits[0], child_lits[1], child_lits[2], [&]( auto const& clause ) {
          solver.add_clause( clause );
        } );
      }
      else if ( _excdc.is_maj( n ) )
      {
        detail::on_maj<add_clause_fn_t>( node_lit, child_lits[0], child_lits[1], child_lits[2], [&]( auto const& clause ) {
          solver.add_clause( clause );
        } );
      }
    } );

    _excdc.foreach_po( [&]( auto const& f ){
      solver.add_clause( {lit_not_cond( cdc_lits[f], !_excdc.is_complemented( f ) )} );
    } );
  }

private:
  Ntk _excdc;
}; /* dont_care_view */

template<class T>
dont_care_view( T const&, T const& ) -> dont_care_view<T>;

} // namespace mockturtle
