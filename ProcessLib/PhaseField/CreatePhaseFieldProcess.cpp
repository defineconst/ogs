/**
 * \file
 * \copyright
 * Copyright (c) 2012-2020, OpenGeoSys Community (http://www.opengeosys.org)
 *            Distributed under a Modified BSD License.
 *              See accompanying file LICENSE.txt or
 *              http://www.opengeosys.org/project/license
 *
 */

#include "CreatePhaseFieldProcess.h"

#include <cassert>

#include "MaterialLib/SolidModels/CreateConstitutiveRelation.h"
#include "MaterialLib/SolidModels/MechanicsBase.h"
#include "ParameterLib/Utils.h"
#include "ProcessLib/Output/CreateSecondaryVariables.h"
#include "ProcessLib/Utils/ProcessUtils.h"

#include "PhaseFieldProcess.h"
#include "PhaseFieldProcessData.h"

namespace ProcessLib
{
namespace PhaseField
{
template <int DisplacementDim>
std::unique_ptr<Process> createPhaseFieldProcess(
    std::string name,
    MeshLib::Mesh& mesh,
    std::unique_ptr<ProcessLib::AbstractJacobianAssembler>&& jacobian_assembler,
    std::vector<ProcessVariable> const& variables,
    std::vector<std::unique_ptr<ParameterLib::ParameterBase>> const& parameters,
    boost::optional<ParameterLib::CoordinateSystem> const&
        local_coordinate_system,
    unsigned const integration_order,
    BaseLib::ConfigTree const& config)
{
    //! \ogs_file_param{prj__processes__process__type}
    config.checkConfigParameter("type", "PHASE_FIELD");
    DBUG("Create PhaseFieldProcess.");

    auto const staggered_scheme =
        //! \ogs_file_param{prj__processes__process__PHASE_FIELD__coupling_scheme}
        config.getConfigParameterOptional<std::string>("coupling_scheme");
    const bool use_monolithic_scheme =
        !(staggered_scheme && (*staggered_scheme == "staggered"));

    // Process variable.

    //! \ogs_file_param{prj__processes__process__PHASE_FIELD__process_variables}
    auto const pv_config = config.getConfigSubtree("process_variables");

    ProcessVariable* variable_ph;
    ProcessVariable* variable_u;
    std::vector<std::vector<std::reference_wrapper<ProcessVariable>>>
        process_variables;
    if (use_monolithic_scheme)  // monolithic scheme.
    {
        OGS_FATAL("Monolithic implementation is not available.");
    }
    else  // staggered scheme.
    {
        using namespace std::string_literals;
        for (
            auto const& variable_name :
            {//! \ogs_file_param_special{prj__processes__process__PHASE_FIELD__process_variables__displacement}
             "displacement"s,
             //! \ogs_file_param_special{prj__processes__process__PHASE_FIELD__process_variables__phasefield}
             "phasefield"s})
        {
            auto per_process_variables =
                findProcessVariables(variables, pv_config, {variable_name});
            process_variables.push_back(std::move(per_process_variables));
        }
        variable_u = &process_variables[0][0].get();
        variable_ph = &process_variables[1][0].get();
    }

    DBUG("Associate displacement with process variable '{:s}'.",
         variable_u->getName());

    if (variable_u->getNumberOfComponents() != DisplacementDim)
    {
        OGS_FATAL(
            "Number of components of the process variable '{:s}' is different "
            "from the displacement dimension: got {:d}, expected {:d}",
            variable_u->getName(),
            variable_u->getNumberOfComponents(),
            DisplacementDim);
    }

    DBUG("Associate phase field with process variable '{:s}'.",
         variable_ph->getName());
    if (variable_ph->getNumberOfComponents() != 1)
    {
        OGS_FATAL(
            "Phasefield process variable '{:s}' is not a scalar variable but "
            "has "
            "{:d} components.",
            variable_ph->getName(),
            variable_ph->getNumberOfComponents());
    }

    auto solid_constitutive_relations =
        MaterialLib::Solids::createConstitutiveRelations<DisplacementDim>(
            parameters, local_coordinate_system, config);

    auto const phasefield_parameters_config =
        //! \ogs_file_param{prj__processes__process__PHASE_FIELD__phasefield_parameters}
        config.getConfigSubtree("phasefield_parameters");

    // Residual stiffness
    auto& residual_stiffness = ParameterLib::findParameter<double>(
        phasefield_parameters_config,
        //! \ogs_file_param_special{prj__processes__process__PHASE_FIELD__phasefield_parameters__residual_stiffness}
        "residual_stiffness", parameters, 1);
    DBUG("Use '{:s}' as residual stiffness.", residual_stiffness.name);

    // Crack resistance
    auto& crack_resistance = ParameterLib::findParameter<double>(
        phasefield_parameters_config,
        //! \ogs_file_param_special{prj__processes__process__PHASE_FIELD__phasefield_parameters__crack_resistance}
        "crack_resistance", parameters, 1);
    DBUG("Use '{:s}' as crack resistance.", crack_resistance.name);

    // Crack length scale
    auto& crack_length_scale = ParameterLib::findParameter<double>(
        phasefield_parameters_config,
        //! \ogs_file_param_special{prj__processes__process__PHASE_FIELD__phasefield_parameters__crack_length_scale}
        "crack_length_scale", parameters, 1);
    DBUG("Use '{:s}' as crack length scale.", crack_length_scale.name);

    // Kinetic coefficient
    auto& kinetic_coefficient = ParameterLib::findParameter<double>(
        phasefield_parameters_config,
        //! \ogs_file_param_special{prj__processes__process__PHASE_FIELD__phasefield_parameters__kinetic_coefficient}
        "kinetic_coefficient", parameters, 1);
    DBUG("Use '{:s}' as kinetic coefficient.", kinetic_coefficient.name);

    // Solid density
    auto& solid_density = ParameterLib::findParameter<double>(
        config,
        //! \ogs_file_param_special{prj__processes__process__PHASE_FIELD__solid_density}
        "solid_density", parameters, 1);
    DBUG("Use '{:s}' as solid density parameter.", solid_density.name);

    // History field
    auto& history_field = ParameterLib::findParameter<double>(
        phasefield_parameters_config,
        //! \ogs_file_param_special{prj__processes__process__PHASE_FIELD__phasefield_parameters__history_field}
        "history_field", parameters, 1);
    DBUG("Use '{:s}' as history field.", history_field.name);

    // Specific body force
    Eigen::Matrix<double, DisplacementDim, 1> specific_body_force;
    {
        std::vector<double> const b =
            //! \ogs_file_param{prj__processes__process__PHASE_FIELD__specific_body_force}
            config.getConfigParameter<std::vector<double>>(
                "specific_body_force");
        if (b.size() != DisplacementDim)
        {
            OGS_FATAL(
                "The size of the specific body force vector does not match the "
                "displacement dimension. Vector size is {:d}, displacement "
                "dimension is {:d}",
                b.size(), DisplacementDim);
        }

        std::copy_n(b.data(), b.size(), specific_body_force.data());
    }

    auto const crack_scheme =
        //! \ogs_file_param{prj__processes__process__PHASE_FIELD__hydro_crack_scheme}
        config.getConfigParameterOptional<std::string>("hydro_crack_scheme");
    if (crack_scheme &&
        ((*crack_scheme != "propagating") && (*crack_scheme != "static")))
    {
        OGS_FATAL(
            "hydro_crack_scheme must be 'propagating' or 'static' but "
            "'{:s}' was given",
            crack_scheme->c_str());
    }

    const bool propagating_crack =
        (crack_scheme && (*crack_scheme == "propagating"));
    const bool crack_pressure =
        (crack_scheme &&
         ((*crack_scheme == "propagating") || (*crack_scheme == "static")));

    PhaseFieldProcessData<DisplacementDim> process_data{
        materialIDs(mesh),   std::move(solid_constitutive_relations),
        residual_stiffness,  crack_resistance,
        crack_length_scale,  kinetic_coefficient,
        solid_density,       history_field,
        specific_body_force, propagating_crack,
        crack_pressure};

    SecondaryVariableCollection secondary_variables;

    ProcessLib::createSecondaryVariables(config, secondary_variables);

    return std::make_unique<PhaseFieldProcess<DisplacementDim>>(
        std::move(name), mesh, std::move(jacobian_assembler), parameters,
        integration_order, std::move(process_variables),
        std::move(process_data), std::move(secondary_variables),
        use_monolithic_scheme);
}

template std::unique_ptr<Process> createPhaseFieldProcess<2>(
    std::string name,
    MeshLib::Mesh& mesh,
    std::unique_ptr<ProcessLib::AbstractJacobianAssembler>&& jacobian_assembler,
    std::vector<ProcessVariable> const& variables,
    std::vector<std::unique_ptr<ParameterLib::ParameterBase>> const& parameters,
    boost::optional<ParameterLib::CoordinateSystem> const&
        local_coordinate_system,
    unsigned const integration_order,
    BaseLib::ConfigTree const& config);

template std::unique_ptr<Process> createPhaseFieldProcess<3>(
    std::string name,
    MeshLib::Mesh& mesh,
    std::unique_ptr<ProcessLib::AbstractJacobianAssembler>&& jacobian_assembler,
    std::vector<ProcessVariable> const& variables,
    std::vector<std::unique_ptr<ParameterLib::ParameterBase>> const& parameters,
    boost::optional<ParameterLib::CoordinateSystem> const&
        local_coordinate_system,
    unsigned const integration_order,
    BaseLib::ConfigTree const& config);

}  // namespace PhaseField
}  // namespace ProcessLib
