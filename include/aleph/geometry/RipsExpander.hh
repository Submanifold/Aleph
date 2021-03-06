#ifndef ALEPH_GEOMETRY_RIPS_EXPANDER_HH__
#define ALEPH_GEOMETRY_RIPS_EXPANDER_HH__

#include <algorithm>
#include <iterator>
#include <list>
#include <limits>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <type_traits>
#include <vector>

namespace aleph
{

namespace geometry
{

/**
  @class RipsExpander
  @brief Class for performing Vietoris--Rips expansion of a simplicial complex

  This class is meant to be used as a functor for calculating the
  Vietoris--Rips complex of a given simplicial complex. For every
  simplex, its co-faces are evaluated and higher-dimensional ones
  are inserted into the complex.

  The implementation follows the paper:

  > Fast Construction of the Vietoris--Rips Complex
  > Afra Zomorodian
  > Computers & Graphics
  > Volume 34, Issue 3, June 2010, Pages 263--271

  Please refer to the network analysis example for a concise demo
  on how to use the class.

  @see network_analysis.cc
*/

template <class SimplicialComplex> class RipsExpander
{
public:
  using Simplex           = typename SimplicialComplex::ValueType;
  using DataType          = typename Simplex::DataType;
  using VertexType        = typename Simplex::VertexType;

  using SimplexContainer  = std::list<Simplex>;

  // Expansion ---------------------------------------------------------

  SimplicialComplex operator()( const SimplicialComplex& K, unsigned dimension )
  {
    std::set<VertexType> vertices;
    K.vertices( std::inserter( vertices,
                               vertices.begin() ) );

    auto lowerNeighbours = getLowerNeighbours( K );

    std::list<Simplex> simplices;

    for( auto&& vertex : vertices )
    {
      simplices.push_back( Simplex( vertex ) );

      if( lowerNeighbours.find( vertex ) != lowerNeighbours.end() )
      {
        addCofaces( simplices.back(),
                    lowerNeighbours,
                    lowerNeighbours.at( vertex ),
                    simplices,
                    dimension );
      }
    }

    // Re-assign weights of all simplices that are already present in
    // the original simplicial complex. This is certainly not a quick
    // algorithm.
    //
    // I am mitigating the performance impact somewhat by considering
    // only 0-simplices and 1-simplices. This may not be the smartest
    // idea, though.
    //
    // TODO: Performance improvements?
    // TODO: Higher-dimensional simplices & their weights?
    for( auto&& simplex : simplices )
    {
      if( simplex.dimension() <= 1 )
      {
        auto itSimplex = K.find( simplex );

        if( itSimplex != K.end() )
          simplex.setData( itSimplex->data() );
      }
    }

    return SimplicialComplex( simplices.begin(), simplices.end() );
  }

  // Weight assignment -------------------------------------------------

  SimplicialComplex assignMaximumWeight( const SimplicialComplex& K, unsigned minDimension = 1 )
  {
    SimplicialComplex S;

    for( auto it = K.begin_dimension(); it != K.end_dimension(); ++it )
    {
      auto s = *it;

      // Re-calculate the weight of the simplex because its
      // dimensionality requirement is not satisfied
      if( s.dimension() > minDimension )
      {
        auto w = s.data();

        for( auto itFace = s.begin_boundary(); itFace != s.end_boundary(); ++itFace )
        {
          auto itFaceInS = S.find( *itFace );
          if( itFaceInS != S.end() )
            w = std::max( w, itFaceInS->data() );
        }

        s.setData( w );
      }

      S.push_back( s );
    }

    return S;
  }

  template <class InputIterator> SimplicialComplex assignMaximumData( const SimplicialComplex& K, InputIterator begin, InputIterator end )
  {
    auto init    = std::numeric_limits<DataType>::lowest();
    auto functor = [] ( const DataType& a, const DataType& b )
    {
      return std::max(a, b);
    };

    return this->assignData( K, begin, end, init, functor );
  }

  /**
    Generic function for assigning data to simplices in the complex.
    Clients need to specify an initial value, which is normally either
    the maximum or minimum value of the given data type, as well as
    functor for determining the value to assign to a given simplex.
    Usually, this is either \c std::min() or \c std::max().
  */

  template <class InputIterator,
            class T,
            class Functor> SimplicialComplex assignData( const SimplicialComplex& K,
                                                         InputIterator begin, InputIterator end,
                                                         T init,
                                                         Functor functor )
  {
    SimplicialComplex S;

    {
      using DataType_ = typename std::iterator_traits<InputIterator>::value_type;
      static_assert( std::is_same<DataType, DataType_>::value, "Data types must agree" );
    }

    std::vector<DataType> dataValues( begin, end );

    // Extract all vertices and assign them indices based on their
    // lexicographical ordering in the complex. This is useful for
    // simplicial complexes that are using a non-contiguous vertex
    // indexing system.

    using IndexType = typename decltype(dataValues)::size_type;
    std::unordered_map<VertexType, IndexType> vertexToIndex;

    {
      std::set<VertexType> vertices;
      K.vertices( std::inserter( vertices, vertices.begin() ) );

      IndexType index = IndexType();
      for( auto&& vertex : vertices )
        vertexToIndex[vertex] = index++;
    }

    for( auto s : K )
    {
      DataType data = init;

      for( auto&& v : s )
        data = functor( data, dataValues[ vertexToIndex[v] ] );

      s.setData( data );
      S.push_back( s );
    }

    return S;
  }

private:

  using VertexContainer    = std::unordered_set<VertexType>;
  using LowerNeighboursMap = std::unordered_map<VertexType, VertexContainer>;

  static void addCofaces( const Simplex& s,
                          const LowerNeighboursMap& lowerNeighboursMap,
                          const VertexContainer& neighbours,
                          SimplexContainer& simplices,
                          unsigned dimension )
  {
    if( s.dimension() >= dimension )
      return;

    for( auto&& neighbour : neighbours )
    {
      // Create new simplex that contains the new neighbouring vertex as an
      // additional vertex. This increases the dimension by one.
      std::vector<VertexType> vertices( s.begin(), s.end() );
      vertices.push_back( neighbour );

      // TODO: Set weight of new simplex; maybe this can be solved by
      // specifying an optional callback beforehand?

      simplices.push_back( Simplex( vertices.begin(), vertices.end() ) );

      if( lowerNeighboursMap.find( neighbour ) != lowerNeighboursMap.end() )
      {
        auto lowerNeighbours  = lowerNeighboursMap.at( neighbour );
        auto commonNeighbours = intersect( lowerNeighbours, neighbours );

        addCofaces( simplices.back(),
                    lowerNeighboursMap,
                    commonNeighbours,
                    simplices,
                    dimension );
      }
    }
  }

  static std::unordered_set<VertexType> intersect( const std::unordered_set<VertexType>& U,
                                                   const std::unordered_set<VertexType>& V )
  {
    auto uPtr = &U;
    auto vPtr = &V;

    std::unordered_set<VertexType> result;

    if( uPtr->size() > vPtr->size() )
      std::swap( uPtr, vPtr );

    for( auto&& u : *uPtr )
      if( vPtr->find( u ) != vPtr->end() )
        result.insert( u );

    return result;
  }

  static LowerNeighboursMap getLowerNeighbours( const SimplicialComplex& K )
  {
    LowerNeighboursMap lowerNeighbours;

    // We only need to traverse the 1-skeleton of the simplicial complex. By
    // adding edges, we automatically fill up all lower neighbours.

    auto&& pair = K.range(1);
    for( auto it = pair.first; it != pair.second; ++it )
    {
      auto&& u = *( it->begin()    ); // first vertex of edge
      auto&& v = *( it->begin() + 1); // second vertex of edge

      if( u < v )
        lowerNeighbours[v].insert( u );
      else
        lowerNeighbours[u].insert( v );
    }

    return lowerNeighbours;
  }
};

} // namespace geometry

} // namespace aleph

#endif
