#ifndef ALEPH_GEOMETRY_COVER_TREE_HH__
#define ALEPH_GEOMETRY_COVER_TREE_HH__

#include <algorithm>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <ostream>
#include <queue>
#include <stack>
#include <stdexcept>
#include <vector>

// FIXME: remove after debugging
#include <iostream>

#include <cassert>
#include <cmath>

namespace aleph
{

namespace geometry
{

/**
  @class CoverTree
  @brief Generic cover tree data structure

  This class models a cover tree data structure, as described in the
  paper "Cover trees for nearest neighbor" by Beygelzimer et al.; an
  implementation is given at:

    http://hunch.net/~jl/projects/cover_tree/cover_tree.html

  This implementation attempts to be as generic as possible. It uses
  the simplified description of the cover tree, as given by Izbicki,
  Shelton in "Faster Cover Trees".
*/

template <class Point, class Metric> class CoverTree
{
public:

  /**
    Covering constant of the cover tree. It might make sense to change
    this later on in order to improve performance. Some papers set the
    constant to `1.3`.
  */

  constexpr static const double coveringConstant = 2.0;

  class Node
  {
  public:

    /** Creates a new node that stores a point */
    Node( const Point& point, long level )
      : _point( point )
      , _level( level )
    {
    }

    /** Calculates current covering distance of the node */
    double coveringDistance() const noexcept
    {
      return std::pow( coveringConstant, static_cast<double>( _level ) );
    }

    /** Calculates current separating distance of the node */
    double separatingDistance() const noexcept
    {
      return std::pow( coveringConstant, static_cast<double>( _level - 1 ) );
    }

    /** @returns true if the node is a leaf node */
    bool isLeaf() const noexcept
    {
      return _children.empty();
    }

    void insert( const Point& p )
    {
      auto d = Metric()( _point, p );

      std::cerr << __FUNCTION__ << ": Inserting " << p << "\n";
      std::cerr << __FUNCTION__ << ": Covering distance           = " << this->coveringDistance() << "\n";
      std::cerr << __FUNCTION__ << ": Distance from point to root = " << d << "\n";

      if( d > this->coveringDistance() )
      {
        while( d > 2 * this->coveringDistance() )
        {
          std::cerr << __FUNCTION__ << ": Distance is bigger than covering distance; need to raise level of tree\n";

          // -----------------------------------------------------------
          //
          // Find a leaf node that can become the new root node with
          // a raised level. If the tree only contains the root node
          // its level can be adjusted multiple times.

          std::stack<Node*> nodes;
          nodes.push( this );

          Node* leaf   = nullptr;
          Node* parent = nullptr;

          // Special case: the root itself is a leaf node; this happens
          // at the beginning of the insertion process and means that a
          // level adjustment has to be performed.
          if( this->isLeaf() )
          {
            this->_level += 1;
            continue;
          }

          while( !nodes.empty() )
          {
            auto node = nodes.top();
            nodes.pop();

            for( auto&& child : node->_children )
            {
              if( child->isLeaf() )
              {
                leaf   = child.get();
                parent = node;
                break;
              }
              else
                nodes.push( child.get() );
            }
          }

          std::cerr << __FUNCTION__ << ": Found leaf node " << leaf->_point << "\n";

          // There is no leaf, so there is nothing to do and we just
          // skip to the bottom where we add the current node as the
          // new root of the tree.
          if( !leaf )
          {
            std::cerr << __FUNCTION__ << ": Unable to identify leaf node\n";
            break;
          }

          assert( leaf );
          assert( parent );

          // Remove leaf from subtree ----------------------------------
          //
          // The previous tree does not contain the leaf node any more,
          // and it can be added as the new root node.

          std::unique_ptr<Node> leaf_ptr( nullptr );

          parent->_children.erase(
            std::remove_if(
              parent->_children.begin(), parent->_children.end(),
              [&leaf, &leaf_ptr] ( std::unique_ptr<Node>& child )
              {
                if( child.get() == leaf )
                {
                  leaf_ptr = std::move( child );
                  return true;
                }

                return false;
              }
            ),
            parent->_children.end()
          );

          // Make leaf node the new root node --------------------------
          //
          // Notice that this does increase the level of the current
          // root. This has to be adjusted for.

          auto oldRoot
            = std::unique_ptr<Node>( new Node( this->_point, this->_level ) );

          for( auto&& child : _children )
            oldRoot->_children.push_back( std::move( child ) );

          _point = leaf->_point;
          _level = _level + 1;

          _children.clear();
          _children.push_back( std::move( oldRoot ) );

          // Since the root of the tree changed, we also have to update
          // the distance calculation.
          d = Metric()( _point, p );

          std::cerr << __FUNCTION__ << ": " << leaf->_point << " is the new root\n";
        }

        // Make current point the new root -----------------------------
        //
        // So far, the new point has not yet been inserted into the
        // tree. This needs to be done now while the cover is valid
        // again.

        auto oldRoot
          = std::unique_ptr<Node>( new Node( this->_point, this->_level ) );

        for( auto&& child : _children )
          oldRoot->_children.push_back( std::move( child ) );

        _point = p;
        _level = _level + 1;

        _children.clear();
        _children.push_back( std::move( oldRoot ) );

        return;
      }

      return insert_( p );
    }

    /**
      Auxiliary function for performing the recursive insertion of a new
      node into the tree.
    */

    void insert_( const Point& p )
    {
      for( auto&& child : _children )
      {
        auto d = Metric()( child->_point, p );
        if( d <= child->coveringDistance() )
        {
          std::cerr << __FUNCTION__ << ": Recursive enumeration of the tree\n";

          // We found a node in which the new point can be inserted
          // *without* violating the covering invariant.
          child->insert_( p );
          return;
        }
      }

      // If we reach this point, we are sufficiently deep in to the tree
      // to make the new point a child of the current node. This code is
      // only reached when the recursive enumeration has finished!
      //
      // Add the new point as a child of the current root node. Note the
      // level adjustment.
      _children.push_back( std::unique_ptr<Node>( new Node( p, _level - 1 ) ) );
    }

    Point _point; //< The point stored in the node
    long  _level; //< The level of the node

    /**
      All children of the node. Their order depends on the insertion
      order into the data set.
    */

    std::vector< std::unique_ptr<Node> > _children;
  };

  /**
    Inserts a new point into the cover tree. If the tree is empty,
    the new point will become the root of the tree. Else, it shall
    be inserted according to the covering invariant.
  */

  void insert( const Point& p )
  {
    if( !_root )
      _root = std::unique_ptr<Node>( new Node(p,0) );
    else
      _root->insert( p );
  }

  /**
    Inserts a sequence of points into the cover tree. This is just
    a convenience function; no parallelization---or other advanced
    techniques---will be used.
  */

  template <class InputIterator> void insert( InputIterator begin, InputIterator end )
  {
    for( auto it = begin; it != end; ++it )
      this->insert( *it );
  }

  // Pretty-printing function for the tree; this is only meant for
  // debugging purposes and could conceivably be implemented using
  // `std::ostream`.
  void print( std::ostream& o )
  {
    std::queue<const Node*> nodes;
    nodes.push( _root.get() );

    while( !nodes.empty() )
    {
      {
        auto n = nodes.size();

        for( decltype(n) i = 0; i < n; i++ )
        {
          auto&& node = nodes.front();

          if( i == 0 )
            o << node->_level << ": ";
          else
            o << " ";

          o << node->_point;

          for( auto&& child : node->_children )
            nodes.push( child.get() );

          nodes.pop();
        }

        o << "\n";
      }
    }
  }

  // Tree access -------------------------------------------------------

  /**
    Gets all nodes according to their corresponding level. The order in
    which they are stored per level is essentially random and shouldn't
    be relied on.
  */

  std::multimap<long, Point> getNodesByLevel() const noexcept
  {
    std::multimap<long, Point> levelMap;

    std::queue<const Node*> nodes;
    nodes.push( _root.get() );

    while( !nodes.empty() )
    {
      auto n = nodes.size();

      for( decltype(n) i = 0; i < n; i++ )
      {
        auto&& node = nodes.front();

        levelMap.insert( std::make_pair( node->_level, node->_point ) );

        for( auto&& child : node->_children )
            nodes.push( child.get() );

        nodes.pop();
      }
    }

    return levelMap;
  }

  /**
    Gets all nodes (which are supposed to be unique) and assigns them
    their level.

    TODO: This is essentially the *same* implementation as the function
    above, albeit with switched nodes and labels in the map. This needs
    to be consolidated somehow.
  */

  std::map<Point, long> nodesToLevel() const noexcept
  {
    std::map<Point, long> levelMap;

    std::queue<const Node*> nodes;
    nodes.push( _root.get() );

    while( !nodes.empty() )
    {
      auto n = nodes.size();

      for( decltype(n) i = 0; i < n; i++ )
      {
        auto&& node = nodes.front();

        levelMap.insert( std::make_pair( node->_point, node->_level ) );

        for( auto&& child : node->_children )
            nodes.push( child.get() );

        nodes.pop();
      }
    }

    return levelMap;
  }

  /** Gets all points in BFS order */
  std::vector<Point> points() const noexcept
  {
    std::vector<Point> result;

    std::queue<const Node*> nodes;
    nodes.push( _root.get() );

    while( !nodes.empty() )
    {
      auto n = nodes.size();

      for( decltype(n) i = 0; i < n; i++ )
      {
        auto&& node = nodes.front();

        result.push_back( node->_point );

        for( auto&& child : node->_children )
          nodes.push( child.get() );

        nodes.pop();
      }
    }

    return result;
  }

  // Tree attributes ---------------------------------------------------

  /**
    @returns Level of the tree, i.e. the level of the root node. If no root
    node exists, a level of zero is returned. This is *not* the depth.
  */

  long level() const noexcept
  {
    if( _root )
      return _root->_level;
    else
      return 0;
  }

  // Validity checks ---------------------------------------------------
  //
  // These are called by debug code and tests to ensure that the cover
  // tree is correct.

  /**
    Checks the level invariant in the cover tree. The level invariant
    states that the level \f$l$ of the *direct* child of a node \f$p$
    is the level \f$l'-1$, where \f$l'$ is the level of \f$p$.
  */

  bool checkLevelInvariant() const noexcept
  {
    std::queue<const Node*> nodes;
    nodes.push( _root.get() );

    // Initialize the level artificially in order to simplify the code
    // below.
    long level = _root->_level + 1;

    while( !nodes.empty() )
    {
      auto n = nodes.size();

      for( decltype(n) i = 0; i < n; i++ )
      {
        auto&& node = nodes.front();

        if( node->_level != level - 1 )
          return false;

        for( auto&& child : node->_children )
          nodes.push( child.get() );

        nodes.pop();
      }

      level -= 1;
    }

    return true;
  }

  /**
    Checks the covering invariant in the cover tree. The covering
    invariant states that the distance between a child and parent
    node is bounded by the covering distance.
  */

  bool checkCoveringInvariant() const noexcept
  {
    std::queue<const Node*> nodes;
    nodes.push( _root.get() );

    while( !nodes.empty() )
    {
      auto n = nodes.size();
      for( decltype(n) i = 0; i < n; i++ )
      {
        auto&& parent = nodes.front();

        for( auto&& child : parent->_children )
        {
          auto d = Metric()( parent->_point, child->_point );
          if( d > parent->coveringDistance() )
          {
            std::cerr << __FUNCTION__ << ": Covering invariant is violated by ("
                      << parent->_point << "," << child->_point << "): "
                      << d << " > " << parent->coveringDistance() << "\n";

            return false;
          }

          nodes.push( child.get() );
        }

        // All children of the current parent node have been processed,
        // so we can remove it.
        nodes.pop();
      }
    }

    return true;
  }

  /**
    Checks the separating invariant in the cover tree. The separating
    invariant states that the distance between a child and its parent
    is larger than the separating distance.
  */

  bool checkSeparatingInvariant() const noexcept
  {
    std::queue<const Node*> nodes;
    nodes.push( _root.get() );

    while( !nodes.empty() )
    {
      auto n = nodes.size();
      for( decltype(n) i = 0; i < n; i++ )
      {
        auto&& parent = nodes.front();

        for( auto it1 = parent->_children.begin(); it1 != parent->_children.end(); ++it1 )
        {
          for( auto it2 = std::next(it1); it2 != parent->_children.end(); ++it2 )
          {
            // The distance between the two points must by necessity be
            // larger than the separating distance of their parent node
            // in order to satisfy the separating property.

            auto&& p = (*it1)->_point;
            auto&& q = (*it2)->_point;
            auto d   = Metric()(p, q);

            if( d <= parent->separatingDistance() )
            {
              std::cerr << __FUNCTION__ << ": Separating invariant is violated by ("
                        << p << "," << q << "): "
                        << d << " > " << parent->separatingDistance() << "\n";

              return false;
            }
          }

          // Add the child such that the next level of the tree can be
          // visited.
          nodes.push( it1->get() );
        }

        // All children of the current parent node have been processed,
        // so we can remove it.
        nodes.pop();
      }
    }

    return true;
  }

  /**
    Generic validity check of the tree. Combines *all* validity
    criteria, i.e. the level invariant, the covering invariant,
    and the separating invariant.
  */

  bool isValid() const noexcept
  {
    return    this->checkLevelInvariant()
           && this->checkCoveringInvariant()
           && this->checkSeparatingInvariant();
  }

  /**
    Checks whether the tree nodes are building a *harmonic cover*.
    A cover is harmonic if its level is smallest among all covers,
    and the distance of nodes to their parents is decreasing.

    The check requires one point for testing the harmonic property
    of the tree.
  */

  bool isHarmonic( const Point& p ) /* FIXME const */ noexcept
  {
    std::vector<double> distances;
    std::vector<double> ancestorDistances;

    auto current  = _root.get();
    auto previous = _root.get();

    while( current )
    {
      // Need to evaluate the distance to the current node *once* at
      // this point. Since we select another `current` node later on
      // it is ensured that we only store the distance *once*.
      auto d = Metric()( p, current->_point );
      if( d <= current->coveringDistance() )
        distances.push_back( static_cast<double>( d ) );

      for( auto&& child : current->_children )
      {
        auto d = Metric()( p, child->_point );
        if( d <= child->coveringDistance() )
        {
          ancestorDistances.push_back(
            Metric()( _root->_point, child->_point )
          );

          // Continue the recursion in the next level, using the current
          // node as a new root.
          current = child.get();
          break;
        }
      }

      if( current == previous )
        break;
      else
        previous = current;
    }

    auto harmonic =
      std::is_sorted( distances.rbegin(), distances.rend(),
        std::less_equal<double>()
      );

    for( auto&& d : ancestorDistances )
      std::cerr << "AD = " << d << "\n";

    if( !harmonic )
      std::cerr << __FUNCTION__ << ": " << current->_point << " is not harmonic\n";

    // FIXME: this is not the proper place for this check, but it is
    // easier at the moment
    if( distances.size() >= 2 )
    {
      auto maxDistance = *std::max_element( distances.begin(), distances.end() );
      auto l           = this->level();

      while( maxDistance <= this->coveringDistance( l ) )
        --l;

      ++l;

      // The cover condition can be improved by making the current point
      // the new root node and inserting points into this tree. We shall
      // use the *dumbest* way of doing this (for now) and simply insert
      // all nodes into the new tree, depending on their distance to the
      // new root.
      if( l < this->level() )
      {
        std::cerr << "Replacing root node with " << current->_point << "\n";

        auto allPoints = this->points();

        allPoints.erase(
          std::remove( allPoints.begin(), allPoints.end(), current->_point ),
          allPoints.end()
        );

        // Sort points in *descending* distance from the new root node
        std::sort( allPoints.begin(), allPoints.end(),
          [&current] ( const Point& p, const Point& q )
          {
            auto dp = Metric()( current->_point, p );
            auto dq = Metric()( current->_point, q );

            return dp > dq;
          }
        );

        _root =
          std::unique_ptr<Node>( new Node( current->_point, l ) );

        this->insert( allPoints.begin(), allPoints.end() );
      }
    }

    return harmonic;
  }

  bool checkDistance( const Point& p ) const noexcept
  {
    std::vector<double> edgeDistances;
    std::vector<double> rootDistances;

    auto current  = _root.get();
    auto previous = _root.get();

    while( current )
    {
      auto d = Metric()( p, current->_point );
      if( d <= current->coveringDistance() )
        edgeDistances.push_back( static_cast<double>( d ) );

      for( auto&& child : current->_children )
      {
        auto d = Metric()( p, child->_point );
        if( d <= child->coveringDistance() )
        {
          rootDistances.push_back(
            Metric()( _root->_point, child->_point )
          );

          // Continue the recursion in the next level, using the current
          // node as a new root.
          current = child.get();
          break;
        }
      }

      if( current == previous )
        break;
      else
        previous = current;
    }

    std::cerr << "Point = " << p << "\n";

    std::cerr << "  root distances:\n";
    for( auto&& d : rootDistances )
      std::cerr << d << " ";
    std::cerr << "\n";

    std::cerr << "  edge distances:\n";
    for( auto&& d : edgeDistances )
      std::cerr << d << " ";
    std::cerr << "\n";

    return true;
  }

private:

  /**
    Calculates covering distance of a given level. This is
    a convenience function that ensures that this check is
    always in sync with the covering constant.
  */

  double coveringDistance( long level ) const noexcept
  {
    return std::pow( coveringConstant, static_cast<double>( level ) );
  }

  /** Root pointer of the tree */
  std::unique_ptr<Node> _root;
};

} // namespace geometry

} // namespace aleph

#endif
