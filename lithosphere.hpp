#ifndef LITHOSPHERE_H_INCLUDED
#define LITHOSPHERE_H_INCLUDED

#include <petscksp.h>
#include <string>
#include <cmath>
#include "domain.hpp"
#include "sbpOps.hpp"
#include "fault.hpp"

class OdeSolver;

class Lithosphere
{

  protected:

    // domain properties
    const PetscInt       _order,_Ny,_Nz;
    const PetscScalar    _Ly,_Lz,_dy,_dz;

    // debugging folder tree
    //~std::string _debugDir;

    // output data
    std::string          _outputDir;

    const PetscScalar    _v0,_vp;

    // boundary conditions
    Vec                  _bcF,_bcS,_bcR,_bcD;
    PetscViewer          _bcFv,_bcSv,_bcRv,_bcDv,_rhsv;

    // off-fault material fields
    const PetscScalar    _rhoIn,_rhoOut,_muIn,_muOut;
    PetscScalar         *_muArr;
    Mat                  _mu;
    const PetscScalar    _depth,_width;
    Vec                  _rhs,_uhat,_sigma_xy,_bcRShift,_surfDisp;


    // linear system data
    std::string          _linSolver;
    KSP                  _ksp;
    PC                   _pc;
    PetscScalar          _kspTol;

    SbpOps               _sbp;
    Fault                _fault;

    // time stepping data
    std::string          _timeIntegrator;
    PetscInt             _strideLength; // stride
    PetscInt             _maxStepCount; // largest number of time steps
    PetscReal            _initTime,_currTime,_maxTime,_minDeltaT,_maxDeltaT;
    int                  _stepCount;
    PetscScalar          _atol;
    PetscScalar          _initDeltaT;

    // viewers
    PetscViewer          _timeViewer,_surfDispViewer,_uhatViewer;

    // runtime data
    double               _integrateTime,_writeTime,_linSolveTime,_factorTime;
    PetscInt             _linSolveCount;


    // disable default copy constructor and assignment operator
    Lithosphere(const Lithosphere &that);
    Lithosphere& operator=(const Lithosphere &rhs);

    PetscErrorCode computeShearStress();
    PetscErrorCode setupKSP();
    PetscErrorCode setSurfDisp();

    PetscErrorCode debug(const PetscReal time,const PetscInt steps,const Vec *var,const Vec *dvar);

  public:

    OdeSolver           *_quadrature;

    Lithosphere(Domain&D);
    ~Lithosphere();

    PetscErrorCode d_dt(PetscScalar const time,Vec const*var,Vec*dvar);
    PetscErrorCode integrate(); // will call OdeSolver method by same name
    PetscErrorCode timeMonitor(const PetscReal time, const PetscInt stepCount,const Vec* var,const Vec*dvar);
    PetscErrorCode debug(const PetscReal time,const PetscInt steps,const Vec *var,const Vec *dvar,const char *stage);

    // IO commands
    PetscErrorCode view();
    PetscErrorCode writeStep();
    PetscErrorCode read();
};

#include "odeSolver.hpp"

#endif