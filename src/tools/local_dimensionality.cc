/*
  This is a tool shipped by 'Aleph - A Library for Exploring Persistent
  Homology'.

  Given an input point cloud, it performs local dimensionality
  estimation (using different schmes) and stores the estimates
  along with the original point cloud.
*/

#include <aleph/containers/DimensionalityEstimators.hh>
#include <aleph/containers/MeanShift.hh>
#include <aleph/containers/PointCloud.hh>

#include <aleph/geometry/FLANN.hh>
#include <aleph/geometry/BruteForce.hh>

#include <aleph/geometry/distances/Euclidean.hh>

#include <aleph/math/KahanSummation.hh>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <getopt.h>

using DataType   = double;
using PointCloud = aleph::containers::PointCloud<DataType>;
using Distance   = aleph::geometry::distances::Euclidean<DataType>;

#ifdef ALEPH_WITH_FLANN
  using NearestNeighbours = aleph::geometry::FLANN<PointCloud, Distance>;
#else
  using NearestNeighbours = aleph::geometry::BruteForce<PointCloud, Distance>;
#endif

int main( int argc, char** argv )
{
  std::string method = "pca";
  unsigned k         = 8;
  unsigned K         = 0;
  unsigned n         = 1;
  bool smooth        = false;

  {
    static option commandLineOptions[] =
    {
      { "k"          , required_argument, nullptr, 'k' },
      { "K"          , required_argument, nullptr, 'K' },
      { "method"     , required_argument, nullptr, 'm' },
      { "n"          , required_argument, nullptr, 'n' },
      { "smooth"     , no_argument      , nullptr, 's' },
      { nullptr      , 0                , nullptr,  0  }
    };

    int option = 0;
    while( ( option = getopt_long( argc, argv, "k:K:m:n:s", commandLineOptions, nullptr ) ) != -1 )
    {
      switch( option )
      {
      case 'k':
        k = static_cast<unsigned>( std::stoull( optarg ) );
        break;
      case 'K':
        K = static_cast<unsigned>( std::stoull( optarg ) );
        break;
      case 'm':
        method = optarg;
        break;
      case 'n':
        n = static_cast<unsigned>( std::stoull( optarg ) );
        break;
      case 's':
        smooth = true;
        break;
      }
    }
  }

  if( ( argc - optind ) < 1 )
    return -1;

  std::string filename = argv[ optind++ ];

  std::cerr << "* Loading point cloud from '" << filename << "'...";

  PointCloud pc = aleph::containers::load<DataType>( filename );

  std::cerr << "finished\n"
            << "* Loaded point cloud with " << pc.size() << " points of dimension " << pc.dimension() << "\n";

  std::vector<double> dimensionalities;

  if( method == "pca" )
  {
    std::cerr << "* Estimating local dimensionality using PCA (k=" << k << ")...";

    auto result
      = aleph::containers::estimateLocalDimensionalityPCA<Distance, PointCloud, NearestNeighbours>( pc, k );

    // converts data in the vector so that we don't have to do it
    // manually
    dimensionalities.assign( result.begin(), result.end() );
  }
  else if( method == "nn" )
  {
    if( K == 0 )
    {
      std::cerr << "* Estimating local dimensionality using nearest neighbours (k=" << k << ")...";

      dimensionalities
        = aleph::containers::estimateLocalDimensionalityNearestNeighbours<Distance, PointCloud, NearestNeighbours>(
          pc, k );
    }
    else if( k <= K )
    {
      std::cerr << "* Estimating local dimensionality using nearest neighbours (k=" << k << ", K=" << K << ")...";

      dimensionalities
        = aleph::containers::estimateLocalDimensionalityNearestNeighbours<Distance, PointCloud, NearestNeighbours>(
        pc, k, K );
    }
  }
  else if( method == "mle" )
  {
    std::cerr << "* Estimating local dimensionality using nearest neighbours and MLE (k=" << k << ")...";

    if( k > K )
      throw std::runtime_error( "Missing maximum parameter for nearest neighbours" );

    dimensionalities
      = aleph::containers::estimateLocalDimensionalityNearestNeighboursMLE<Distance, PointCloud, NearestNeighbours>(
      pc, k, K
    );
  }
  else if( method == "mst" )
  {
    std::cerr << "* Estimating local dimensionality using MST...";

    dimensionalities
      = aleph::containers::estimateLocalDimensionalityNearestNeighbours<Distance>( pc );
  }

  std::cerr << "finished\n";

  // Output ------------------------------------------------------------

  if( smooth )
  {
    std::cerr << "* Performing smoothing operation with k=" << k << " and n=" << n << "...";

    std::vector<double> tmp;

    aleph::containers::meanShiftSmoothing<NearestNeighbours>(
      pc,
      dimensionalities.begin(), dimensionalities.end(),
      std::back_inserter( tmp ),
      k,
      n );

    dimensionalities.swap( tmp );

    std::cerr << "\n";
  }

  for( auto&& d : dimensionalities )
    std::cout << d << "\n";
}
