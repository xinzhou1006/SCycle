#ifndef MAXWELLVISCOELASTIC_H_INCLUDED
#define MAXWELLVISCOELASTIC_H_INCLUDED

#include <petscksp.h>
#include <string>
#include <cmath>
#include <vector>
#include "integratorContext.hpp"
#include "genFuncs.hpp"
#include "domain.hpp"
#include "linearElastic.hpp"
#include "heatEquation.hpp"

// models a 1D Maxwell slider assuming symmetric boundary condition
// on the fault.
class SymmMaxwellViscoelastic: public SymmLinearElastic
{
  protected:
    const char       *_file;
    std::string       _delim; // format is: var delim value (without the white space)
    std::string       _inputDir; // directory to load viscosity from

    std::string  _viscDistribution; // options: mms, layered, effectiveVisc
    std::vector<double> _viscVals,_viscDepths;
    std::vector<double> _AVals,_ADepths,_nVals,_nDepths,_BVals,_BDepths,_sigmadevVals,_sigmadevDepths;
    PetscScalar _strainRate;
    Vec         _visc;

    Vec         _gxyP,_dgxyP; // viscoelastic strain and strain rate
    Vec         _gxzP,_dgxzP; // viscoelastic strain and strain rate

    PetscViewer _gxyPV,_dgxyPV;
    PetscViewer _gxzPV,_dgxzPV;

    // additional body fields for visualization
    Vec         _gTxyP,_gTxzP; // total strain
    PetscViewer _gTxyPV,_gTxzPV;
    Vec         _stressxzP;
    PetscViewer _stressxyPV,_stressxzPV;

    //~ std::string _thermalCoupling;
    //~ HeatEquation _he;

    PetscErrorCode setViscStrainSourceTerms(Vec& source,const_it_vec varBegin,const_it_vec varEnd);
    PetscErrorCode setViscStrainRates(const PetscScalar time,const_it_vec varBegin,const_it_vec varEnd,
                                          it_vec dvarBegin,it_vec dvarEnd);
    PetscErrorCode setStresses(const PetscScalar time,const_it_vec varBegin,const_it_vec varEnd);
    PetscErrorCode setViscousStrainRateSAT(Vec &u, Vec &gL, Vec &gR, Vec &out);
    PetscErrorCode debug(const PetscReal time,const PetscInt stepCount,
                           const_it_vec varBegin,const_it_vec varEnd,
                           const_it_vec dvarBegin,const_it_vec dvarEnd,const char *stage);

    PetscErrorCode setMMSInitialConditions();
    PetscErrorCode addMMSViscStrainsAndRates(const PetscScalar time,const_it_vec varBegin,const_it_vec varEnd,
      it_vec dvarBegin,it_vec dvarEnd);
    PetscErrorCode setMMSBoundaryConditions(const double time);


  public:
    SymmMaxwellViscoelastic(Domain&D);
    ~SymmMaxwellViscoelastic();

    PetscErrorCode resetInitialConds();

    PetscErrorCode integrate(); // don't need now that LinearElastic defines this
    PetscErrorCode d_dt(const PetscScalar time,const_it_vec varBegin,const_it_vec varEnd,
                     it_vec dvarBegin,it_vec dvarEnd);
    PetscErrorCode d_dt_mms(const PetscScalar time,const_it_vec varBegin,const_it_vec varEnd,
                            it_vec dvarBegin,it_vec dvarEnd);
    PetscErrorCode d_dt_eqCycle(const PetscScalar time,const_it_vec varBegin,const_it_vec varEnd,
                                it_vec dvarBegin,it_vec dvarEnd);
    PetscErrorCode d_dt_kinetic(const PetscScalar time,const_it_vec varBegin,const_it_vec varEnd,
                                it_vec dvarBegin,it_vec dvarEnd);
    PetscErrorCode timeMonitor(const PetscReal time,const PetscInt stepCount,
                             const_it_vec varBegin,const_it_vec varEnd,
                             const_it_vec dvarBegin,const_it_vec dvarEnd);

    PetscErrorCode writeStep1D();
    PetscErrorCode writeStep2D();
    PetscErrorCode view();

    PetscErrorCode measureMMSError();

    // load settings from input file
    PetscErrorCode loadSettings(const char *file);
    PetscErrorCode setFields(Domain& D);
    PetscErrorCode loadFieldsFromFiles();
    PetscErrorCode setVecFromVectors(Vec& vec, vector<double>& vals,vector<double>& depths);

    // check input from file
    PetscErrorCode checkInput();
};

#endif
