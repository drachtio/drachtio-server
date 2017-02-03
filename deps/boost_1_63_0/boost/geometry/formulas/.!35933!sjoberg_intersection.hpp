// Boost.Geometry

// Copyright (c) 2016 Oracle and/or its affiliates.

// Contributed and/or modified by Adam Wulkiewicz, on behalf of Oracle

// Use, modification and distribution is subject to the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_GEOMETRY_FORMULAS_SJOBERG_INTERSECTION_HPP
#define BOOST_GEOMETRY_FORMULAS_SJOBERG_INTERSECTION_HPP


#include <boost/math/constants/constants.hpp>

#include <boost/geometry/core/radius.hpp>
#include <boost/geometry/core/srs.hpp>

#include <boost/geometry/util/condition.hpp>
#include <boost/geometry/util/math.hpp>

#include <boost/geometry/algorithms/detail/flattening.hpp>


namespace boost { namespace geometry { namespace formula
{

/*!
\brief The intersection of two geodesics as proposed by Sjoberg.
\author See
    - [Sjoberg02] Lars E. Sjoberg, Intersections on the sphere and ellipsoid, 2002
      http://link.springer.com/article/10.1007/s00190-001-0230-9
    - [Sjoberg07] Lars E. Sjoberg, Geodetic intersection on the ellipsoid, 2007
      http://link.springer.com/article/10.1007/s00190-007-0204-7
*/
template
<
    typename CT,
    template <typename, bool, bool, bool, bool, bool> class Inverse,
    unsigned int Order = 4
>
class sjoberg_intersection
{
    typedef Inverse<CT, false, true, false, false, false> inverse_type;
    typedef typename inverse_type::result_type inverse_result;

public:
    template <typename T1, typename T2, typename Spheroid>
    static inline bool apply(T1 const& lona1, T1 const& lata1,
                             T1 const& lona2, T1 const& lata2,
                             T2 const& lonb1, T2 const& latb1,
                             T2 const& lonb2, T2 const& latb2,
                             CT & lon, CT & lat,
                             Spheroid const& spheroid)
    {
        CT const lon_a1 = lona1;
        CT const lat_a1 = lata1;
        CT const lon_a2 = lona2;
        CT const lat_a2 = lata2;
        CT const lon_b1 = lonb1;
        CT const lat_b1 = latb1;
        CT const lon_b2 = lonb2;
        CT const lat_b2 = latb2;

        CT const alpha1 = inverse_type::apply(lon_a1, lat_a1, lon_a2, lat_a2, spheroid).azimuth;
        CT const alpha2 = inverse_type::apply(lon_b1, lat_b1, lon_b2, lat_b2, spheroid).azimuth;

        return apply(lon_a1, lat_a1, alpha1, lon_b1, lat_b1, alpha2, lon, lat, spheroid);
    }
    
