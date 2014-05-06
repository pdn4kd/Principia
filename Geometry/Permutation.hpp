#pragma once

#include "Geometry/Grassmann.hpp"
#include "Geometry/LinearMap.hpp"
#include "Geometry/R3Element.hpp"
#include "Geometry/Sign.hpp"

namespace principia {
namespace geometry {

// A permutation of the coordinates. Obviously not coordinate-free, but
// practical.  There are no precision losses when composing or applying
// permutations.
template<typename Scalar, typename FromFrame, typename ToFrame>
class Permutation : public LinearMap<Scalar, FromFrame, ToFrame> {
  // These constants are used in the definition of type CoordinatePermutation.
  // The sign bit gives the sign of the permutation.
  static const int even = 0, odd = 0x80000000;
  // Three two-bit fields which indicate how each coordinate get mapped by the
  // permutation.
  static const int x = 0, y = 1, z = 2;
  // A three bit field used when using this enum to index arrays.
  static const int index = 6;

 public:
  enum CoordinatePermutation {
    XYZ = even + (x << x * 2) + (y << y * 2) + (z << z * 2) + (0 << index),
    YZX = even + (y << x * 2) + (z << y * 2) + (x << z * 2) + (1 << index),
    ZXY = even + (z << x * 2) + (x << y * 2) + (y << z * 2) + (2 << index),
    XZY = odd  + (x << x * 2) + (z << y * 2) + (y << z * 2) + (3 << index),
    ZYX = odd  + (z << x * 2) + (y << y * 2) + (x << z * 2) + (4 << index),
    YXZ = odd  + (y << x * 2) + (x << y * 2) + (z << z * 2) + (5 << index)
  };

  Permutation(CoordinatePermutation const coordinate_permutation);
  virtual ~Permutation() = default;

  Vector<Scalar, ToFrame> operator()(
      Vector<Scalar, FromFrame> const& vector) const override;
  Bivector<Scalar, ToFrame> operator()(
      Bivector<Scalar, FromFrame> const& bivector) const override;
  Trivector<Scalar, ToFrame> operator()(
      Trivector<Scalar, FromFrame> const& trivector) const override;

  Sign Determinant() const;

  // TODO(phl): Uncomment once orthogonal transformations are done.
  // OrthogonalTransformation<Scalar, FromFrame, ToFrame> Forget() const;

  static Permutation Identity();

 private:
  CoordinatePermutation const coordinate_permutation_;

  template<typename Scalar, typename FromFrame, typename ToFrame>
  friend R3Element<Scalar> operator*(
      Permutation<Scalar, FromFrame, ToFrame> const& left,
      R3Element<Scalar> const& right);
};

template<typename Scalar, typename FromFrame, typename ToFrame>
R3Element<Scalar> operator*(
    Permutation<Scalar, FromFrame, ToFrame> const& left,
    R3Element<Scalar> const& right);

}  // namespace geometry
}  // namespace principia

#include "Geometry/Permutation-body.hpp"
