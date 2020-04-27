/**
 * \file
 * \copyright
 * Copyright (c) 2012-2020, OpenGeoSys Community (http://www.opengeosys.org)
 *            Distributed under a Modified BSD License.
 *              See accompanying file LICENSE.txt or
 *              http://www.opengeosys.org/project/license
 *
 * Created on March 20, 2020, 9:59 AM
 */

#include "CapillaryPressureVanGenuchten.h"

#include <algorithm>
#include <cmath>

#include "MaterialLib/MPL/Medium.h"

namespace MaterialPropertyLib
{
CapillaryPressureVanGenuchten::CapillaryPressureVanGenuchten(
    double const residual_liquid_saturation,
    double const residual_gas_saturation,
    double const exponent,
    double const p_b,
    double const maximum_capillary_pressure)
    : _S_L_res(residual_liquid_saturation),
      _S_L_max(1. - residual_gas_saturation),
      _m(exponent),
      _p_b(p_b),
      _p_cap_max(maximum_capillary_pressure)
{
    if (!(_m > 0 && _m < 1))
    {
        OGS_FATAL(
            "The exponent value m = {:g} of van Genuchten capillary pressure "
            "model, is out of its range of (0, 1)",
            _m);
    }
}

PropertyDataType CapillaryPressureVanGenuchten::value(
    VariableArray const& variable_array,
    ParameterLib::SpatialPosition const& /*pos*/, double const /*t*/,
    double const /*dt*/) const
{
    double const S_L = std::get<double>(
        variable_array[static_cast<int>(Variable::liquid_saturation)]);

    if (S_L <= _S_L_res)
    {
        return _p_cap_max;
    }

    if (S_L >= _S_L_max)
    {
        return 0;
    }

    double const S_eff = (S_L - _S_L_res) / (_S_L_max - _S_L_res);
    assert(0 <= S_eff && S_eff <= 1);

    double const p_cap =
        _p_b * std::pow(std::pow(S_eff, -1.0 / _m) - 1.0, 1.0 - _m);
    assert(p_cap > 0);
    return std::min(p_cap, _p_cap_max);
}

PropertyDataType CapillaryPressureVanGenuchten::dValue(
    VariableArray const& variable_array, Variable const primary_variable,
    ParameterLib::SpatialPosition const& /*pos*/, double const /*t*/,
    double const /*dt*/) const
{
    (void)primary_variable;
    assert((primary_variable == Variable::liquid_saturation) &&
           "CapillaryPressureVanGenuchten::dValue is implemented for "
           "derivatives with respect to liquid saturation only.");

    double const S_L = std::get<double>(
        variable_array[static_cast<int>(Variable::liquid_saturation)]);

    if (S_L <= _S_L_res)
    {
        return 0;
    }

    if (S_L >= _S_L_max)
    {
        return 0;
    }

    double const S_eff = (S_L - _S_L_res) / (_S_L_max - _S_L_res);

    assert(0 < S_eff && S_eff < 1.0);

    double const val1 = std::pow(S_eff, -1.0 / _m);
    double const p_cap = _p_b * std::pow(val1 - 1.0, 1.0 - _m);
    if (p_cap >= _p_cap_max)
    {
        return 0;
    }

    double const val2 = std::pow(val1 - 1.0, -_m);
    return _p_b * (_m - 1.0) * val1 * val2 / (_m * (S_L - _S_L_res));
}
}  // namespace MaterialPropertyLib
