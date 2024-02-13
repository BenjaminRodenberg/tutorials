// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
// vi: set et ts=4 sw=4 sts=4:
/*****************************************************************************
 *   This program is free software: you can redistribute it and/or modify    *
 *   it under the terms of the GNU General Public License as published by    *
 *   the Free Software Foundation, either version 3 of the License, or       *
 *   (at your option) any later version.                                     *
 *                                                                           *
 *   This program is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the            *
 *   GNU General Public License for more details.                            *
 *                                                                           *
 *   You should have received a copy of the GNU General Public License       *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.   *
 *****************************************************************************/

/*
 * \brief OnePModel in combination with the NI model for a conduction problem.
 * The simulation domain is a tube with an elevated temperature on the left hand
 * side.
 */

#ifndef DUMUX_1PNI_CONDUCTION_PROBLEM_HH
#define DUMUX_1PNI_CONDUCTION_PROBLEM_HH

#include <cmath>

#include <dumux/common/boundarytypes.hh>
#include <dumux/common/parameters.hh>
#include <dumux/common/properties.hh>
#include <dumux/porousmediumflow/problem.hh>

#include <dumux-precice/couplingadapter.hh>

namespace Dumux {

template <class TypeTag>
class OnePNIConductionProblem : public PorousMediumFlowProblem<TypeTag> {
  using ParentType = PorousMediumFlowProblem<TypeTag>;
  using GridView =
      typename GetPropType<TypeTag, Properties::GridGeometry>::GridView;
  using Scalar = GetPropType<TypeTag, Properties::Scalar>;
  using PrimaryVariables = GetPropType<TypeTag, Properties::PrimaryVariables>;
  using FluidSystem = GetPropType<TypeTag, Properties::FluidSystem>;
  using BoundaryTypes = Dumux::BoundaryTypes<
      GetPropType<TypeTag, Properties::ModelTraits>::numEq()>;
  using VolumeVariables = GetPropType<TypeTag, Properties::VolumeVariables>;
  using SolutionVector = GetPropType<TypeTag, Properties::SolutionVector>;
  using Element = typename GridView::template Codim<0>::Entity;
  using GlobalPosition = typename Element::Geometry::GlobalCoordinate;
  using GridGeometry = GetPropType<TypeTag, Properties::GridGeometry>;

  enum { dimWorld = GridView::dimensionworld };
  using DimWorldMatrix = Dune::FieldMatrix<Scalar, dimWorld, dimWorld>;

  // copy indices of primary variables for convenience
  using Indices =
      typename GetPropType<TypeTag, Properties::ModelTraits>::Indices;
  enum {
    pressureIdx = Indices::pressureIdx,
    temperatureIdx = Indices::temperatureIdx
  };

public:
  OnePNIConductionProblem(std::shared_ptr<const GridGeometry> gridGeometry)
      : ParentType(gridGeometry),
        couplingParticipant_(Dumux::Precice::CouplingAdapter::getInstance()) {
    // initialize fluid system
    FluidSystem::init();

    name_ = getParam<std::string>("Problem.Name");

    porosity_.resize(gridGeometry->numDofs());
    k00_.resize(gridGeometry->numDofs());
    k01_.resize(gridGeometry->numDofs());
    k10_.resize(gridGeometry->numDofs());
    k11_.resize(gridGeometry->numDofs());
  }

  /*!
   * \brief Return the temperature index.
   */
  int returnTemperatureIdx() { return temperatureIdx; }

  /*!
   * \brief Return the problem name.
   * This is used as a prefix for files generated by the simulation.
   */
  const std::string &name() const { return name_; }

  /*!
   * \brief Specifies which kind of boundary condition should be
   *        used for which equation on a given boundary segment.
   *
   * \param globalPos The position for which the bc type should be evaluated
   */
  BoundaryTypes boundaryTypesAtPos(const GlobalPosition &globalPos) const {
    BoundaryTypes bcTypes;

    // meshwidth calculation (assumes regular rectangular grid)
    auto cells = getParam<std::array<int, 2>>("Grid.Cells");
    double meshWidthX = (this->gridGeometry().bBoxMax()[0] -
                         this->gridGeometry().bBoxMin()[0]) /
                        cells[0];
    double meshWidthY = (this->gridGeometry().bBoxMax()[1] -
                         this->gridGeometry().bBoxMin()[1]) /
                        cells[1];

    if (globalPos[1] < this->gridGeometry().bBoxMin()[1] + eps_ &&
        getParam<std::string>("BoundaryConditions.BcTypeBottom") == "dirichlet")
      bcTypes.setAllDirichlet();
    else if (globalPos[1] > this->gridGeometry().bBoxMax()[1] - eps_ &&
             getParam<std::string>("BoundaryConditions.BcTypeTop") ==
                 "dirichlet")
      bcTypes.setAllDirichlet();
    else if (globalPos[0] < this->gridGeometry().bBoxMin()[0] + eps_ &&
             getParam<std::string>("BoundaryConditions.BcTypeLeft") ==
                 "dirichlet")
      bcTypes.setAllDirichlet();
    else if (globalPos[0] > this->gridGeometry().bBoxMax()[0] - eps_ &&
             getParam<std::string>("BoundaryConditions.BcTypeRight") ==
                 "dirichlet")
      bcTypes.setAllDirichlet();
    // heat source bottom left corner
    else if ((globalPos[1] < this->gridGeometry().bBoxMin()[1] + eps_ &&
              globalPos[0] <
                  this->gridGeometry().bBoxMin()[0] + meshWidthX + eps_) &&
             getParam<bool>("BoundaryConditions.UseHeatSourceBottomLeft"))
      bcTypes.setAllDirichlet();
    else if ((globalPos[0] < this->gridGeometry().bBoxMin()[0] + eps_ &&
              globalPos[1] <
                  this->gridGeometry().bBoxMin()[1] + meshWidthY + eps_) &&
             getParam<bool>("BoundaryConditions.UseHeatSourceBottomLeft"))
      bcTypes.setAllDirichlet();
    else
      bcTypes.setAllNeumann(); // default is adiabatic
    return bcTypes;
  }