    template <typename Spheroid>
    static inline bool apply(CT const& lon1, CT const& lat1, CT const& alpha1,
                             CT const& lon2, CT const& lat2, CT const& alpha2,
                             CT & lon, CT & lat,
                             Spheroid const& spheroid)
    {
        // coordinates in radians

        // TODO - handle special cases like degenerated segments, equator, poles, etc.

        CT const c0 = 0;
        CT const c1 = 1;
        CT const c2 = 2;

        CT const pi = math::pi<CT>();
        CT const pi_half = pi / c2;
        CT const f = detail::flattening<CT>(spheroid);
        CT const one_minus_f = c1 - f;
        CT const e_sqr = f * (c2 - f);
        
        CT const sin_alpha1 = sin(alpha1);
        CT const sin_alpha2 = sin(alpha2);

        CT const tan_beta1 = one_minus_f * tan(lat1);
        CT const tan_beta2 = one_minus_f * tan(lat2);
        CT const beta1 = atan(tan_beta1);
        CT const beta2 = atan(tan_beta2);
        CT const cos_beta1 = cos(beta1);
        CT const cos_beta2 = cos(beta2);
        CT const sin_beta1 = sin(beta1);
        CT const sin_beta2 = sin(beta2);

        // Clairaut constants (lower-case in the paper)
        int const sign_C1 = math::abs(alpha1) <= pi_half ? 1 : -1;
        int const sign_C2 = math::abs(alpha2) <= pi_half ? 1 : -1;
        // Cj = 1 if on equator
        CT const C1 = sign_C1 * cos_beta1 * sin_alpha1;
        CT const C2 = sign_C2 * cos_beta2 * sin_alpha2;

        CT const sqrt_1_C1_sqr = math::sqrt(c1 - math::sqr(C1));
        CT const sqrt_1_C2_sqr = math::sqrt(c1 - math::sqr(C2));

        // handle special case: segments on the equator
        bool const on_equator1 = math::equals(sqrt_1_C1_sqr, c0);
        bool const on_equator2 = math::equals(sqrt_1_C2_sqr, c0);
        if (on_equator1 && on_equator2)
        {
            return false;
        }
        else if (on_equator1)
        {
            CT const dL2 = d_lambda_e_sqr(sin_beta2, c0, C2, sqrt_1_C2_sqr, e_sqr);
            CT const asin_t2_t02 = asin(C2 * tan_beta2 / sqrt_1_C2_sqr);
            lat = c0;
            lon = lon2 - asin_t2_t02 + dL2;
            return true;
        }
        else if (on_equator2)
        {
            CT const dL1 = d_lambda_e_sqr(sin_beta1, c0, C1, sqrt_1_C1_sqr, e_sqr);
            CT const asin_t1_t01 = asin(C1 * tan_beta1 / sqrt_1_C1_sqr);
            lat = c0;
            lon = lon1 - asin_t1_t01 + dL1;
            return true;
        }

        CT const t01 = sqrt_1_C1_sqr / C1;
        CT const t02 = sqrt_1_C2_sqr / C2;

        CT const asin_t1_t01 = asin(tan_beta1 / t01);
        CT const asin_t2_t02 = asin(tan_beta2 / t02);
        CT const t01_t02 = t01 * t02;
        CT const t01_t02_2 = c2 * t01_t02;
        CT const sqr_t01_sqr_t02 = math::sqr(t01) + math::sqr(t02);

        CT t = tan_beta1;
        int t_id = 0;

        // find the initial t using simplified spherical solution
        // though not entirely since the reduced latitudes and azimuths are spheroidal
        // [Sjoberg07]
        CT const k_base = lon1 - lon2 + asin_t2_t02 - asin_t1_t01;
        
        {
            CT const K = sin(k_base);
            CT const d1 = sqr_t01_sqr_t02;
            //CT const d2 = t01_t02_2 * math::sqrt(c1 - math::sqr(K));
            CT const d2 = t01_t02_2 * cos(k_base);
            CT const D1 = math::sqrt(d1 - d2);
            CT const D2 = math::sqrt(d1 + d2);
            CT const K_t01_t02 = K * t01_t02;

            CT const T1 = K_t01_t02 / D1;
            CT const T2 = K_t01_t02 / D2;
            CT asin_T1_t01 = 0;
            CT asin_T1_t02 = 0;
            CT asin_T2_t01 = 0;
            CT asin_T2_t02 = 0;

            // test 4 possible results
            CT l1 = 0, l2 = 0, dl = 0;
            bool found = check_t<0>( T1,
                                    lon1,  asin_T1_t01 = asin(T1 / t01), asin_t1_t01,
                                    lon2,  asin_T1_t02 = asin(T1 / t02), asin_t2_t02,
                                    t, l1, l2, dl, t_id)
                      || check_t<1>(-T1,
                                    lon1, -asin_T1_t01                 , asin_t1_t01,
                                    lon2, -asin_T1_t02                 , asin_t2_t02,
                                    t, l1, l2, dl, t_id)
                      || check_t<2>( T2,
                                    lon1,  asin_T2_t01 = asin(T2 / t01), asin_t1_t01,
                                    lon2,  asin_T2_t02 = asin(T2 / t02), asin_t2_t02,
                                    t, l1, l2, dl, t_id)
                      || check_t<3>(-T2,
                                    lon1, -asin_T2_t01                 , asin_t1_t01,
                                    lon2, -asin_T2_t02                 , asin_t2_t02,
                                    t, l1, l2, dl, t_id);

            boost::ignore_unused(found);
        }
        
        // [Sjoberg07]
        //int const d2_sign = t_id < 2 ? -1 : 1;
        int const t_sign = (t_id % 2) ? -1 : 1;
        // [Sjoberg02]
        CT const C1_sqr = math::sqr(C1);
        CT const C2_sqr = math::sqr(C2);
        
        CT beta = atan(t);
        CT dL1 = 0, dL2 = 0;
        CT asin_t_t01 = 0;
        CT asin_t_t02 = 0;

        for (int i = 0; i < 10; ++i)
        {
            CT const sin_beta = sin(beta);

            // integrals approximation
            dL1 = d_lambda_e_sqr(sin_beta1, sin_beta, C1, sqrt_1_C1_sqr, e_sqr);
            dL2 = d_lambda_e_sqr(sin_beta2, sin_beta, C2, sqrt_1_C2_sqr, e_sqr);

            // [Sjoberg07]
            /*CT const k = k_base + dL1 - dL2;
            CT const K = sin(k);
            CT const d1 = sqr_t01_sqr_t02;
            //CT const d2 = t01_t02_2 * math::sqrt(c1 - math::sqr(K));
            CT const d2 = t01_t02_2 * cos(k);
            CT const D = math::sqrt(d1 + d2_sign * d2);
            CT const t_new = t_sign * K * t01_t02 / D;
            CT const dt = math::abs(t_new - t);
            t = t_new;
            CT const new_beta = atan(t);
            CT const dbeta = math::abs(new_beta - beta);
            beta = new_beta;*/

            // [Sjoberg02] - it converges faster