  /*!
   * \brief Evaluates the boundary conditions for a Dirichlet boundary segment.
   * This function is only called in dirichlet boundary cells.
   * \param globalPos The position for which the bc type should be evaluated
   */
  PrimaryVariables dirichletAtPos(const GlobalPosition &globalPos) const {
    PrimaryVariables priVars(initial_());

    // meshwidth calculation (assumes regular rectangular grids)
    auto cells = getParam<std::array<int, 2>>("Grid.Cells");
    double meshWidthX = (this->gridGeometry().bBoxMax()[0] -
                         this->gridGeometry().bBoxMin()[0]) /
                        cells[0];
    double meshWidthY = (this->gridGeometry().bBoxMax()[1] -
                         this->gridGeometry().bBoxMin()[1]) /
                        cells[1];

    if (globalPos[0] < this->gridGeometry().bBoxMin()[0] + eps_)
      priVars[temperatureIdx] = getParam<Scalar>("BoundaryConditions.BcLeft");
    else if (globalPos[0] > this->gridGeometry().bBoxMax()[0] - eps_)
      priVars[temperatureIdx] = getParam<Scalar>("BoundaryConditions.BcRight");

    if (globalPos[1] < this->gridGeometry().bBoxMin()[1] + eps_)
      priVars[temperatureIdx] = getParam<Scalar>("BoundaryConditions.BcBottom");
    else if (globalPos[1] > this->gridGeometry().bBoxMax()[1] - eps_)
      priVars[temperatureIdx] = getParam<Scalar>("BoundaryConditions.BcTop");

    // heat source bottom left corner
    if (getParam<bool>("BoundaryConditions.UseHeatSourceBottomLeft")) {
      if ((globalPos[1] < this->gridGeometry().bBoxMin()[1] + eps_ &&
           globalPos[0] <
               this->gridGeometry().bBoxMin()[0] + meshWidthX + eps_) ||
          (globalPos[0] < this->gridGeometry().bBoxMin()[0] + eps_ &&
           globalPos[1] <
               this->gridGeometry().bBoxMin()[1] + meshWidthY + eps_))
        priVars[temperatureIdx] =
            getParam<Scalar>("BoundaryConditions.HeatSourceBottomLeft");
    }
    return priVars;
  }

  /*!
   * \brief Evaluates the initial value for a control volume.
   *
   * \param globalPos The position for which the initial condition should be
   * evaluated
   */
  PrimaryVariables initialAtPos(const GlobalPosition &globalPos) const {
    return initial_();
  }

  // to make available to vtkOutput
  const std::vector<Scalar> &getPorosity() { return porosity_; }
  const std::vector<Scalar> &getK00() { return k00_; }
  const std::vector<Scalar> &getK01() { return k01_; }
  const std::vector<Scalar> &getK10() { return k10_; }
  const std::vector<Scalar> &getK11() { return k11_; }

  /*!
   * \brief Function to update the conductivities, porosities of additional vtk
   * output
   *
   * \param globalPos The position for which the initial condition should be
   * evaluated
   */
  template <class SolutionVector>
  void updateVtkOutput(const SolutionVector &curSol) {
    for (const auto &element : elements(this->gridGeometry().gridView())) {
      const auto elemSol =
          elementSolution(element, curSol, this->gridGeometry());
      auto fvGeometry = localView(this->gridGeometry());
      fvGeometry.bindElement(element);
      for (const auto &scv : scvs(fvGeometry)) {
        VolumeVariables volVars;
        volVars.update(elemSol, *this, element, scv);
        const auto elementIdx = scv.elementIndex();
        porosity_[elementIdx] = volVars.porosity();
        k00_[elementIdx] = volVars.effectiveThermalConductivity()[0][0];
        k01_[elementIdx] = volVars.effectiveThermalConductivity()[0][1];
        k10_[elementIdx] = volVars.effectiveThermalConductivity()[1][0];
        k11_[elementIdx] = volVars.effectiveThermalConductivity()[1][1];
      }
    }
  }

private:
  Dumux::Precice::CouplingAdapter &couplingParticipant_;

  // the internal method for the initial condition
  PrimaryVariables initial_() const {
    PrimaryVariables priVars(0.0);
    priVars[pressureIdx] = getParam<Scalar>("InitialConditions.Pressure");
    priVars[temperatureIdx] = getParam<Scalar>("InitialConditions.Temperature");
    return priVars;
  }
  static constexpr Scalar eps_ = 1e-6;
  std::string name_;
  std::vector<Scalar> porosity_;
  std::vector<Scalar> k00_;
  std::vector<Scalar> k01_;
  std::vector<Scalar> k10_;
  std::vector<Scalar> k11_;
};

} // namespace Dumux

#endif
